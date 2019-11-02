/* ----------------------------------------------------------------------------
Copyright (c) 2019, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
"Arenas" are fixed area's of OS memory from which we can allocate
large blocks (>= MI_ARENA_BLOCK_SIZE, 16MiB). Currently only used to
allocate in one arena consisting of huge OS pages -- otherwise it 
delegates to direct allocation from the OS.

In the future, we can expose an API to manually add more arenas which
is sometimes needed for embedded devices or shared memory for example.

The arena allocation needs to be thread safe and we use a lock-free scan
with on-demand coalescing.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-atomic.h"

#include <string.h>  // memset

// os.c
void* _mi_os_alloc_aligned(size_t size, size_t alignment, bool commit, bool* large, mi_os_tld_t* tld);
//int   _mi_os_alloc_huge_os_pages(size_t pages, double max_secs, void** pstart, size_t* pages_reserved, size_t* psize) mi_attr_noexcept;
void  _mi_os_free(void* p, size_t size, mi_stats_t* stats);
void* _mi_os_alloc_huge_os_pages(size_t pages, int numa_node, size_t* psize);
int   _mi_os_numa_node_count(void);


/* -----------------------------------------------------------
  Arena allocation
----------------------------------------------------------- */

#define MI_SEGMENT_ALIGN     MI_SEGMENT_SIZE
#define MI_ARENA_BLOCK_SIZE  MI_SEGMENT_SIZE 
#define MI_MAX_ARENAS        (64)

// Block info: bit 0 contains the `in_use` bit, the upper bits the
// size in count of arena blocks.
typedef uintptr_t mi_block_info_t;

// A memory arena descriptor
typedef struct mi_arena_s {
  uint8_t* start;                         // the start of the memory area
  size_t   block_count;                   // size of the area in arena blocks (of `MI_ARENA_BLOCK_SIZE`)
  int      numa_node;                     // associated NUMA node
  bool     is_zero_init;                  // is the arena zero initialized?
  bool     is_large;                      // large OS page allocated
  _Atomic(uintptr_t)       block_bottom;  // optimization to start the search for free blocks
  _Atomic(mi_block_info_t) blocks[1];     // `block_count` block info's
} mi_arena_t;


// The available arenas
static _Atomic(mi_arena_t*) mi_arenas[MI_MAX_ARENAS];
static _Atomic(uintptr_t)   mi_arena_count; // = 0


/* -----------------------------------------------------------
  Arena allocations get a memory id where the lower 8 bits are
  the arena index +1, and the upper bits the block index.
----------------------------------------------------------- */

// Use `0` as a special id for direct OS allocated memory.
#define MI_MEMID_OS   0

static size_t mi_memid_create(size_t arena_index, size_t block_index) {
  mi_assert_internal(arena_index < 0xFF);
  return ((block_index << 8) | ((arena_index+1) & 0xFF));
}

static void mi_memid_indices(size_t memid, size_t* arena_index, size_t* block_index) {
  mi_assert_internal(memid != MI_MEMID_OS);
  *arena_index = (memid & 0xFF) - 1;
  *block_index = (memid >> 8);
}

/* -----------------------------------------------------------
  Block info
----------------------------------------------------------- */

static bool mi_block_is_in_use(mi_block_info_t info) {
  return ((info&1) != 0);
}

static size_t mi_block_count(mi_block_info_t info) {
  return (info>>1);
}

static mi_block_info_t mi_block_info_create(size_t bcount, bool in_use) {
  return (((mi_block_info_t)bcount << 1) | (in_use ? 1 : 0));
}


/* -----------------------------------------------------------
  Thread safe allocation in an arena
----------------------------------------------------------- */

static void* mi_arena_allocx(mi_arena_t* arena, size_t start_idx, size_t end_idx, size_t needed_bcount, bool* is_zero, size_t* block_index)
{
  // Scan linearly through all block info's
  // Skipping used ranges, coalescing free ranges on demand.
  mi_assert_internal(needed_bcount > 0);
  mi_assert_internal(start_idx <= arena->block_count);
  mi_assert_internal(end_idx <= arena->block_count);
  _Atomic(mi_block_info_t)* block = &arena->blocks[start_idx];
  _Atomic(mi_block_info_t)* end = &arena->blocks[end_idx];
  while (block < end) {
    mi_block_info_t binfo = mi_atomic_read_relaxed(block);
    size_t bcount = mi_block_count(binfo);
    if (mi_block_is_in_use(binfo)) {
      // in-use, skip ahead
      mi_assert_internal(bcount > 0);
      block += bcount;
    }
    else {
      // free blocks
      if (bcount==0) {
        // optimization:
        // use 0 initialized blocks at the end, to use single atomic operation
        // initially to reduce contention (as we don't need to split)
        if (block + needed_bcount > end) {
          return NULL; // does not fit
        }
        else if (!mi_atomic_cas_weak(block, mi_block_info_create(needed_bcount, true), binfo)) {
          // ouch, someone else was quicker. Try again..
          continue;
        }
        else {
          // we got it: return a pointer to the claimed memory
          ptrdiff_t idx = (block - arena->blocks);
          *is_zero = arena->is_zero_init;
          *block_index = idx;
          return (arena->start + (idx*MI_ARENA_BLOCK_SIZE));
        }
      }

      mi_assert_internal(bcount>0);
      if (needed_bcount > bcount) {
#if 0 // MI_NO_ARENA_COALESCE
        block += bcount; // too small, skip to the next range
        continue;
#else
        // too small, try to coalesce
        _Atomic(mi_block_info_t)* block_next = block + bcount;
        if (block_next >= end) {
          return NULL; // does not fit
        }
        mi_block_info_t binfo_next = mi_atomic_read(block_next);
        size_t bcount_next = mi_block_count(binfo_next);
        if (mi_block_is_in_use(binfo_next)) {
          // next block is in use, cannot coalesce
          block += (bcount + bcount_next); // skip ahea over both blocks
        }
        else {
          // next block is free, try to coalesce
          // first set the next one to being used to prevent dangling ranges
          if (!mi_atomic_cas_strong(block_next, mi_block_info_create(bcount_next, true), binfo_next)) {
            // someone else got in before us.. try again
            continue;
          }
          else {
            if (!mi_atomic_cas_strong(block, mi_block_info_create(bcount + bcount_next, true), binfo)) {  // use strong to increase success chance
              // someone claimed/coalesced the block in the meantime
              // first free the next block again..
              bool ok = mi_atomic_cas_strong(block_next, mi_block_info_create(bcount_next, false), binfo_next); // must be strong
              mi_assert(ok); UNUSED(ok);
              // and try again
              continue;
            }
            else {
              // coalesced! try again
              // todo: we could optimize here to immediately claim the block if the
              // coalesced size is a fit instead of retrying. Keep it simple for now.
              continue;
            }
          }
        }
#endif    
      }
      else {  // needed_bcount <= bcount
        mi_assert_internal(needed_bcount <= bcount);
        // it fits, claim the whole block
        if (!mi_atomic_cas_weak(block, mi_block_info_create(bcount, true), binfo)) {
          // ouch, someone else was quicker. Try again..
          continue;
        }
        else {
          // got it, now split off the needed part
          if (needed_bcount < bcount) {
            mi_atomic_write(block + needed_bcount, mi_block_info_create(bcount - needed_bcount, false));
            mi_atomic_write(block, mi_block_info_create(needed_bcount, true));
          }
          // return a pointer to the claimed memory
          ptrdiff_t idx = (block - arena->blocks);
          *is_zero = false;
          *block_index = idx;
          return (arena->start + (idx*MI_ARENA_BLOCK_SIZE));
        }
      }
    }
  }
  // no success
  return NULL;
}

// Try to reduce search time by starting from bottom and wrap around.
static void* mi_arena_alloc(mi_arena_t* arena, size_t needed_bcount, bool* is_zero, size_t* block_index) 
{
  uintptr_t bottom = mi_atomic_read_relaxed(&arena->block_bottom);
  void* p = mi_arena_allocx(arena, bottom, arena->block_count, needed_bcount, is_zero, block_index);
  if (p == NULL && bottom > 0) {
    // try again from the start
    p = mi_arena_allocx(arena, 0, bottom, needed_bcount, is_zero, block_index);
  }
  if (p != NULL) {
    mi_atomic_write(&arena->block_bottom, *block_index);
  }
  return p;
}

/* -----------------------------------------------------------
  Arena Allocation
----------------------------------------------------------- */

static void* mi_arena_alloc_from(mi_arena_t* arena, size_t arena_index, size_t needed_bcount, 
                                    bool* commit, bool* large, bool* is_zero,
                                    size_t* memid) 
{
  size_t block_index = SIZE_MAX;
  void* p = mi_arena_alloc(arena, needed_bcount, is_zero, &block_index);
  if (p != NULL) {
    mi_assert_internal(block_index != SIZE_MAX);
#if MI_DEBUG>=1
    _Atomic(mi_block_info_t)* block = &arena->blocks[block_index];
    mi_block_info_t binfo = mi_atomic_read(block);
    mi_assert_internal(mi_block_is_in_use(binfo));
    mi_assert_internal(mi_block_count(binfo) >= needed_bcount);
#endif
    *memid = mi_memid_create(arena_index, block_index);
    *commit = true;           // TODO: support commit on demand?
    *large = arena->is_large;
  }
  return p;
}

void* _mi_arena_alloc_aligned(size_t size, size_t alignment, 
                              bool* commit, bool* large, bool* is_zero, 
                              size_t* memid, mi_os_tld_t* tld) 
{
  mi_assert_internal(memid != NULL && tld != NULL);
  mi_assert_internal(size > 0);
  *memid = MI_MEMID_OS;  
  *is_zero = false;
  bool default_large = false;
  if (large==NULL) large = &default_large;  // ensure `large != NULL`

  // try to allocate in an arena if the alignment is small enough
  // and if there is not too much waste around the `MI_ARENA_BLOCK_SIZE`.
  if (alignment <= MI_SEGMENT_ALIGN &&
      size >= 3*(MI_ARENA_BLOCK_SIZE/4) &&  // > 48MiB (not more than 25% waste)
      !(size > MI_ARENA_BLOCK_SIZE && size < 3*(MI_ARENA_BLOCK_SIZE/2)) // ! <64MiB - 96MiB>
     ) 
  {
    size_t asize = _mi_align_up(size, MI_ARENA_BLOCK_SIZE);
    size_t bcount = asize / MI_ARENA_BLOCK_SIZE;
    int numa_node = _mi_os_numa_node(); // current numa node

    mi_assert_internal(size <= bcount*MI_ARENA_BLOCK_SIZE);
    // try numa affine allocation
    for (size_t i = 0; i < MI_MAX_ARENAS; i++) {
      mi_arena_t* arena = (mi_arena_t*)mi_atomic_read_ptr_relaxed(mi_atomic_cast(void*, &mi_arenas[i]));
      if (arena==NULL) break; // end reached
      if ((arena->numa_node<0 || arena->numa_node==numa_node) && // numa local?
          (*large || !arena->is_large)) // large OS pages allowed, or arena is not large OS pages
      { 
        void* p = mi_arena_alloc_from(arena, i, bcount, commit, large, is_zero, memid);
        mi_assert_internal((uintptr_t)p % alignment == 0);
        if (p != NULL) return p;
      }
    }
    // try from another numa node instead..
    for (size_t i = 0; i < MI_MAX_ARENAS; i++) {
      mi_arena_t* arena = (mi_arena_t*)mi_atomic_read_ptr_relaxed(mi_atomic_cast(void*, &mi_arenas[i]));
      if (arena==NULL) break; // end reached
      if ((arena->numa_node>=0 && arena->numa_node!=numa_node) && // not numa local!
          (*large || !arena->is_large)) // large OS pages allowed, or arena is not large OS pages
      {
        void* p = mi_arena_alloc_from(arena, i, bcount, commit, large, is_zero, memid);
        mi_assert_internal((uintptr_t)p % alignment == 0);
        if (p != NULL) return p;
      }
    }
  }

  // finally, fall back to the OS
  *is_zero = true;
  *memid = MI_MEMID_OS;
  return _mi_os_alloc_aligned(size, alignment, *commit, large, tld);
}

void* _mi_arena_alloc(size_t size, bool* commit, bool* large, bool* is_zero, size_t* memid, mi_os_tld_t* tld) 
{
  return _mi_arena_alloc_aligned(size, MI_ARENA_BLOCK_SIZE, commit, large, is_zero, memid, tld);
}

/* -----------------------------------------------------------
  Arena free
----------------------------------------------------------- */

void _mi_arena_free(void* p, size_t size, size_t memid, mi_stats_t* stats) {
  mi_assert_internal(size > 0 && stats != NULL);
  if (p==NULL) return;
  if (size==0) return;
  if (memid == MI_MEMID_OS) {
    // was a direct OS allocation, pass through
    _mi_os_free(p, size, stats);
  }
  else {
    // allocated in an arena
    size_t arena_idx;
    size_t block_idx;
    mi_memid_indices(memid, &arena_idx, &block_idx);
    mi_assert_internal(arena_idx < MI_MAX_ARENAS);
    mi_arena_t* arena = (mi_arena_t*)mi_atomic_read_ptr_relaxed(mi_atomic_cast(void*, &mi_arenas[arena_idx]));
    mi_assert_internal(arena != NULL);
    if (arena == NULL) {
      _mi_fatal_error("trying to free from non-existent arena: %p, size %zu, memid: 0x%zx\n", p, size, memid);
      return;
    }
    mi_assert_internal(arena->block_count > block_idx);
    if (arena->block_count <= block_idx) {
      _mi_fatal_error("trying to free from non-existent block: %p, size %zu, memid: 0x%zx\n", p, size, memid);
      return;
    }
    _Atomic(mi_block_info_t)* block = &arena->blocks[block_idx];
    mi_block_info_t binfo = mi_atomic_read_relaxed(block);
    mi_assert_internal(mi_block_is_in_use(binfo));
    mi_assert_internal(mi_block_count(binfo)*MI_ARENA_BLOCK_SIZE >= size);
    if (!mi_block_is_in_use(binfo)) {
      _mi_fatal_error("trying to free an already freed block: %p, size %zu\n", p, size);
      return;
    };
    bool ok = mi_atomic_cas_strong(block, mi_block_info_create(mi_block_count(binfo), false), binfo);
    mi_assert_internal(ok);
    if (!ok) {
      _mi_warning_message("unable to free arena block: %p, info 0x%zx", p, binfo);
    }
    if (block_idx < mi_atomic_read_relaxed(&arena->block_bottom)) {
      mi_atomic_write(&arena->block_bottom, block_idx);
    }
  }
}

/* -----------------------------------------------------------
  Add an arena.
----------------------------------------------------------- */

static bool mi_arena_add(mi_arena_t* arena) {
  mi_assert_internal(arena != NULL);
  mi_assert_internal((uintptr_t)arena->start % MI_SEGMENT_ALIGN == 0);
  mi_assert_internal(arena->block_count > 0);
  mi_assert_internal(mi_mem_is_zero(arena->blocks,arena->block_count*sizeof(mi_block_info_t)));

  uintptr_t i = mi_atomic_addu(&mi_arena_count,1);
  if (i >= MI_MAX_ARENAS) {
    mi_atomic_subu(&mi_arena_count, 1);
    return false;
  }
  mi_atomic_write_ptr(mi_atomic_cast(void*,&mi_arenas[i]), arena);
  return true;
}


/* -----------------------------------------------------------
  Reserve a huge page arena.
----------------------------------------------------------- */
#include <errno.h> // ENOMEM

// reserve at a specific numa node
int mi_reserve_huge_os_pages_at(size_t pages, int numa_node) mi_attr_noexcept {
  size_t hsize = 0;
  void* p = _mi_os_alloc_huge_os_pages(pages, numa_node, &hsize);
  if (p==NULL) return ENOMEM;
  _mi_verbose_message("reserved %zu huge (1GiB) pages\n", pages);
  
  size_t bcount = hsize / MI_ARENA_BLOCK_SIZE;
  size_t asize = sizeof(mi_arena_t) + (bcount*sizeof(mi_block_info_t));  // one too much
  mi_arena_t* arena = (mi_arena_t*)_mi_os_alloc(asize, &_mi_stats_main); // TODO: can we avoid allocating from the OS?
  if (arena == NULL) {
    _mi_os_free(p, hsize, &_mi_stats_main);
    return ENOMEM;
  }
  arena->block_count = bcount;
  arena->start = (uint8_t*)p;
  arena->block_bottom = 0;
  arena->numa_node = numa_node; // TODO: or get the current numa node if -1? (now it allows anyone to allocate on -1)
  arena->is_large = true;
  arena->is_zero_init = true;
  memset(arena->blocks, 0, bcount * sizeof(mi_block_info_t));
  mi_arena_add(arena);
  return 0;
}


// reserve huge pages evenly among all numa nodes. 
int mi_reserve_huge_os_pages_interleave(size_t pages) mi_attr_noexcept {
  if (pages == 0) return 0;

  // pages per numa node
  int numa_count = _mi_os_numa_node_count();
  if (numa_count <= 0) numa_count = 1;
  size_t pages_per = pages / numa_count;
  if (pages_per == 0) pages_per = 1;
  
  // reserve evenly among numa nodes
  for (int numa_node = 0; numa_node < numa_count && pages > 0; numa_node++) {
    int err = mi_reserve_huge_os_pages_at((pages_per > pages ? pages : pages_per), numa_node);
    if (err) return err;
    if (pages < pages_per) {
      pages = 0;
    }
    else {
      pages -= pages_per;
    }
  }

  return 0;
}

int mi_reserve_huge_os_pages(size_t pages, double max_secs, size_t* pages_reserved) mi_attr_noexcept {
  UNUSED(max_secs);
  _mi_verbose_message("mi_reserve_huge_os_pages is deprecated: use mi_reserve_huge_os_pages_interleave/at instead\n");
  if (pages_reserved != NULL) *pages_reserved = 0;
  int err = mi_reserve_huge_os_pages_interleave(pages);  
  if (err==0 && pages_reserved!=NULL) *pages_reserved = pages;
  return err;
}
