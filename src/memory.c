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

// local
static bool mi_delay_remove(mi_delay_slots_t* delay_slots, void* p, size_t size);


// Constants
#if (MI_INTPTR_SIZE==8)
#define MI_HEAP_REGION_MAX_SIZE    (256 * GiB)  // 48KiB for the region map 
#elif (MI_INTPTR_SIZE==4)
#define MI_HEAP_REGION_MAX_SIZE    (3 * GiB)    // ~ KiB for the region map
#else
#error "define the maximum heap space allowed for regions on this platform"
#endif

#define MI_SEGMENT_ALIGN          MI_SEGMENT_SIZE

#define MI_REGION_MAX_BLOCKS      MI_BITMAP_FIELD_BITS
#define MI_REGION_SIZE            (MI_SEGMENT_SIZE * MI_BITMAP_FIELD_BITS)    // 256MiB  (64MiB on 32 bits)
#define MI_REGION_MAX             (MI_HEAP_REGION_MAX_SIZE / MI_REGION_SIZE)  // 1024  (48 on 32 bits)
#define MI_REGION_MAX_OBJ_BLOCKS  (MI_REGION_MAX_BLOCKS/4)                    // 64MiB
#define MI_REGION_MAX_OBJ_SIZE    (MI_REGION_MAX_OBJ_BLOCKS*MI_SEGMENT_SIZE)  

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
  volatile _Atomic(mi_region_info_t) info;        // start of the memory area (and flags)
  volatile _Atomic(uintptr_t)        numa_node;   // associated numa node + 1 (so 0 is no association)
  mi_bitmap_field_t                  in_use;      // bit per in-use block
  mi_bitmap_field_t                  dirty;       // track if non-zero per block
  mi_bitmap_field_t                  commit;      // track if committed per block (if `!info.is_committed))
  size_t                             arena_memid; // if allocated from a (huge page) arena
} mem_region_t;

// The region map
static mem_region_t regions[MI_REGION_MAX];

// Allocated regions
static volatile _Atomic(uintptr_t) regions_count; // = 0;        


/* ----------------------------------------------------------------------------
Utility functions
-----------------------------------------------------------------------------*/

// Blocks (of 4MiB) needed for the given size.
static size_t mi_region_block_count(size_t size) {
  return _mi_divide_up(size, MI_SEGMENT_SIZE);
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


static size_t mi_memid_create(mem_region_t* region, mi_bitmap_index_t bit_idx) {
  mi_assert_internal(bit_idx < MI_BITMAP_FIELD_BITS);
  size_t idx = region - regions;
  mi_assert_internal(&regions[idx] == region);
  return (idx*MI_BITMAP_FIELD_BITS + bit_idx)<<1;
}

static size_t mi_memid_create_from_arena(size_t arena_memid) {
  return (arena_memid << 1) | 1;
}

static bool mi_memid_is_arena(size_t id) {
  return ((id&1)==1);
}

static bool mi_memid_indices(size_t id, mem_region_t** region, mi_bitmap_index_t* bit_idx, size_t* arena_memid) {
  if (mi_memid_is_arena(id)) {
    *arena_memid = (id>>1);
    return true;
  }
  else {
    size_t idx = (id >> 1) / MI_BITMAP_FIELD_BITS;
    *bit_idx   = (mi_bitmap_index_t)(id>>1) % MI_BITMAP_FIELD_BITS;
    *region    = &regions[idx];
    return false;
  }
}

/* ----------------------------------------------------------------------------
  Allocate a region is allocated from the OS (or an arena)
-----------------------------------------------------------------------------*/

static bool mi_region_try_alloc_os(size_t blocks, bool commit, bool allow_large, mem_region_t** region, mi_bitmap_index_t* bit_idx, mi_os_tld_t* tld)
{
  // not out of regions yet?
  if (mi_atomic_read_relaxed(&regions_count) >= MI_REGION_MAX - 1) return false;

  // try to allocate a fresh region from the OS
  bool region_commit = (commit && mi_option_is_enabled(mi_option_eager_region_commit));
  bool region_large = (commit && allow_large);
  bool is_zero = false;
  size_t arena_memid = 0;
  void* const start = _mi_arena_alloc_aligned(MI_REGION_SIZE, MI_SEGMENT_ALIGN, &region_commit, &region_large, &is_zero, &arena_memid, tld);
  if (start == NULL) return false;
  mi_assert_internal(!(region_large && !allow_large));

  // claim a fresh slot
  const uintptr_t idx = mi_atomic_increment(&regions_count);
  if (idx >= MI_REGION_MAX) {
    mi_atomic_decrement(&regions_count);
    _mi_arena_free(start, MI_REGION_SIZE, arena_memid, tld->stats);
    return false;
  }

  // allocated, initialize and claim the initial blocks
  mem_region_t* r = &regions[idx];
  r->numa_node = _mi_os_numa_node(tld) + 1;
  r->arena_memid = arena_memid;
  mi_atomic_write(&r->in_use, 0);
  mi_atomic_write(&r->dirty, (is_zero ? 0 : ~0UL));
  mi_atomic_write(&r->commit, (region_commit ? ~0UL : 0));
  *bit_idx = 0;
  mi_bitmap_claim(&r->in_use, 1, blocks, *bit_idx, NULL);

  // and share it 
  mi_atomic_write(&r->info, mi_region_info_create(start, region_large, region_commit)); // now make it available to others
  *region = r;
  return true;
}

/* ----------------------------------------------------------------------------
  Try to claim blocks in suitable regions
-----------------------------------------------------------------------------*/

static bool mi_region_is_suitable(const mem_region_t* region, int numa_node, bool allow_large ) {
  // initialized at all?
  mi_region_info_t info = mi_atomic_read_relaxed(&region->info);
  if (info==0) return false;

  // numa correct
  if (numa_node >= 0) {  // use negative numa node to always succeed
    int rnode = ((int)mi_atomic_read_relaxed(&region->numa_node)) - 1;
    if (rnode >= 0 && rnode != numa_node) return false;
  }

  // check allow-large
  bool is_large;
  bool is_committed;
  mi_region_info_read(info, &is_large, &is_committed);  
  if (!allow_large && is_large) return false;

  return true;
}


static bool mi_region_try_claim(size_t blocks, bool allow_large, mem_region_t** region, mi_bitmap_index_t* bit_idx, mi_os_tld_t* tld)
{
  // try all regions for a free slot
  const int numa_node = (_mi_os_numa_node_count() <= 1 ? -1 : _mi_os_numa_node(tld));
  const size_t count = mi_atomic_read(&regions_count);
  size_t idx = tld->region_idx; // Or start at 0 to reuse low addresses? 
  for (size_t visited = 0; visited < count; visited++, idx++) {
    if (idx >= count) idx = 0;  // wrap around
    mem_region_t* r = &regions[idx];
    if (mi_region_is_suitable(r, numa_node, allow_large)) {
      if (mi_bitmap_try_claim_field(&r->in_use, 0, blocks, bit_idx)) {
        tld->region_idx = idx;    // remember the last found position
        *region = r;
        return true;
      }
    }
  }
  return false;
}


static void* mi_region_try_alloc(size_t blocks, bool* commit, bool* is_large, bool* is_zero, size_t* memid, mi_os_tld_t* tld)
{
  mi_assert_internal(blocks <= MI_BITMAP_FIELD_BITS);
  mem_region_t* region;
  mi_bitmap_index_t bit_idx;
  // first try to claim in existing regions
  if (!mi_region_try_claim(blocks, *is_large, &region, &bit_idx, tld)) {
    // otherwise try to allocate a fresh region
    if (!mi_region_try_alloc_os(blocks, *commit, *is_large, &region, &bit_idx, tld)) {
      // out of regions or memory
      return NULL;
    }
  }
  
  // found a region and claimed `blocks` at `bit_idx`
  mi_assert_internal(region != NULL);
  mi_assert_internal(mi_bitmap_is_claimed(&region->in_use, 1, blocks, bit_idx));

  mi_region_info_t info = mi_atomic_read(&region->info);
  bool region_is_committed = false;
  bool region_is_large = false;
  void* start = mi_region_info_read(info, &region_is_large, &region_is_committed);
  mi_assert_internal(!(region_is_large && !*is_large));
  mi_assert_internal(start != NULL);

  *is_zero = mi_bitmap_claim(&region->dirty, 1, blocks, bit_idx, NULL);  
  *is_large = region_is_large;
  *memid = mi_memid_create(region, bit_idx);
  void* p = (uint8_t*)start + (mi_bitmap_index_bit_in_field(bit_idx) * MI_SEGMENT_SIZE);
  if (region_is_committed) {
    // always committed
    *commit = true;
  }
  else if (*commit) {
    // ensure commit
    bool any_zero;
    mi_bitmap_claim(&region->commit, 1, blocks, bit_idx, &any_zero);
    if (any_zero) {
      bool commit_zero;
      _mi_mem_commit(p, blocks * MI_SEGMENT_SIZE, &commit_zero, tld);
      if (commit_zero) *is_zero = true;
    }
  }
  else {
    // no need to commit, but check if already fully committed
    *commit = mi_bitmap_is_claimed(&region->commit, 1, blocks, bit_idx);
  }  
  
  // and return the allocation  
  mi_assert_internal(p != NULL);  
  return p;
}


/* ----------------------------------------------------------------------------
 Allocation
-----------------------------------------------------------------------------*/

// Allocate `size` memory aligned at `alignment`. Return non NULL on success, with a given memory `id`.
// (`id` is abstract, but `id = idx*MI_REGION_MAP_BITS + bitidx`)
void* _mi_mem_alloc_aligned(size_t size, size_t alignment, bool* commit, bool* large, bool* is_zero, size_t* memid, mi_os_tld_t* tld)
{
  mi_assert_internal(memid != NULL && tld != NULL);
  mi_assert_internal(size > 0);
  *memid = 0;
  *is_zero = false;
  bool default_large = false;
  if (large==NULL) large = &default_large;  // ensure `large != NULL`  
  if (size == 0) return NULL;
  size = _mi_align_up(size, _mi_os_page_size());

  // allocate from regions if possible
  size_t arena_memid;
  const size_t blocks = mi_region_block_count(size);
  if (blocks <= MI_REGION_MAX_OBJ_BLOCKS && alignment <= MI_SEGMENT_ALIGN) {
    void* p = mi_region_try_alloc(blocks, commit, large, is_zero, memid, tld);
    mi_assert_internal(p == NULL || (uintptr_t)p % alignment == 0);    
    if (p != NULL) {
      if (*commit) { ((uint8_t*)p)[0] = 0; }
      return p;
    }
    _mi_warning_message("unable to allocate from region: size %zu\n", size);
  }

  // and otherwise fall back to the OS
  void* p = _mi_arena_alloc_aligned(size, alignment, commit, large, is_zero, &arena_memid, tld);
  *memid = mi_memid_create_from_arena(arena_memid);
  mi_assert_internal( p == NULL || (uintptr_t)p % alignment == 0);
  if (p != NULL && *commit) { ((uint8_t*)p)[0] = 0; }
  return p;
}



/* ----------------------------------------------------------------------------
Free
-----------------------------------------------------------------------------*/

// Free previously allocated memory with a given id.
void _mi_mem_free(void* p, size_t size, size_t id, mi_os_tld_t* tld) {
  mi_assert_internal(size > 0 && tld != NULL);
  if (p==NULL) return;
  if (size==0) return;

  mi_delay_remove(tld->reset_delay, p, size);

  size_t arena_memid = 0;
  mi_bitmap_index_t bit_idx;
  mem_region_t* region;
  if (mi_memid_indices(id,&region,&bit_idx,&arena_memid)) {
   // was a direct arena allocation, pass through
    _mi_arena_free(p, size, arena_memid, tld->stats);
  }
  else {
    // allocated in a region
    mi_assert_internal(size <= MI_REGION_MAX_OBJ_SIZE); if (size > MI_REGION_MAX_OBJ_SIZE) return;
    // we can align the size up to page size (as we allocate that way too)
    // this ensures we fully commit/decommit/reset
    size = _mi_align_up(size, _mi_os_page_size());
    const size_t blocks = mi_region_block_count(size);
    mi_region_info_t info = mi_atomic_read(&region->info);
    bool is_large;
    bool is_committed;
    void* start = mi_region_info_read(info, &is_large, &is_committed);
    mi_assert_internal(start != NULL);
    void* blocks_start = (uint8_t*)start + (bit_idx * MI_SEGMENT_SIZE);
    mi_assert_internal(blocks_start == p); // not a pointer in our area?
    mi_assert_internal(bit_idx + blocks <= MI_BITMAP_FIELD_BITS);
    if (blocks_start != p || bit_idx + blocks > MI_BITMAP_FIELD_BITS) return; // or `abort`?

    // decommit (or reset) the blocks to reduce the working set.
    // TODO: implement delayed decommit/reset as these calls are too expensive
    // if the memory is reused soon.
    // reset: 10x slowdown on malloc-large, decommit: 17x slowdown on malloc-large
    if (!is_large &&
        mi_option_is_enabled(mi_option_segment_reset) &&
        mi_option_is_enabled(mi_option_eager_commit))  // cannot reset halfway committed segments, use `option_page_reset` instead            
    {
      // note: don't use `_mi_mem_reset` as it is shared with other threads!
      _mi_os_reset(p, size, tld->stats);    // TODO: maintain reset bits to unreset  
    }
    if (!is_committed) {
      // adjust commit statistics as we commit again when re-using the same slot
      _mi_stat_decrease(&tld->stats->committed, mi_good_commit_size(size));
    }

    // TODO: should we free empty regions? currently only done _mi_mem_collect.
    // this frees up virtual address space which might be useful on 32-bit systems?

    // and unclaim
    mi_bitmap_unclaim(&region->in_use, 1, blocks, bit_idx);
  }
}


/* ----------------------------------------------------------------------------
  collection
-----------------------------------------------------------------------------*/
void _mi_mem_collect(mi_os_tld_t* tld) {
  // free every region that has no segments in use.
  uintptr_t rcount = mi_atomic_read_relaxed(&regions_count);
  for (size_t i = 0; i < rcount; i++) {
    mem_region_t* region = &regions[i];
    if (mi_atomic_read_relaxed(&region->info) != 0) {
      // if no segments used, try to claim the whole region
      uintptr_t m;
      do {
        m = mi_atomic_read_relaxed(&region->in_use);
      } while(m == 0 && !mi_atomic_cas_weak(&region->in_use, MI_BITMAP_FIELD_FULL, 0 ));
      if (m == 0) {
        // on success, free the whole region
        bool is_eager_committed;
        void* start = mi_region_info_read(mi_atomic_read(&regions[i].info), NULL, &is_eager_committed);
        if (start != NULL) { // && !_mi_os_is_huge_reserved(start)) {
          mi_delay_remove(tld->reset_delay, start, MI_REGION_SIZE);
          _mi_arena_free(start, MI_REGION_SIZE, region->arena_memid, tld->stats);
        }
        // and release
        mi_atomic_write(&region->info,0);
      }
    }
  }
}

/* ----------------------------------------------------------------------------
  Delay slots
-----------------------------------------------------------------------------*/

typedef void (mi_delay_resolve_fun)(void* addr, size_t size, void* arg);

static void mi_delay_insert(mi_delay_slots_t* ds,
  mi_msecs_t delay, uint8_t* addr, size_t size,
  mi_delay_resolve_fun* resolve, void* arg)
{
  if (ds == NULL || delay==0 || addr==NULL || size==0) {
    resolve(addr, size, arg);
    return;
  }

  mi_msecs_t now = _mi_clock_now();
  mi_delay_slot_t* oldest = &ds->slots[0];
  // walk through all slots, resolving expired ones.
  // remember the oldest slot to insert the new entry in.
  size_t newcount = 0;
  for (size_t i = 0; i < ds->count; i++) {
    mi_delay_slot_t* slot = &ds->slots[i];
    
    if (slot->expire == 0) {
      // empty slot
      oldest = slot;
    }
    // TODO: should we handle overlapping areas too?
    else if (slot->addr <= addr && slot->addr + slot->size >= addr + size) {
      // earlier slot encompasses new area, increase expiration
      slot->expire = now + delay;
      delay = 0; 
    }
    else if (addr <= slot->addr && addr + size >= slot->addr + slot->size) {
      // new one encompasses old slot, overwrite
      slot->expire = now + delay;
      slot->addr = addr;
      slot->size = size;
      delay = 0;
    }
    else if (slot->expire < now) {
      // expired slot, resolve now
      slot->expire = 0;
      resolve(slot->addr, slot->size, arg);
    }
    else if (oldest->expire > slot->expire) {  
      oldest = slot;
      newcount = i+1;
    }
    else {
      newcount = i+1;
    }
  }
  ds->count = newcount;
  if (delay>0) {
    // not yet registered, use the oldest slot (or a new one if there is space)
    if (ds->count < ds->capacity) {
      oldest = &ds->slots[ds->count];
      ds->count++;
    }
    else if (oldest->expire > 0) { 
      resolve(oldest->addr, oldest->size, arg);  // evict if not empty
    }
    mi_assert_internal((oldest - ds->slots) < (ptrdiff_t)ds->count);
    oldest->expire = now + delay;
    oldest->addr = addr;
    oldest->size = size;
  }
}

static bool mi_delay_remove(mi_delay_slots_t* ds, void* p, size_t size)
{
  if (ds == NULL || p==NULL || size==0) return false; 
  
  uint8_t* addr = (uint8_t*)p;
  bool done = false;
  size_t newcount = 0;
  
  // walk through all valid slots
  for (size_t i = 0; i < ds->count; i++) {
    mi_delay_slot_t* slot = &ds->slots[i];
    if (slot->addr <= addr && slot->addr + slot->size >= addr + size) {
      // earlier slot encompasses the area; remove it
      slot->expire = 0;
      done = true;
    }
    else if (addr <= slot->addr && addr + size >= slot->addr + slot->size) {
      // new one encompasses old slot, remove it
      slot->expire = 0;
    }
    else if ((addr <= slot->addr && addr + size > slot->addr) ||
      (addr < slot->addr + slot->size && addr + size >= slot->addr + slot->size)) {
      // partial overlap
      // can happen with a large object spanning onto some partial end block
      // mi_assert_internal(false);
      slot->expire = 0;
    }
    else {
      newcount = i + 1;
    }
  }
  ds->count = newcount;
  return done;
}

static void mi_resolve_reset(void* p, size_t size, void* vtld) {
  mi_os_tld_t* tld = (mi_os_tld_t*)vtld;
  _mi_os_reset(p, size, tld->stats);
}

bool _mi_mem_reset(void* p, size_t size, mi_os_tld_t* tld) {
  mi_delay_insert(tld->reset_delay, mi_option_get(mi_option_reset_delay),
    (uint8_t*)p, size, &mi_resolve_reset, tld);
  return true;
}

bool _mi_mem_unreset(void* p, size_t size, bool* is_zero, mi_os_tld_t* tld) {
  if (!mi_delay_remove(tld->reset_delay, (uint8_t*)p, size)) {
    return _mi_os_unreset(p, size, is_zero, tld->stats);
  }
  return true;
}



/* ----------------------------------------------------------------------------
  Other
-----------------------------------------------------------------------------*/

bool _mi_mem_commit(void* p, size_t size, bool* is_zero, mi_os_tld_t* tld) {
  mi_delay_remove(tld->reset_delay,p, size);
  return _mi_os_commit(p, size, is_zero, tld->stats);
}

bool _mi_mem_decommit(void* p, size_t size, mi_os_tld_t* tld) {
  mi_delay_remove(tld->reset_delay, p, size);
  return _mi_os_decommit(p, size, tld->stats);
}

bool _mi_mem_protect(void* p, size_t size) {
  return _mi_os_protect(p, size);
}

bool _mi_mem_unprotect(void* p, size_t size) {
  return _mi_os_unprotect(p, size);
}
