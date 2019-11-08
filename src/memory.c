/* ----------------------------------------------------------------------------
Copyright (c) 2019, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
This implements a layer between the raw OS memory (VirtualAlloc/mmap/sbrk/..)
and the segment and huge object allocation by mimalloc. There may be multiple
implementations of this (one could be the identity going directly to the OS,
another could be a simple cache etc), but the current one uses large "regions".
In contrast to the rest of mimalloc, the "regions" are shared between threads and
need to be accessed using atomic operations.
We need this memory layer between the raw OS calls because of:
1. on `sbrk` like systems (like WebAssembly) we need our own memory maps in order
   to reuse memory effectively.
2. It turns out that for large objects, between 1MiB and 32MiB (?), the cost of
   an OS allocation/free is still (much) too expensive relative to the accesses 
   in that object :-( (`malloc-large` tests this). This means we need a cheaper 
   way to reuse memory.
3. This layer allows for NUMA aware allocation.

Possible issues:
- (2) can potentially be addressed too with a small cache per thread which is much
  simpler. Generally though that requires shrinking of huge pages, and may overuse
  memory per thread. (and is not compatible with `sbrk`).
- Since the current regions are per-process, we need atomic operations to
  claim blocks which may be contended
- In the worst case, we need to search the whole region map (16KiB for 256GiB)
  linearly. At what point will direct OS calls be faster? Is there a way to
  do this better without adding too much complexity?
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-atomic.h"

#include <string.h>  // memset

#include "bitmap.inc.c"

// Internal raw OS interface
size_t  _mi_os_large_page_size();
bool    _mi_os_protect(void* addr, size_t size);
bool    _mi_os_unprotect(void* addr, size_t size);
bool    _mi_os_commit(void* p, size_t size, bool* is_zero, mi_stats_t* stats);
bool    _mi_os_decommit(void* p, size_t size, mi_stats_t* stats);
bool    _mi_os_reset(void* p, size_t size, mi_stats_t* stats);
bool    _mi_os_unreset(void* p, size_t size, bool* is_zero, mi_stats_t* stats);

// arena.c
void    _mi_arena_free(void* p, size_t size, size_t memid, mi_stats_t* stats);
void*   _mi_arena_alloc(size_t size, bool* commit, bool* large, bool* is_zero, size_t* memid, mi_os_tld_t* tld);
void*   _mi_arena_alloc_aligned(size_t size, size_t alignment, bool* commit, bool* large, bool* is_zero, size_t* memid, mi_os_tld_t* tld);


// Constants
#if (MI_INTPTR_SIZE==8)
#define MI_HEAP_REGION_MAX_SIZE    (256 * GiB)  // 40KiB for the region map 
#elif (MI_INTPTR_SIZE==4)
#define MI_HEAP_REGION_MAX_SIZE    (3 * GiB)    // ~ KiB for the region map
#else
#error "define the maximum heap space allowed for regions on this platform"
#endif

#define MI_SEGMENT_ALIGN          MI_SEGMENT_SIZE

#define MI_REGION_SIZE            (MI_SEGMENT_SIZE * MI_BITMAP_FIELD_BITS)    // 256MiB  (64MiB on 32 bits)
#define MI_REGION_MAX_ALLOC_SIZE  (MI_REGION_SIZE/4)                          // 64MiB
#define MI_REGION_MAX             (MI_HEAP_REGION_MAX_SIZE / MI_REGION_SIZE)  // 1024  (48 on 32 bits)


// Region info is a pointer to the memory region and two bits for 
// its flags: is_large, and is_committed.
typedef uintptr_t mi_region_info_t;

static inline mi_region_info_t mi_region_info_create(void* start, bool is_large, bool is_committed) {
  return ((uintptr_t)start | ((uintptr_t)(is_large?1:0) << 1) | (is_committed?1:0));
}

static inline void* mi_region_info_read(mi_region_info_t info, bool* is_large, bool* is_committed) {
  if (is_large) *is_large = ((info&0x02) != 0);
  if (is_committed) *is_committed = ((info&0x01) != 0);
  return (void*)(info & ~0x03);
}


// A region owns a chunk of REGION_SIZE (256MiB) (virtual) memory with
// a bit map with one bit per MI_SEGMENT_SIZE (4MiB) block.
typedef struct mem_region_s {
  volatile _Atomic(mi_region_info_t) info;       // start of the memory area (and flags)
  volatile _Atomic(uintptr_t)        numa_node;  // associated numa node + 1 (so 0 is no association)
  size_t   arena_memid;                          // if allocated from a (huge page) arena
} mem_region_t;

// The region map
static mem_region_t regions[MI_REGION_MAX];

// A bit mask per region for its claimed MI_SEGMENT_SIZE blocks.
static mi_bitmap_field_t regions_map[MI_REGION_MAX];

// A bit mask per region to track which blocks are dirty (= potentially written to)
static mi_bitmap_field_t regions_dirty[MI_REGION_MAX];

// Allocated regions
static volatile _Atomic(uintptr_t) regions_count; // = 0;        


/* ----------------------------------------------------------------------------
Utility functions
-----------------------------------------------------------------------------*/

// Blocks (of 4MiB) needed for the given size.
static size_t mi_region_block_count(size_t size) {
  mi_assert_internal(size <= MI_REGION_MAX_ALLOC_SIZE);
  return (size + MI_SEGMENT_SIZE - 1) / MI_SEGMENT_SIZE;
}

// Return a rounded commit/reset size such that we don't fragment large OS pages into small ones.
static size_t mi_good_commit_size(size_t size) {
  if (size > (SIZE_MAX - _mi_os_large_page_size())) return size;
  return _mi_align_up(size, _mi_os_large_page_size());
}

// Return if a pointer points into a region reserved by us.
bool mi_is_in_heap_region(const void* p) mi_attr_noexcept {
  if (p==NULL) return false;
  size_t count = mi_atomic_read_relaxed(&regions_count);
  for (size_t i = 0; i < count; i++) {
    uint8_t* start = (uint8_t*)mi_region_info_read( mi_atomic_read_relaxed(&regions[i].info), NULL, NULL);
    if (start != NULL && (uint8_t*)p >= start && (uint8_t*)p < start + MI_REGION_SIZE) return true;
  }
  return false;
}


static size_t mi_memid_create(mi_bitmap_index_t bitmap_idx) {
  return bitmap_idx<<1;
}

static size_t mi_memid_create_from_arena(size_t arena_memid) {
  return (arena_memid << 1) | 1;
}

static bool mi_memid_is_arena(size_t id) {
  return ((id&1)==1);
}

static bool mi_memid_indices(size_t id, mi_bitmap_index_t* bitmap_idx, size_t* arena_memid) {
  if (mi_memid_is_arena(id)) {
    *arena_memid = (id>>1);
    return true;
  }
  else {
    *bitmap_idx = (mi_bitmap_index_t)(id>>1);
    return false;
  }
}

/* ----------------------------------------------------------------------------
  Ensure a region is allocated from the OS (or an arena)
-----------------------------------------------------------------------------*/

static bool mi_region_ensure_allocated(size_t idx, bool allow_large, mi_region_info_t* pinfo, mi_os_tld_t* tld)
{
  // ensure the region is reserved
  mi_region_info_t info = mi_atomic_read(&regions[idx].info);
  if (mi_unlikely(info == 0))
  {
    bool region_commit = mi_option_is_enabled(mi_option_eager_region_commit);
    bool region_large = allow_large;
    bool is_zero = false;
    size_t arena_memid = 0;
    void* const start = _mi_arena_alloc_aligned(MI_REGION_SIZE, MI_SEGMENT_ALIGN, &region_commit, &region_large, &is_zero, &arena_memid, tld);
    mi_assert_internal(!(region_large && !allow_large));

    if (start == NULL) {
      // failure to allocate from the OS! fail
      *pinfo = 0;
      return false;
    }

    // set the newly allocated region
    // try to initialize any region up to 4 beyond the current one in
    // care multiple threads are doing this concurrently (common at startup)    
    info = mi_region_info_create(start, region_large, region_commit);
    bool claimed = false;
    for (size_t i = 0; i <= 4 && idx + i < MI_REGION_MAX && !claimed; i++) {
      if (!is_zero) {
        // set dirty bits before CAS; this might race with a zero block but that is ok. 
        // (but writing before cas prevents a concurrent allocation to assume it is not dirty)
        mi_atomic_write(&regions_dirty[idx+i], MI_BITMAP_FIELD_FULL);
      }
      if (mi_atomic_cas_strong(&regions[idx+i].info, info, 0)) {
        // claimed!
        regions[idx+i].arena_memid = arena_memid;
        mi_atomic_write(&regions[idx+i].numa_node, _mi_os_numa_node(tld) + 1);
        mi_atomic_increment(&regions_count);
        claimed = true;
      }
    }
    if (!claimed) {
      // free our OS allocation if we didn't succeed to store it in some region
      _mi_arena_free(start, MI_REGION_SIZE, arena_memid, tld->stats);      
    }
    // continue with the actual info at our index in case another thread was quicker with the allocation
    info = mi_atomic_read(&regions[idx].info);
    mi_assert_internal(info != 0);
  }
  mi_assert_internal(info == mi_atomic_read(&regions[idx].info));
  mi_assert_internal(info != 0);
  *pinfo = info;
  return true;
}


/* ----------------------------------------------------------------------------
  Commit blocks
-----------------------------------------------------------------------------*/

static void* mi_region_commit_blocks(mi_bitmap_index_t bitmap_idx, mi_region_info_t info, size_t blocks, size_t size, bool* commit, bool* is_large, bool* is_zero, mi_os_tld_t* tld)
{
  // set dirty bits
  *is_zero = mi_bitmap_claim(regions_dirty, MI_REGION_MAX, blocks, bitmap_idx);

  // Commit the blocks to memory
  bool region_is_committed = false;
  bool region_is_large = false;
  void* start = mi_region_info_read(info, &region_is_large, &region_is_committed);
  mi_assert_internal(!(region_is_large && !*is_large));
  mi_assert_internal(start!=NULL);

  void* blocks_start = (uint8_t*)start + (mi_bitmap_index_bit_in_field(bitmap_idx) * MI_SEGMENT_SIZE);
  if (*commit && !region_is_committed) {
    // ensure commit 
    bool commit_zero = false;
    _mi_os_commit(blocks_start, mi_good_commit_size(size), &commit_zero, tld->stats);  // only commit needed size (unless using large OS pages)
    if (commit_zero) *is_zero = true;
  }
  else if (!*commit && region_is_committed) {
    // but even when no commit is requested, we might have committed anyway (in a huge OS page for example)
    *commit = true;
  }

  // and return the allocation  
  mi_assert_internal(blocks_start != NULL);
  *is_large = region_is_large;
  return blocks_start;
}

/* ----------------------------------------------------------------------------
  Claim and allocate blocks in a region
-----------------------------------------------------------------------------*/

static bool mi_region_alloc_blocks(
  size_t idx, size_t blocks, size_t size,
  bool* commit, bool* allow_large, bool* is_zero,
  void** p, size_t* id, mi_os_tld_t* tld)
{
  mi_bitmap_index_t bitmap_idx;
  if (!mi_bitmap_try_claim_field(regions_map, idx, blocks, &bitmap_idx)) {
    return true; // no error, but also no success
  }
  mi_region_info_t info;
  if (!mi_region_ensure_allocated(idx,*allow_large,&info,tld)) {
    // failed to allocate region memory, unclaim the bits and fail
    mi_bitmap_unclaim(regions_map, MI_REGION_MAX, blocks, bitmap_idx);
    return false;
  }
  *p = mi_region_commit_blocks(bitmap_idx,info,blocks,size,commit,allow_large,is_zero,tld);
  *id = mi_memid_create(bitmap_idx);
  return true;
}


/* ----------------------------------------------------------------------------
  Try to allocate blocks in suitable regions
-----------------------------------------------------------------------------*/

static bool mi_region_is_suitable(int numa_node, size_t idx, bool commit, bool allow_large ) {
  uintptr_t m = mi_atomic_read_relaxed(&regions_map[idx]);
  if (m == MI_BITMAP_FIELD_FULL) return false;
  if (numa_node >= 0) {  // use negative numa node to always succeed
    int rnode = ((int)mi_atomic_read_relaxed(&regions[idx].numa_node)) - 1;
    if (rnode >= 0 && rnode != numa_node) return false;
  }
  if (commit && allow_large) return true;  // always ok

  // otherwise skip incompatible regions if possible. 
  // this is not guaranteed due to multiple threads allocating at the same time but
  // that's ok. In secure mode, large is never allowed for any thread, so that works out; 
  // otherwise we might just not be able to reset/decommit individual pages sometimes.
  mi_region_info_t info = mi_atomic_read_relaxed(&regions[idx].info);
  bool is_large;
  bool is_committed;
  void* start = mi_region_info_read(info, &is_large, &is_committed);
  // note: we also skip if commit is false and the region is committed,
  // that is a bit strong but prevents allocation of eager delayed segments in 
  // committed memory
  bool ok = (start == NULL || (commit || !is_committed) || (allow_large || !is_large)); // Todo: test with one bitmap operation?
  return ok;
}

// Try to allocate `blocks` in a `region` at `idx` of a given `size`. Does a quick check before trying to claim.
// Returns `false` on an error (OOM); `true` otherwise. `p` and `id` are only written
// if the blocks were successfully claimed so ensure they are initialized to NULL/0 before the call.
// (not being able to claim is not considered an error so check for `p != NULL` afterwards).
static bool mi_region_try_alloc_blocks(
  int numa_node, size_t idx, size_t blocks, size_t size,
  bool* commit, bool* allow_large, bool* is_zero,
  void** p, size_t* id, mi_os_tld_t* tld)
{
  // check if there are available blocks in the region..
  mi_assert_internal(idx < MI_REGION_MAX);
  if (mi_region_is_suitable(numa_node, idx, *commit, *allow_large)) {
    return mi_region_alloc_blocks(idx, blocks, size, commit, allow_large, is_zero, p, id, tld);
  }
  return true;  // no error, but no success either
}

/* ----------------------------------------------------------------------------
 Allocation
-----------------------------------------------------------------------------*/

// Allocate `size` memory aligned at `alignment`. Return non NULL on success, with a given memory `id`.
// (`id` is abstract, but `id = idx*MI_REGION_MAP_BITS + bitidx`)
void* _mi_mem_alloc_aligned(size_t size, size_t alignment, bool* commit, bool* large, bool* is_zero, 
                            size_t* id, mi_os_tld_t* tld)
{
  mi_assert_internal(id != NULL && tld != NULL);
  mi_assert_internal(size > 0);
  *id = 0;
  *is_zero = false;
  bool default_large = false;
  if (large==NULL) large = &default_large;  // ensure `large != NULL`  

  // use direct OS allocation for huge blocks or alignment 
  if (size > MI_REGION_MAX_ALLOC_SIZE || alignment > MI_SEGMENT_ALIGN) {
    size_t arena_memid = 0;
    void* p = _mi_arena_alloc_aligned(mi_good_commit_size(size), alignment, commit, large, is_zero, &arena_memid, tld);  // round up size
    *id = mi_memid_create_from_arena(arena_memid);
    return p;
  }

  // always round size to OS page size multiple (so commit/decommit go over the entire range)
  // TODO: use large OS page size here?
  size = _mi_align_up(size, _mi_os_page_size());

  // calculate the number of needed blocks
  const size_t blocks = mi_region_block_count(size);
  mi_assert_internal(blocks > 0 && blocks <= 8*MI_INTPTR_SIZE);

  // find a range of free blocks
  const int numa_node = (_mi_os_numa_node_count() <= 1 ? -1 : _mi_os_numa_node(tld));
  void* p = NULL;
  const size_t count = mi_atomic_read(&regions_count);
  size_t idx = tld->region_idx; // Or start at 0 to reuse low addresses? 
  for (size_t visited = 0; visited < count; visited++, idx++) {
    if (idx >= count) idx = 0;  // wrap around
    if (!mi_region_try_alloc_blocks(numa_node, idx, blocks, size, commit, large, is_zero, &p, id, tld)) return NULL; // error
    if (p != NULL) break;
  }

  if (p == NULL) {
    // no free range in existing regions -- try to extend beyond the count.. but at most 8 regions
    for (idx = count; idx < mi_atomic_read_relaxed(&regions_count) + 8 && idx < MI_REGION_MAX; idx++) {
      if (!mi_region_try_alloc_blocks(numa_node, idx, blocks, size, commit, large, is_zero, &p, id, tld)) return NULL; // error
      if (p != NULL) break;
    }
  }

  if (p == NULL) {
    // we could not find a place to allocate, fall back to the os directly
    _mi_warning_message("unable to allocate from region: size %zu\n", size);    
    size_t arena_memid = 0;
    p = _mi_arena_alloc_aligned(size, alignment, commit, large, is_zero, &arena_memid, tld);
    *id = mi_memid_create_from_arena(arena_memid);
  }
  else {
    tld->region_idx = idx;  // next start of search
  }

  mi_assert_internal( p == NULL || (uintptr_t)p % alignment == 0);
  return p;
}



/* ----------------------------------------------------------------------------
Free
-----------------------------------------------------------------------------*/

// Free previously allocated memory with a given id.
void _mi_mem_free(void* p, size_t size, size_t id, mi_stats_t* stats) {
  mi_assert_internal(size > 0 && stats != NULL);
  if (p==NULL) return;
  if (size==0) return;
  size_t arena_memid = 0;
  mi_bitmap_index_t bitmap_idx;
  if (mi_memid_indices(id,&bitmap_idx,&arena_memid)) {
   // was a direct arena allocation, pass through
    _mi_arena_free(p, size, arena_memid, stats);
  }
  else {
    // allocated in a region
    mi_assert_internal(size <= MI_REGION_MAX_ALLOC_SIZE); if (size > MI_REGION_MAX_ALLOC_SIZE) return;
    // we can align the size up to page size (as we allocate that way too)
    // this ensures we fully commit/decommit/reset
    size = _mi_align_up(size, _mi_os_page_size());    
    const size_t blocks = mi_region_block_count(size);
    const size_t idx    = mi_bitmap_index_field(bitmap_idx);
    const size_t bitidx = mi_bitmap_index_bit_in_field(bitmap_idx);
    mi_assert_internal(idx < MI_REGION_MAX); if (idx >= MI_REGION_MAX) return; // or `abort`?
    mem_region_t* region = &regions[idx];
    mi_region_info_t info = mi_atomic_read(&region->info);
    bool is_large;
    bool is_eager_committed;
    void* start = mi_region_info_read(info,&is_large,&is_eager_committed);
    mi_assert_internal(start != NULL);
    void* blocks_start = (uint8_t*)start + (bitidx * MI_SEGMENT_SIZE);
    mi_assert_internal(blocks_start == p); // not a pointer in our area?
    mi_assert_internal(bitidx + blocks <= MI_BITMAP_FIELD_BITS);
    if (blocks_start != p || bitidx + blocks > MI_BITMAP_FIELD_BITS) return; // or `abort`?

    // decommit (or reset) the blocks to reduce the working set.
    // TODO: implement delayed decommit/reset as these calls are too expensive
    // if the memory is reused soon.
    // reset: 10x slowdown on malloc-large, decommit: 17x slowdown on malloc-large
    if (!is_large) {
      if (mi_option_is_enabled(mi_option_segment_reset)) {
        if (!is_eager_committed &&  // cannot reset large pages
          (mi_option_is_enabled(mi_option_eager_commit) ||  // cannot reset halfway committed segments, use `option_page_reset` instead
            mi_option_is_enabled(mi_option_reset_decommits))) // but we can decommit halfway committed segments
        {
          _mi_os_reset(p, size, stats);
          //_mi_os_decommit(p, size, stats);  // todo: and clear dirty bits?
        }
      }
    }    
    if (!is_eager_committed) {
      // adjust commit statistics as we commit again when re-using the same slot
      _mi_stat_decrease(&stats->committed, mi_good_commit_size(size));
    }

    // TODO: should we free empty regions? currently only done _mi_mem_collect.
    // this frees up virtual address space which might be useful on 32-bit systems?

    // and unclaim
    mi_bitmap_unclaim(regions_map, MI_REGION_MAX, blocks, bitmap_idx);
  }
}


/* ----------------------------------------------------------------------------
  collection
-----------------------------------------------------------------------------*/
void _mi_mem_collect(mi_stats_t* stats) {
  // free every region that has no segments in use.
  for (size_t i = 0; i < regions_count; i++) {
    if (mi_atomic_read_relaxed(&regions_map[i]) == 0) {
      // if no segments used, try to claim the whole region
      uintptr_t m;
      do {
        m = mi_atomic_read_relaxed(&regions_map[i]);
      } while(m == 0 && !mi_atomic_cas_weak(&regions_map[i], MI_BITMAP_FIELD_FULL, 0 ));
      if (m == 0) {
        // on success, free the whole region
        bool is_eager_committed;
        void* start = mi_region_info_read(mi_atomic_read(&regions[i].info), NULL, &is_eager_committed);
        if (start != NULL) { // && !_mi_os_is_huge_reserved(start)) {
          _mi_arena_free(start, MI_REGION_SIZE, regions[i].arena_memid, stats);
        }
        // and release
        mi_atomic_write(&regions[i].info,0);
        mi_atomic_write(&regions_dirty[i],0);
        mi_atomic_write(&regions_map[i],0);
      }
    }
  }
}


/* ----------------------------------------------------------------------------
  Other
-----------------------------------------------------------------------------*/

bool _mi_mem_commit(void* p, size_t size, bool* is_zero, mi_stats_t* stats) {
  return _mi_os_commit(p, size, is_zero, stats);
}

bool _mi_mem_decommit(void* p, size_t size, mi_stats_t* stats) {
  return _mi_os_decommit(p, size, stats);
}

bool _mi_mem_reset(void* p, size_t size, mi_stats_t* stats) {
  return _mi_os_reset(p, size, stats);
}

bool _mi_mem_unreset(void* p, size_t size, bool* is_zero, mi_stats_t* stats) {
  return _mi_os_unreset(p, size, is_zero, stats);
}

bool _mi_mem_protect(void* p, size_t size) {
  return _mi_os_protect(p, size);
}

bool _mi_mem_unprotect(void* p, size_t size) {
  return _mi_os_unprotect(p, size);
}
