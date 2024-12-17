/* ----------------------------------------------------------------------------
Copyright (c) 2019-2024, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
"Arenas" are fixed area's of OS memory from which we can allocate
large blocks (>= MI_ARENA_MIN_BLOCK_SIZE, 4MiB).
In contrast to the rest of mimalloc, the arenas are shared between
threads and need to be accessed using atomic operations.

Arenas are also used to for huge OS page (1GiB) reservations or for reserving
OS memory upfront which can be improve performance or is sometimes needed
on embedded devices. We can also employ this with WASI or `sbrk` systems
to reserve large arenas upfront and be able to reuse the memory more effectively.

The arena allocation needs to be thread safe and we use an atomic bitmap to allocate.
-----------------------------------------------------------------------------*/

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "bitmap.h"


/* -----------------------------------------------------------
  Arena allocation
----------------------------------------------------------- */

#define MI_ARENA_BIN_COUNT      (MI_BIN_COUNT)
#define MI_ARENA_MIN_SIZE       (MI_BCHUNK_BITS * MI_ARENA_SLICE_SIZE)           // 32 MiB (or 8 MiB on 32-bit)
#define MI_ARENA_MAX_SIZE       (MI_BITMAP_MAX_BIT_COUNT * MI_ARENA_SLICE_SIZE)

// A memory arena descriptor
typedef struct mi_arena_s {
  mi_memid_t          memid;                // memid of the memory area
  mi_arena_id_t       id;                   // arena id (> 0 where `arena == arenas[arena->id - 1]`)

  size_t              slice_count;          // total size of the area in arena slices (of `MI_ARENA_SLICE_SIZE`)
  size_t              info_slices;          // initial slices reserved for the arena bitmaps
  int                 numa_node;            // associated NUMA node
  bool                is_exclusive;         // only allow allocations if specifically for this arena
  bool                is_large;             // memory area consists of large- or huge OS pages (always committed)
  _Atomic(mi_msecs_t) purge_expire;         // expiration time when slices can be purged from `slices_purge`.
  _Atomic(mi_msecs_t) purge_expire_extend;  // the purge expiration may be extended by a bit

  mi_bitmap_t*        slices_free;          // is the slice free?
  mi_bitmap_t*        slices_committed;     // is the slice committed? (i.e. accessible)
  mi_bitmap_t*        slices_dirty;         // is the slice potentially non-zero?
  mi_bitmap_t*        slices_purge;         // slices that can be purged
  mi_bitmap_t*        pages;                // all registered pages (abandoned and owned)
  mi_bitmap_t*        pages_abandoned[MI_BIN_COUNT];  // abandoned pages per size bin (a set bit means the start of the page)
                                            // the full queue contains abandoned full pages
  // followed by the bitmaps (whose sizes depend on the arena size)
  // note: when adding bitmaps revise `mi_arena_info_slices_needed`
} mi_arena_t;

// Every "page" in `pages_purge` points to purge info 
// (since we use it for any free'd range and not just for pages)
typedef struct mi_purge_info_s {
  _Atomic(mi_msecs_t)  expire;
  _Atomic(size_t)      slice_count;
} mi_purge_info_t;


#define MI_MAX_ARENAS         (160)         // Limited for now (and takes up .bss).. but arena's scale up exponentially (see `mi_arena_reserve`)
                                            // 160 arenas is enough for ~2 TiB memory

// The available arenas
static mi_decl_cache_align _Atomic(mi_arena_t*) mi_arenas[MI_MAX_ARENAS];
static mi_decl_cache_align _Atomic(size_t)      mi_arena_count; // = 0


static mi_lock_t mi_arena_reserve_lock;

void _mi_arena_init(void) {
  mi_lock_init(&mi_arena_reserve_lock);
}

/* -----------------------------------------------------------
  Arena id's
  id = arena_index + 1
----------------------------------------------------------- */

size_t mi_arena_id_index(mi_arena_id_t id) {
  return (size_t)(id <= 0 ? MI_MAX_ARENAS : id - 1);
}

static mi_arena_id_t mi_arena_id_create(size_t arena_index) {
  mi_assert_internal(arena_index < MI_MAX_ARENAS);
  return (int)arena_index + 1;
}

mi_arena_id_t _mi_arena_id_none(void) {
  return 0;
}

static bool mi_arena_id_is_suitable(mi_arena_id_t arena_id, bool arena_is_exclusive, mi_arena_id_t req_arena_id) {
  return ((!arena_is_exclusive && req_arena_id == _mi_arena_id_none()) ||
          (arena_id == req_arena_id));
}

bool _mi_arena_memid_is_suitable(mi_memid_t memid, mi_arena_id_t request_arena_id) {
  if (memid.memkind == MI_MEM_ARENA) {
    const mi_arena_t* arena = memid.mem.arena.arena;
    return mi_arena_id_is_suitable(arena->id, arena->is_exclusive, request_arena_id);
  }
  else {
    return mi_arena_id_is_suitable(_mi_arena_id_none(), false, request_arena_id);
  }
}

size_t mi_arena_get_count(void) {
  return mi_atomic_load_relaxed(&mi_arena_count);
}

mi_arena_t* mi_arena_from_index(size_t idx) {
  mi_assert_internal(idx < mi_arena_get_count());
  return mi_atomic_load_ptr_relaxed(mi_arena_t, &mi_arenas[idx]);
}

mi_arena_t* mi_arena_from_id(mi_arena_id_t id) {
  return mi_arena_from_index(mi_arena_id_index(id));
}

static size_t mi_arena_info_slices(mi_arena_t* arena) {
  return arena->info_slices;
}

#if MI_DEBUG > 1
static bool mi_arena_has_page(mi_arena_t* arena, mi_page_t* page) {
  return (page->memid.memkind == MI_MEM_ARENA &&
          page->memid.mem.arena.arena == arena &&
          mi_bitmap_is_setN(arena->pages, page->memid.mem.arena.slice_index, 1));
}
#endif

/* -----------------------------------------------------------
  Util
----------------------------------------------------------- */


// Size of an arena
static size_t mi_arena_size(mi_arena_t* arena) {
  return mi_size_of_slices(arena->slice_count);
}

// Start of the arena memory area
static uint8_t* mi_arena_start(mi_arena_t* arena) {
  return ((uint8_t*)arena);
}

// Start of a slice
uint8_t* mi_arena_slice_start(mi_arena_t* arena, size_t slice_index) {
  return (mi_arena_start(arena) + mi_size_of_slices(slice_index));
}

// Arena area
void* mi_arena_area(mi_arena_id_t arena_id, size_t* size) {
  if (size != NULL) *size = 0;
  const size_t arena_index = mi_arena_id_index(arena_id);
  if (arena_index >= MI_MAX_ARENAS) return NULL;
  mi_arena_t* arena = mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[arena_index]);
  if (arena == NULL) return NULL;
  if (size != NULL) { *size = mi_size_of_slices(arena->slice_count); }
  return mi_arena_start(arena);
}


// Create an arena memid
static mi_memid_t mi_memid_create_arena(mi_arena_t* arena, size_t slice_index, size_t slice_count) {
  mi_assert_internal(slice_index < UINT32_MAX);
  mi_assert_internal(slice_count < UINT32_MAX);
  mi_assert_internal(slice_count > 0);
  mi_assert_internal(slice_index < arena->slice_count);
  mi_memid_t memid = _mi_memid_create(MI_MEM_ARENA);
  memid.mem.arena.arena = arena;
  memid.mem.arena.slice_index = (uint32_t)slice_index;
  memid.mem.arena.slice_count = (uint32_t)slice_count;
  return memid;
}

// get the arena and slice span
static mi_arena_t* mi_arena_from_memid(mi_memid_t memid, size_t* slice_index, size_t* slice_count) {
  mi_assert_internal(memid.memkind == MI_MEM_ARENA);
  mi_arena_t* arena = memid.mem.arena.arena;
  if (slice_index) *slice_index = memid.mem.arena.slice_index;
  if (slice_count) *slice_count = memid.mem.arena.slice_count;
  return arena;
}

static mi_arena_t* mi_page_arena(mi_page_t* page, size_t* slice_index, size_t* slice_count) {
  // todo: maybe store the arena* directly in the page?
  return mi_arena_from_memid(page->memid, slice_index, slice_count);
}

static size_t mi_memid_size(mi_memid_t memid) {
  if (memid.memkind == MI_MEM_ARENA) {
    return memid.mem.arena.slice_count * MI_ARENA_SLICE_SIZE;
  }
  else if (mi_memid_is_os(memid) || memid.memkind == MI_MEM_EXTERNAL) {
    return memid.mem.os.size;
  }
  else {
    return 0;
  }
}

/* -----------------------------------------------------------
  Arena Allocation
----------------------------------------------------------- */

static mi_decl_noinline void* mi_arena_try_alloc_at(
  mi_arena_t* arena, size_t slice_count, bool commit, size_t tseq, mi_memid_t* memid)
{
  size_t slice_index;
  if (!mi_bitmap_try_find_and_clearN(arena->slices_free, slice_count, tseq, &slice_index)) return NULL;

  // claimed it!
  void* p = mi_arena_slice_start(arena, slice_index);
  *memid = mi_memid_create_arena(arena, slice_index, slice_count);
  memid->is_pinned = arena->memid.is_pinned;

  // set the dirty bits
  if (arena->memid.initially_zero) {
    memid->initially_zero = mi_bitmap_setN(arena->slices_dirty, slice_index, slice_count, NULL);
  }

  // set commit state
  if (commit) {
    memid->initially_committed = true;

    // commit requested, but the range may not be committed as a whole: ensure it is committed now
    if (!mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count)) {
      // not fully committed: commit the full range and set the commit bits
      // (this may race and we may double-commit which is fine)
      bool commit_zero = false;
      if (!_mi_os_commit(p, mi_size_of_slices(slice_count), &commit_zero)) {
        memid->initially_committed = false;
      }
      else {
        if (commit_zero) { memid->initially_zero = true; }
        #if MI_DEBUG > 1
        if (memid->initially_zero) {
          if (!mi_mem_is_zero(p, mi_size_of_slices(slice_count))) {
            _mi_error_message(EFAULT, "interal error: arena allocation was not zero-initialized!\n");
            memid->initially_zero = false;
          }
        }
        #endif
        size_t already_committed_count = 0;
        mi_bitmap_setN(arena->slices_committed, slice_index, slice_count, &already_committed_count);
        if (already_committed_count < slice_count) {
          // todo: also decrease total
          mi_stat_decrease(_mi_stats_main.committed, mi_size_of_slices(already_committed_count));
        }
      }
    }
    if (memid->initially_zero) {
      mi_track_mem_defined(p, slice_count * MI_ARENA_SLICE_SIZE);
    }
    else {
      mi_track_mem_undefined(p, slice_count * MI_ARENA_SLICE_SIZE);
    }
  }
  else {
    // no need to commit, but check if already fully committed
    memid->initially_committed = mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count);
  }

  mi_assert_internal(mi_bitmap_is_clearN(arena->slices_free, slice_index, slice_count));
  if (commit) { mi_assert_internal(mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count)); }
  mi_assert_internal(mi_bitmap_is_setN(arena->slices_dirty, slice_index, slice_count));
  
  return p;
}


// try to reserve a fresh arena space
static bool mi_arena_reserve(size_t req_size, bool allow_large, mi_arena_id_t req_arena_id, mi_arena_id_t* arena_id)
{
  // if (_mi_preloading()) return false;  // use OS only while pre loading
  if (req_arena_id != _mi_arena_id_none()) return false;

  const size_t arena_count = mi_atomic_load_acquire(&mi_arena_count);
  if (arena_count > (MI_MAX_ARENAS - 4)) return false;

  // calc reserve
  size_t arena_reserve = mi_option_get_size(mi_option_arena_reserve);
  if (arena_reserve == 0) return false;

  if (!_mi_os_has_virtual_reserve()) {
    arena_reserve = arena_reserve/4;  // be conservative if virtual reserve is not supported (for WASM for example)
  }
  arena_reserve = _mi_align_up(arena_reserve, MI_ARENA_SLICE_SIZE);

  if (arena_count >= 1 && arena_count <= 128) {
    // scale up the arena sizes exponentially every 4 entries
    const size_t multiplier = (size_t)1 << _mi_clamp(arena_count/4, 0, 16);
    size_t reserve = 0;
    if (!mi_mul_overflow(multiplier, arena_reserve, &reserve)) {
      arena_reserve = reserve;
    }
  }

  // check arena bounds
  const size_t min_reserve = MI_ARENA_MIN_SIZE;
  const size_t max_reserve = MI_ARENA_MAX_SIZE;   // 16 GiB
  if (arena_reserve < min_reserve) {
    arena_reserve = min_reserve;
  }
  else if (arena_reserve > max_reserve) {
    arena_reserve = max_reserve;
  }

  if (arena_reserve < req_size) return false;  // should be able to at least handle the current allocation size

  // commit eagerly?
  bool arena_commit = false;
  if (mi_option_get(mi_option_arena_eager_commit) == 2) { arena_commit = _mi_os_has_overcommit(); }
  else if (mi_option_get(mi_option_arena_eager_commit) == 1) { arena_commit = true; }

  // and try to reserve the arena
  int err = mi_reserve_os_memory_ex(arena_reserve, arena_commit, allow_large, false /* exclusive? */, arena_id);
  if (err != 0) {
    // failed, try a smaller size?
    const size_t small_arena_reserve = (MI_SIZE_BITS == 32 ? 128*MI_MiB : 1*MI_GiB);
    if (arena_reserve > small_arena_reserve) {
      // try again
      err = mi_reserve_os_memory_ex(small_arena_reserve, arena_commit, allow_large, false /* exclusive? */, arena_id);
    }
  }
  return (err==0);
}




/* -----------------------------------------------------------
  Arena iteration
----------------------------------------------------------- */

static inline bool mi_arena_is_suitable(mi_arena_t* arena, mi_arena_id_t req_arena_id, int numa_node, bool allow_large) {
  if (!allow_large && arena->is_large) return false;
  if (!mi_arena_id_is_suitable(arena->id, arena->is_exclusive, req_arena_id)) return false;
  if (req_arena_id == _mi_arena_id_none()) { // if not specific, check numa affinity
    const bool numa_suitable = (numa_node < 0 || arena->numa_node < 0 || arena->numa_node == numa_node);
    if (!numa_suitable) return false;
  }
  return true;
}


#define mi_forall_arenas(req_arena_id, tseq, name_arena) \
  { \
  const size_t _arena_count = mi_arena_get_count(); \
  if (_arena_count > 0) { \
    const size_t _arena_cycle = _arena_count - 1; /* first search the arenas below the last one */ \
    size_t _start; \
    if (req_arena_id == _mi_arena_id_none()) { \
       /* always start searching in the arena's below the max */ \
      _start = (_arena_cycle <= 1 ? 0 : (tseq % _arena_cycle)); \
    } \
    else { \
      _start = mi_arena_id_index(req_arena_id); \
      mi_assert_internal(_start < _arena_count); \
    } \
    for (size_t _i = 0; _i < _arena_count; _i++) { \
      size_t _idx; \
      if (_i < _arena_cycle) { \
        _idx = _i + _start; \
        if (_idx >= _arena_cycle) { _idx -= _arena_cycle; } /* adjust so we rotate through the cycle */ \
      } \
      else { \
        _idx = _i; /* remaining arena's */ \
      } \
      mi_arena_t* const name_arena = mi_arena_from_index(_idx); \
      if (name_arena != NULL) \
      {

#define mi_forall_arenas_end()  \
      } \
      if (req_arena_id != _mi_arena_id_none()) break; \
    } \
  }}

#define mi_forall_suitable_arenas(req_arena_id, tseq, allow_large, name_arena) \
  mi_forall_arenas(req_arena_id,tseq,name_arena) { \
    if (mi_arena_is_suitable(name_arena, req_arena_id, -1 /* todo: numa node */, allow_large)) { \

#define mi_forall_suitable_arenas_end() \
  }} \
  mi_forall_arenas_end()

/* -----------------------------------------------------------
  Arena allocation
----------------------------------------------------------- */

// allocate slices from the arenas
static mi_decl_noinline void* mi_arena_try_find_free(
  size_t slice_count, size_t alignment,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, size_t tseq, mi_memid_t* memid)
{
  mi_assert_internal(slice_count <= mi_slice_count_of_size(MI_ARENA_MAX_OBJ_SIZE));
  mi_assert(alignment <= MI_ARENA_SLICE_ALIGN);
  if (alignment > MI_ARENA_SLICE_ALIGN) return NULL;

  // search arena's
  mi_forall_suitable_arenas(req_arena_id, tseq, allow_large, arena)
  {
    void* p = mi_arena_try_alloc_at(arena, slice_count, commit, tseq, memid);
    if (p != NULL) return p;
  }
  mi_forall_suitable_arenas_end();
  return NULL;
}

// Allocate slices from the arena's -- potentially allocating a fresh arena
static mi_decl_noinline void* mi_arena_try_alloc(
  size_t slice_count, size_t alignment,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, size_t tseq, mi_memid_t* memid)
{
  mi_assert(slice_count <= MI_ARENA_MAX_OBJ_SLICES);
  mi_assert(alignment <= MI_ARENA_SLICE_ALIGN);
  void* p;
again:
  // try to find free slices in the arena's
  p = mi_arena_try_find_free(slice_count, alignment, commit, allow_large, req_arena_id, tseq, memid);
  if (p != NULL) return p;

  // did we need a specific arena?
  if (req_arena_id != _mi_arena_id_none()) return NULL;

  // don't create arena's while preloading (todo: or should we?)
  if (_mi_preloading()) return NULL;

  // otherwise, try to reserve a new arena -- but one thread at a time.. (todo: allow 2 or 4 to reduce contention?)
  if (mi_lock_try_acquire(&mi_arena_reserve_lock)) {
    mi_arena_id_t arena_id = 0;
    bool ok = mi_arena_reserve(mi_size_of_slices(slice_count), allow_large, req_arena_id, &arena_id);
    mi_lock_release(&mi_arena_reserve_lock);
    if (ok) {
      // and try allocate in there
      mi_assert_internal(req_arena_id == _mi_arena_id_none());
      p = mi_arena_try_find_free(slice_count, alignment, commit, allow_large, req_arena_id, tseq, memid);
      if (p != NULL) return p;
    }
  }
  else {
    // if we are racing with another thread wait until the new arena is reserved (todo: a better yield?)
    mi_atomic_yield();
    goto again;
  }

  return NULL;
}

// Allocate from the OS (if allowed)
static void* mi_arena_os_alloc_aligned(
  size_t size, size_t alignment, size_t align_offset,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, mi_memid_t* memid)
{
  // if we cannot use OS allocation, return NULL
  if (mi_option_is_enabled(mi_option_disallow_os_alloc) || req_arena_id != _mi_arena_id_none()) {
    errno = ENOMEM;
    return NULL;
  }

  if (align_offset > 0) {
    return _mi_os_alloc_aligned_at_offset(size, alignment, align_offset, commit, allow_large, memid);
  }
  else {
    return _mi_os_alloc_aligned(size, alignment, commit, allow_large, memid);
  }
}


// Allocate large sized memory
void* _mi_arena_alloc_aligned(
  size_t size, size_t alignment, size_t align_offset,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, size_t tseq, mi_memid_t* memid)
{
  mi_assert_internal(memid != NULL);
  mi_assert_internal(size > 0);

  // *memid = _mi_memid_none();
  // const int numa_node = _mi_os_numa_node(&tld->os); // current numa node

  // try to allocate in an arena if the alignment is small enough and the object is not too small (as for heap meta data)
  if (!mi_option_is_enabled(mi_option_disallow_arena_alloc) && // is arena allocation allowed?
      req_arena_id == _mi_arena_id_none() &&                   // not a specific arena?
      size >= MI_ARENA_MIN_OBJ_SIZE && size <= MI_ARENA_MAX_OBJ_SIZE &&  // and not too small/large
      alignment <= MI_ARENA_SLICE_ALIGN && align_offset == 0)            // and good alignment
  {
    const size_t slice_count = mi_slice_count_of_size(size);
    void* p = mi_arena_try_alloc(slice_count, alignment, commit, allow_large, req_arena_id, tseq, memid);
    if (p != NULL) return p;
  }

  // fall back to the OS
  void* p = mi_arena_os_alloc_aligned(size, alignment, align_offset, commit, allow_large, req_arena_id, memid);
  return p;
}

void* _mi_arena_alloc(size_t size, bool commit, bool allow_large, mi_arena_id_t req_arena_id, size_t tseq, mi_memid_t* memid)
{
  return _mi_arena_alloc_aligned(size, MI_ARENA_SLICE_SIZE, 0, commit, allow_large, req_arena_id, tseq, memid);
}



/* -----------------------------------------------------------
  Arena page allocation
----------------------------------------------------------- */

static bool mi_arena_try_claim_abandoned(size_t slice_index, mi_arena_t* arena, mi_subproc_t* subproc, mi_heaptag_t heap_tag, bool* keep_abandoned) {
  // found an abandoned page of the right size
  mi_page_t* const page  = (mi_page_t*)mi_arena_slice_start(arena, slice_index);
  // can we claim ownership?
  if (!mi_page_try_claim_ownership(page)) {
    // there was a concurrent free ..
    // we need to keep it in the abandoned map as the free will call `mi_arena_page_unabandon`,
    // and wait for readers (us!) to finish. This is why it is very important to set the abandoned
    // bit again (or otherwise the unabandon will never stop waiting).
    *keep_abandoned = true;
    return false;
  }
  if (subproc != page->subproc || heap_tag != page->heap_tag) {
    // wrong sub-process or heap_tag.. we need to unown again
    // note: this normally never happens unless subprocesses/heaptags are actually used.
    // (an unown might free the page, and depending on that we can keep it in the abandoned map or not)
    // note: a minor wrinkle: the page will still be mapped but the abandoned map entry is (temporarily) clear at this point.
    //       so we cannot check in `mi_arena_free` for this invariant to hold.
    const bool freed = _mi_page_unown(page);
    *keep_abandoned = !freed;
    return false;
  }
  // yes, we can reclaim it, keep the abandaned map entry clear
  *keep_abandoned = false;
  return true;
}

static mi_page_t* mi_arena_page_try_find_abandoned(size_t slice_count, size_t block_size, mi_arena_id_t req_arena_id, mi_heaptag_t heaptag, mi_tld_t* tld)
{
  MI_UNUSED(slice_count);
  const size_t bin = _mi_bin(block_size);
  mi_assert_internal(bin < MI_BIN_COUNT);

  // any abandoned in our size class?
  mi_subproc_t* const subproc = tld->subproc;
  mi_assert_internal(subproc != NULL);
  if (mi_atomic_load_relaxed(&subproc->abandoned_count[bin]) == 0) return NULL;

  // search arena's
  const bool allow_large = true;
  size_t tseq = tld->tseq;
  mi_forall_suitable_arenas(req_arena_id, tseq, allow_large, arena)
  {
    size_t slice_index;
    mi_bitmap_t* const bitmap = arena->pages_abandoned[bin];

    if (mi_bitmap_try_find_and_claim(bitmap, tseq, &slice_index, &mi_arena_try_claim_abandoned, arena, subproc, heaptag)) {
      // found an abandoned page of the right size
      // and claimed ownership.
      mi_page_t* page = (mi_page_t*)mi_arena_slice_start(arena, slice_index);
      mi_assert_internal(mi_page_is_owned(page));
      mi_assert_internal(mi_page_is_abandoned(page));
      mi_assert_internal(mi_arena_has_page(arena,page));
      mi_atomic_decrement_relaxed(&subproc->abandoned_count[bin]);
      _mi_stat_decrease(&_mi_stats_main.pages_abandoned, 1);
      _mi_stat_counter_increase(&_mi_stats_main.pages_reclaim_on_alloc, 1);

      _mi_page_free_collect(page, false);  // update `used` count
      mi_assert_internal(mi_bitmap_is_clearN(arena->slices_free, slice_index, slice_count));
      mi_assert_internal(mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count));
      mi_assert_internal(mi_bitmap_is_setN(arena->slices_dirty, slice_index, slice_count));
      mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
      mi_assert_internal(_mi_ptr_page(page)==page);
      mi_assert_internal(_mi_ptr_page(mi_page_start(page))==page);
      mi_assert_internal(mi_page_block_size(page) == block_size);
      mi_assert_internal(!mi_page_is_full(page));
      return page;
    }
  }
  mi_forall_suitable_arenas_end();
  return NULL;
}

static mi_page_t* mi_arena_page_alloc_fresh(size_t slice_count, size_t block_size, size_t block_alignment,
                                            mi_arena_id_t req_arena_id, mi_tld_t* tld)
{
  const bool allow_large = true;
  const bool commit = true;
  const bool os_align = (block_alignment > MI_PAGE_MAX_OVERALLOC_ALIGN);
  const size_t page_alignment = MI_ARENA_SLICE_ALIGN;

  // try to allocate from free space in arena's
  mi_memid_t memid = _mi_memid_none();
  mi_page_t* page = NULL;
  if (!mi_option_is_enabled(mi_option_disallow_arena_alloc) && // allowed to allocate from arena's?
      !os_align &&                            // not large alignment
      slice_count <= MI_ARENA_MAX_OBJ_SLICES) // and not too large
  {
    page = (mi_page_t*)mi_arena_try_alloc(slice_count, page_alignment, commit, allow_large, req_arena_id, tld->tseq, &memid);
    if (page != NULL) {
      mi_assert_internal(mi_bitmap_is_clearN(memid.mem.arena.arena->pages, memid.mem.arena.slice_index, memid.mem.arena.slice_count));
      mi_bitmap_set(memid.mem.arena.arena->pages, memid.mem.arena.slice_index);
    }
  }

  // otherwise fall back to the OS
  if (page == NULL) {
    if (os_align) {
      // note: slice_count already includes the page
      mi_assert_internal(slice_count >= mi_slice_count_of_size(block_size) + mi_slice_count_of_size(page_alignment));
      page = (mi_page_t*)mi_arena_os_alloc_aligned(mi_size_of_slices(slice_count), block_alignment, page_alignment /* align offset */, commit, allow_large, req_arena_id, &memid);
    }
    else {
      page = (mi_page_t*)mi_arena_os_alloc_aligned(mi_size_of_slices(slice_count), page_alignment, 0 /* align offset */, commit, allow_large, req_arena_id, &memid);
    }
  }

  if (page == NULL) return NULL;
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(!os_align || _mi_is_aligned((uint8_t*)page + page_alignment, block_alignment));

  // claimed free slices: initialize the page partly
  if (!memid.initially_zero) {
    mi_track_mem_undefined(page, slice_count * MI_ARENA_SLICE_SIZE);
    _mi_memzero_aligned(page, sizeof(*page));
  }
  else {
    mi_track_mem_defined(page, slice_count * MI_ARENA_SLICE_SIZE);
  }
  #if MI_DEBUG > 1
  if (memid.initially_zero) {
    if (!mi_mem_is_zero(page, mi_size_of_slices(slice_count))) {
      _mi_error_message(EFAULT, "internal error: page memory was not zero initialized.\n");
      memid.initially_zero = false;
      _mi_memzero_aligned(page, sizeof(*page));
    }
  }
  #endif
  mi_assert(MI_PAGE_INFO_SIZE >= mi_page_info_size());
  size_t block_start;
  #if MI_GUARDED
  // in a guarded build, we align pages with blocks a multiple of an OS page size, to the OS page size
  // this ensures that all blocks in such pages are OS page size aligned (which is needed for the guard pages)
  const size_t os_page_size = _mi_os_page_size();
  mi_assert_internal(MI_PAGE_ALIGN >= os_page_size);
  if (!os_align && block_size % os_page_size == 0 && block_size > os_page_size /* at least 2 or more */ ) {
    block_start = _mi_align_up(mi_page_info_size(), os_page_size);
  }
  else
  #endif
  if (os_align) {
    block_start = MI_PAGE_ALIGN;
  }
  else if (_mi_is_power_of_two(block_size) && block_size <= MI_PAGE_MAX_START_BLOCK_ALIGN2) {
    // naturally align all power-of-2 blocks
    block_start = _mi_align_up(mi_page_info_size(), block_size);
  }
  else {
    // otherwise start after the info
    block_start = mi_page_info_size();
  }
  const size_t reserved    = (os_align ? 1 : (mi_size_of_slices(slice_count) - block_start) / block_size);
  mi_assert_internal(reserved > 0 && reserved <= UINT16_MAX);
  page->reserved = (uint16_t)reserved;
  page->page_start = (uint8_t*)page + block_start;
  page->block_size = block_size;
  page->memid = memid;   
  page->free_is_zero = memid.initially_zero;
  if (block_size > 0 && _mi_is_power_of_two(block_size)) {
    page->block_size_shift = (uint8_t)mi_ctz(block_size);
  }
  else {
    page->block_size_shift = 0;
  }
  _mi_page_map_register(page);
  mi_assert_internal(_mi_ptr_page(page)==page);
  mi_assert_internal(_mi_ptr_page(mi_page_start(page))==page);

  mi_page_try_claim_ownership(page);
  mi_assert_internal(mi_page_block_size(page) == block_size);
  mi_assert_internal(mi_page_is_abandoned(page));
  mi_assert_internal(mi_page_is_owned(page));
  return page;
}

static mi_page_t* mi_arena_page_allocN(mi_heap_t* heap, size_t slice_count, size_t block_size) {
  const mi_arena_id_t  req_arena_id = heap->arena_id;
  mi_tld_t* const tld = heap->tld;

  // 1. look for an abandoned page
  mi_page_t* page = mi_arena_page_try_find_abandoned(slice_count, block_size, req_arena_id, heap->tag, tld);
  if (page != NULL) {
    return page;  // return as abandoned
  }

  // 2. find a free block, potentially allocating a new arena
  page = mi_arena_page_alloc_fresh(slice_count, block_size, 1, req_arena_id, tld);
  if (page != NULL) {
    mi_assert_internal(page->memid.memkind != MI_MEM_ARENA || page->memid.mem.arena.slice_count == slice_count);
    _mi_page_init(heap, page);
    return page;
  }

  return NULL;
}


static mi_page_t* mi_singleton_page_alloc(mi_heap_t* heap, size_t block_size, size_t block_alignment) {
  const mi_arena_id_t  req_arena_id = heap->arena_id;
  mi_tld_t* const tld = heap->tld;
  const bool os_align = (block_alignment > MI_PAGE_MAX_OVERALLOC_ALIGN);
  const size_t info_size = (os_align ? MI_PAGE_ALIGN : mi_page_info_size());
  const size_t slice_count = mi_slice_count_of_size(info_size + block_size);

  mi_page_t* page = mi_arena_page_alloc_fresh(slice_count, block_size, block_alignment, req_arena_id, tld);
  if (page == NULL) return NULL;

  mi_assert(page != NULL);
  mi_assert(page->reserved == 1);
  mi_assert_internal(_mi_ptr_page(page)==page);
  mi_assert_internal(_mi_ptr_page(mi_page_start(page))==page);

  return page;
}


mi_page_t* _mi_arena_page_alloc(mi_heap_t* heap, size_t block_size, size_t block_alignment) {
  mi_page_t* page;
  if mi_unlikely(block_alignment > MI_PAGE_MAX_OVERALLOC_ALIGN) {
    mi_assert_internal(_mi_is_power_of_two(block_alignment));
    page = mi_singleton_page_alloc(heap, block_size, block_alignment);
  }
  else if (block_size <= MI_SMALL_MAX_OBJ_SIZE) {
    page = mi_arena_page_allocN(heap, mi_slice_count_of_size(MI_SMALL_PAGE_SIZE), block_size);
  }
  else if (block_size <= MI_MEDIUM_MAX_OBJ_SIZE) {
    page = mi_arena_page_allocN(heap, mi_slice_count_of_size(MI_MEDIUM_PAGE_SIZE), block_size);
  }
  else if (block_size <= MI_LARGE_MAX_OBJ_SIZE) {
    page = mi_arena_page_allocN(heap, mi_slice_count_of_size(MI_LARGE_PAGE_SIZE), block_size);
  }
  else {
    page = mi_singleton_page_alloc(heap, block_size, block_alignment);
  }
  // mi_assert_internal(page == NULL || _mi_page_segment(page)->subproc == tld->subproc);
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(_mi_ptr_page(page)==page);
  mi_assert_internal(_mi_ptr_page(mi_page_start(page))==page);
  mi_assert_internal(block_alignment <= MI_PAGE_MAX_OVERALLOC_ALIGN || _mi_is_aligned(mi_page_start(page), block_alignment));

  return page;
}

static void mi_arena_free(void* p, size_t size, mi_memid_t memid);

void _mi_arena_page_free(mi_page_t* page) {
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(_mi_ptr_page(page)==page);
  mi_assert_internal(mi_page_is_owned(page));
  mi_assert_internal(mi_page_all_free(page));
  mi_assert_internal(page->next==NULL);

  #if MI_DEBUG>1
  if (page->memid.memkind==MI_MEM_ARENA && !mi_page_is_full(page)) {
    size_t bin = _mi_bin(mi_page_block_size(page));
    size_t slice_index;
    size_t slice_count;
    mi_arena_t* arena = mi_page_arena(page, &slice_index, &slice_count);

    mi_assert_internal(mi_bitmap_is_clearN(arena->slices_free, slice_index, slice_count));
    mi_assert_internal(mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count));
    mi_assert_internal(mi_bitmap_is_clearN(arena->pages_abandoned[bin], slice_index, 1));
    mi_assert_internal(mi_bitmap_is_setN(page->memid.mem.arena.arena->pages, page->memid.mem.arena.slice_index, 1));
    // note: we cannot check for `!mi_page_is_abandoned_and_mapped` since that may
    // be (temporarily) not true if the free happens while trying to reclaim
    // see `mi_arana_try_claim_abandoned`
  }
  #endif

  _mi_page_map_unregister(page);
  if (page->memid.memkind == MI_MEM_ARENA) {
    mi_bitmap_clear(page->memid.mem.arena.arena->pages, page->memid.mem.arena.slice_index);
  }
  mi_arena_free(page, mi_memid_size(page->memid), page->memid);
}

/* -----------------------------------------------------------
  Arena abandon
----------------------------------------------------------- */

static void mi_arena_page_abandon_no_stat(mi_page_t* page) {
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(_mi_ptr_page(page)==page);
  mi_assert_internal(mi_page_is_owned(page));
  mi_assert_internal(mi_page_is_abandoned(page));
  mi_assert_internal(!mi_page_all_free(page));
  mi_assert_internal(page->next==NULL);

  mi_subproc_t* subproc = page->subproc;
  if (page->memid.memkind==MI_MEM_ARENA && !mi_page_is_full(page)) {
    // make available for allocations
    size_t bin = _mi_bin(mi_page_block_size(page));
    size_t slice_index;
    size_t slice_count;
    mi_arena_t* arena = mi_page_arena(page, &slice_index, &slice_count);
    mi_assert_internal(!mi_page_is_singleton(page));
    mi_assert_internal(mi_bitmap_is_clearN(arena->slices_free, slice_index, slice_count));
    mi_assert_internal(mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count));
    mi_assert_internal(mi_bitmap_is_setN(arena->slices_dirty, slice_index, slice_count));

    mi_page_set_abandoned_mapped(page);
    const bool wasclear = mi_bitmap_set(arena->pages_abandoned[bin], slice_index);
    MI_UNUSED(wasclear); mi_assert_internal(wasclear);
    mi_atomic_increment_relaxed(&subproc->abandoned_count[bin]);
  }
  else {
    // page is full (or a singleton), page is OS/externally allocated
    // leave as is; it will be reclaimed when an object is free'd in the page
  }
  _mi_page_unown(page);
}

void _mi_arena_page_abandon(mi_page_t* page) {
  mi_arena_page_abandon_no_stat(page);
  _mi_stat_increase(&_mi_stats_main.pages_abandoned, 1);
}

bool _mi_arena_page_try_reabandon_to_mapped(mi_page_t* page) {
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(_mi_ptr_page(page)==page);
  mi_assert_internal(mi_page_is_owned(page));
  mi_assert_internal(mi_page_is_abandoned(page));
  mi_assert_internal(!mi_page_is_abandoned_mapped(page));
  mi_assert_internal(!mi_page_is_full(page));
  mi_assert_internal(!mi_page_all_free(page));
  mi_assert_internal(!mi_page_is_singleton(page));
  if (mi_page_is_full(page) || mi_page_is_abandoned_mapped(page) || page->memid.memkind != MI_MEM_ARENA) {
    return false;
  }
  else {
    _mi_stat_counter_increase(&_mi_stats_main.pages_reabandon_full, 1);
    mi_arena_page_abandon_no_stat(page);
    return true;
  }
}

// called from `mi_free` if trying to unabandon an abandoned page
void _mi_arena_page_unabandon(mi_page_t* page) {
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(_mi_ptr_page(page)==page);
  mi_assert_internal(mi_page_is_owned(page));
  mi_assert_internal(mi_page_is_abandoned(page));

  if (mi_page_is_abandoned_mapped(page)) {
    mi_assert_internal(page->memid.memkind==MI_MEM_ARENA);
    // remove from the abandoned map
    size_t bin = _mi_bin(mi_page_block_size(page));
    size_t slice_index;
    size_t slice_count;
    mi_arena_t* arena = mi_page_arena(page, &slice_index, &slice_count);

    mi_assert_internal(mi_bitmap_is_clearN(arena->slices_free, slice_index, slice_count));
    mi_assert_internal(mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count));
    
    // this busy waits until a concurrent reader (from alloc_abandoned) is done
    mi_bitmap_clear_once_set(arena->pages_abandoned[bin], slice_index);
    mi_page_clear_abandoned_mapped(page);
    mi_atomic_decrement_relaxed(&page->subproc->abandoned_count[bin]);
  }
  else {
    // page is full (or a singleton), page is OS/nly allocated
    // nothing to do
    // TODO: maintain count of these as well?
  }
  _mi_stat_decrease(&_mi_stats_main.pages_abandoned, 1);
}

void _mi_arena_reclaim_all_abandoned(mi_heap_t* heap) {
  MI_UNUSED(heap);
  // TODO: implement this
  return;
}


/* -----------------------------------------------------------
  Arena free
----------------------------------------------------------- */
static void mi_arena_schedule_purge(mi_arena_t* arena, size_t slice_index, size_t slices);
static void mi_arenas_try_purge(bool force, bool visit_all);

static void mi_arena_free(void* p, size_t size, mi_memid_t memid) {
  if (p==NULL) return;
  if (size==0) return;

  // need to set all memory to undefined as some parts may still be marked as no_access (like padding etc.)
  mi_track_mem_undefined(p, size);

  if (mi_memkind_is_os(memid.memkind)) {
    // was a direct OS allocation, pass through
    _mi_os_free(p, size, memid);
  }
  else if (memid.memkind == MI_MEM_ARENA) {
    // allocated in an arena
    size_t slice_count;
    size_t slice_index;
    mi_arena_t* arena = mi_arena_from_memid(memid, &slice_index, &slice_count);
    mi_assert_internal((size%MI_ARENA_SLICE_SIZE)==0);
    mi_assert_internal((slice_count*MI_ARENA_SLICE_SIZE)==size);
    mi_assert_internal(mi_arena_slice_start(arena,slice_index) <= (uint8_t*)p);
    mi_assert_internal(mi_arena_slice_start(arena,slice_index) + mi_size_of_slices(slice_count) > (uint8_t*)p);
    // checks
    if (arena == NULL) {
      _mi_error_message(EINVAL, "trying to free from an invalid arena: %p, size %zu, memid: 0x%zx\n", p, size, memid);
      return;
    }
    mi_assert_internal(slice_index < arena->slice_count);
    mi_assert_internal(slice_index >= mi_arena_info_slices(arena));
    if (slice_index < mi_arena_info_slices(arena) || slice_index > arena->slice_count) {
      _mi_error_message(EINVAL, "trying to free from an invalid arena block: %p, size %zu, memid: 0x%zx\n", p, size, memid);
      return;
    }

    // potentially decommit
    if (!arena->memid.is_pinned /* && !arena->memid.initially_committed */) { // todo: allow decommit even if initially committed?
      // (delay) purge the page
      mi_arena_schedule_purge(arena, slice_index, slice_count);
    }

    // and make it available to others again
    bool all_inuse = mi_bitmap_setN(arena->slices_free, slice_index, slice_count, NULL);
    if (!all_inuse) {
      _mi_error_message(EAGAIN, "trying to free an already freed arena block: %p, size %zu\n", mi_arena_slice_start(arena,slice_index), mi_size_of_slices(slice_count));
      return;
    };
  }
  else if (memid.memkind == MI_MEM_META) {
    _mi_meta_free(p, size, memid);
  }
  else {
    // arena was none, external, or static; nothing to do
    mi_assert_internal(mi_memid_needs_no_free(memid));
  }

  // try to purge expired decommits
  mi_arenas_try_purge(false, false);
}

// Purge the arenas; if `force_purge` is true, amenable parts are purged even if not yet expired
void _mi_arenas_collect(bool force_purge) {
  mi_arenas_try_purge(force_purge, force_purge /* visit all? */);
}

// Is a pointer inside any of our arenas?
bool _mi_arena_contains(const void* p) {
  const size_t max_arena = mi_arena_get_count();
  for (size_t i = 0; i < max_arena; i++) {
    mi_arena_t* arena = mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[i]);
    if (arena != NULL && mi_arena_start(arena) <= (const uint8_t*)p && mi_arena_start(arena) + mi_size_of_slices(arena->slice_count) >(const uint8_t*)p) {
      return true;
    }
  }
  return false;
}



/* -----------------------------------------------------------
  Remove an arena.
----------------------------------------------------------- */

// destroy owned arenas; this is unsafe and should only be done using `mi_option_destroy_on_exit`
// for dynamic libraries that are unloaded and need to release all their allocated memory.
static void mi_arenas_unsafe_destroy(void) {
  const size_t max_arena = mi_arena_get_count();
  size_t new_max_arena = 0;
  for (size_t i = 0; i < max_arena; i++) {
    mi_arena_t* arena = mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[i]);
    if (arena != NULL) {
      // mi_lock_done(&arena->abandoned_visit_lock);
      mi_atomic_store_ptr_release(mi_arena_t, &mi_arenas[i], NULL);
      if (mi_memkind_is_os(arena->memid.memkind)) {
        _mi_os_free(mi_arena_start(arena), mi_arena_size(arena), arena->memid);
      }
    }
  }

  // try to lower the max arena.
  size_t expected = max_arena;
  mi_atomic_cas_strong_acq_rel(&mi_arena_count, &expected, new_max_arena);
}


// destroy owned arenas; this is unsafe and should only be done using `mi_option_destroy_on_exit`
// for dynamic libraries that are unloaded and need to release all their allocated memory.
void _mi_arena_unsafe_destroy_all(void) {
  mi_arenas_unsafe_destroy();
  _mi_arenas_collect(true /* force purge */);  // purge non-owned arenas
}


/* -----------------------------------------------------------
  Add an arena.
----------------------------------------------------------- */

static bool mi_arena_add(mi_arena_t* arena, mi_arena_id_t* arena_id, mi_stats_t* stats) {
  mi_assert_internal(arena != NULL);
  mi_assert_internal(arena->slice_count > 0);
  if (arena_id != NULL) { *arena_id = -1; }

  // first try to find a NULL entry
  const size_t count = mi_arena_get_count();
  size_t i;
  for (i = 0; i < count; i++) {
    if (mi_arena_from_index(i) == NULL) {
      arena->id = mi_arena_id_create(i);
      mi_arena_t* expected = NULL;
      if (mi_atomic_cas_ptr_strong_release(mi_arena_t, &mi_arenas[i], &expected, arena)) {
        // success
        if (arena_id != NULL) { *arena_id = arena->id; }
        return true;
      }
      else {
        arena->id = _mi_arena_id_none();
      }
    }
  }

  // otherwise increase the max
  i = mi_atomic_increment_acq_rel(&mi_arena_count);
  if (i >= MI_MAX_ARENAS) {
    mi_atomic_decrement_acq_rel(&mi_arena_count);
    return false;
  }

  _mi_stat_counter_increase(&stats->arena_count,1);
  arena->id = mi_arena_id_create(i);
  mi_atomic_store_ptr_release(mi_arena_t,&mi_arenas[i], arena);
  if (arena_id != NULL) { *arena_id = arena->id; }
  return true;
}

static size_t mi_arena_info_slices_needed(size_t slice_count, size_t* bitmap_base) {
  if (slice_count == 0) slice_count = MI_BCHUNK_BITS;
  mi_assert_internal((slice_count % MI_BCHUNK_BITS) == 0);
  const size_t base_size = _mi_align_up(sizeof(mi_arena_t), MI_BCHUNK_SIZE);
  const size_t bitmaps_count = 5 + MI_ARENA_BIN_COUNT; // free, commit, dirty, purge, pages, and abandoned
  const size_t bitmaps_size = bitmaps_count * mi_bitmap_size(slice_count,NULL);
  const size_t size = base_size + bitmaps_size;

  const size_t os_page_size = _mi_os_page_size();
  const size_t info_size = _mi_align_up(size, os_page_size) + os_page_size; // + guard page
  const size_t info_slices = mi_slice_count_of_size(info_size);

  if (bitmap_base != NULL) *bitmap_base = base_size;
  return info_slices;
}

static mi_bitmap_t* mi_arena_bitmap_init(size_t slice_count, uint8_t** base) {
  mi_bitmap_t* bitmap = (mi_bitmap_t*)(*base);
  *base = (*base) + mi_bitmap_init(bitmap, slice_count, true /* already zero */);
  return bitmap;
}


static bool mi_manage_os_memory_ex2(void* start, size_t size, bool is_large, int numa_node, bool exclusive, mi_memid_t memid, mi_arena_id_t* arena_id) mi_attr_noexcept
{
  mi_assert(!is_large || (memid.initially_committed && memid.is_pinned));
  mi_assert(_mi_is_aligned(start,MI_ARENA_SLICE_SIZE));
  mi_assert(start!=NULL);
  if (start==NULL) return false;
  if (!_mi_is_aligned(start,MI_ARENA_SLICE_SIZE)) {
    // todo: use alignment in memid to align to slice size first?
    _mi_warning_message("cannot use OS memory since it is not aligned to %zu KiB (address %p)", MI_ARENA_SLICE_SIZE/MI_KiB, start);
    return false;
  }

  if (arena_id != NULL) { *arena_id = _mi_arena_id_none(); }

  const size_t slice_count = _mi_align_down(size / MI_ARENA_SLICE_SIZE, MI_BCHUNK_BITS);
  if (slice_count > MI_BITMAP_MAX_BIT_COUNT) {  // 16 GiB for now
    // todo: allow larger areas (either by splitting it up in arena's or having larger arena's)
    _mi_warning_message("cannot use OS memory since it is too large (size %zu MiB, maximum is %zu MiB)", size/MI_MiB, mi_size_of_slices(MI_BITMAP_MAX_BIT_COUNT)/MI_MiB);
    return false;
  }
  size_t bitmap_base;
  const size_t info_slices = mi_arena_info_slices_needed(slice_count, &bitmap_base);
  if (slice_count < info_slices+1) {
    _mi_warning_message("cannot use OS memory since it is not large enough (size %zu KiB, minimum required is %zu KiB)", size/MI_KiB, mi_size_of_slices(info_slices+1)/MI_KiB);
    return false;
  }

  mi_arena_t* arena = (mi_arena_t*)start;

  // commit & zero if needed
  bool is_zero = memid.initially_zero;
  if (!memid.initially_committed) {
    _mi_os_commit(arena, mi_size_of_slices(info_slices), NULL);
  }
  if (!is_zero) {
    _mi_memzero(arena, mi_size_of_slices(info_slices));
  }

  // init
  arena->id           = _mi_arena_id_none();
  arena->memid        = memid;
  arena->is_exclusive = exclusive;
  arena->slice_count  = slice_count;
  arena->info_slices  = info_slices;
  arena->numa_node    = numa_node; // TODO: or get the current numa node if -1? (now it allows anyone to allocate on -1)
  arena->is_large     = is_large;
  arena->purge_expire = 0;
  arena->purge_expire_extend = 0;
  // mi_lock_init(&arena->abandoned_visit_lock);

  // init bitmaps
  uint8_t* base = mi_arena_start(arena) + bitmap_base;
  arena->slices_free = mi_arena_bitmap_init(slice_count,&base);
  arena->slices_committed = mi_arena_bitmap_init(slice_count,&base);
  arena->slices_dirty = mi_arena_bitmap_init(slice_count,&base);
  arena->slices_purge = mi_arena_bitmap_init(slice_count, &base);
  arena->pages = mi_arena_bitmap_init(slice_count, &base);
  for( size_t i = 0; i < MI_ARENA_BIN_COUNT; i++) {
    arena->pages_abandoned[i] = mi_arena_bitmap_init(slice_count,&base);
  }
  mi_assert_internal(mi_size_of_slices(info_slices) >= (size_t)(base - mi_arena_start(arena)));

  // reserve our meta info (and reserve slices outside the memory area)
  mi_bitmap_unsafe_setN(arena->slices_free, info_slices /* start */, arena->slice_count - info_slices);
  if (memid.initially_committed) {
    mi_bitmap_unsafe_setN(arena->slices_committed, 0, arena->slice_count);
  }
  else {
    mi_bitmap_setN(arena->slices_committed, 0, info_slices, NULL);
  }
  if (!memid.initially_zero) {
    mi_bitmap_unsafe_setN(arena->slices_dirty, 0, arena->slice_count);
  }
  else {
    mi_bitmap_setN(arena->slices_dirty, 0, info_slices, NULL);
  }

  return mi_arena_add(arena, arena_id, &_mi_stats_main);
}


bool mi_manage_os_memory_ex(void* start, size_t size, bool is_committed, bool is_large, bool is_zero, int numa_node, bool exclusive, mi_arena_id_t* arena_id) mi_attr_noexcept {
  mi_memid_t memid = _mi_memid_create(MI_MEM_EXTERNAL);
  memid.mem.os.base = start;
  memid.mem.os.size = size;
  memid.initially_committed = is_committed;
  memid.initially_zero = is_zero;
  memid.is_pinned = is_large;
  return mi_manage_os_memory_ex2(start, size, is_large, numa_node, exclusive, memid, arena_id);
}

// Reserve a range of regular OS memory
int mi_reserve_os_memory_ex(size_t size, bool commit, bool allow_large, bool exclusive, mi_arena_id_t* arena_id) mi_attr_noexcept {
  if (arena_id != NULL) *arena_id = _mi_arena_id_none();
  size = _mi_align_up(size, MI_ARENA_SLICE_SIZE); // at least one slice
  mi_memid_t memid;
  void* start = _mi_os_alloc_aligned(size, MI_ARENA_SLICE_ALIGN, commit, allow_large, &memid);
  if (start == NULL) return ENOMEM;
  const bool is_large = memid.is_pinned; // todo: use separate is_large field?
  if (!mi_manage_os_memory_ex2(start, size, is_large, -1 /* numa node */, exclusive, memid, arena_id)) {
    _mi_os_free_ex(start, size, commit, memid);
    _mi_verbose_message("failed to reserve %zu KiB memory\n", _mi_divide_up(size, 1024));
    return ENOMEM;
  }
  _mi_verbose_message("reserved %zu KiB memory%s\n", _mi_divide_up(size, 1024), is_large ? " (in large os pages)" : "");
  // mi_debug_show_arenas(true, true, false);

  return 0;
}

// Manage a range of regular OS memory
bool mi_manage_os_memory(void* start, size_t size, bool is_committed, bool is_large, bool is_zero, int numa_node) mi_attr_noexcept {
  return mi_manage_os_memory_ex(start, size, is_committed, is_large, is_zero, numa_node, false /* exclusive? */, NULL);
}

// Reserve a range of regular OS memory
int mi_reserve_os_memory(size_t size, bool commit, bool allow_large) mi_attr_noexcept {
  return mi_reserve_os_memory_ex(size, commit, allow_large, false, NULL);
}


/* -----------------------------------------------------------
  Debugging
----------------------------------------------------------- */
static size_t mi_debug_show_bfield(mi_bfield_t field, char* buf) {
  size_t bit_set_count = 0;
  for (int bit = 0; bit < MI_BFIELD_BITS; bit++) {
    bool is_set = ((((mi_bfield_t)1 << bit) & field) != 0);
    if (is_set) bit_set_count++;
    buf[bit] = (is_set ? 'x' : '.');
  }
  return bit_set_count;
}

static size_t mi_debug_show_page_bfield(mi_bfield_t field, char* buf, mi_arena_t* arena, size_t slice_index) {
  size_t bit_set_count = 0;
  long bit_of_page = 0;
  for (int bit = 0; bit < MI_BFIELD_BITS; bit++, bit_of_page--) {
    bool is_set = ((((mi_bfield_t)1 << bit) & field) != 0);
    void* start = mi_arena_slice_start(arena, slice_index + bit);
    if (is_set) {
      mi_assert_internal(bit_of_page <= 0);
      bit_set_count++;
      mi_page_t* page = (mi_page_t*)start;
      char c = 'p';
      if (mi_page_is_abandoned_mapped(page)) { c = 'a'; }
      else if (mi_page_is_abandoned(page)) { c = (mi_page_is_singleton(page) ? 's' : 'f'); }
      bit_of_page = (long)page->memid.mem.arena.slice_count;
      buf[bit] = c;
    }    
    else {
      char c = '?';
      if (bit_of_page > 0) { c = '-'; }
      else if (_mi_meta_is_meta_page(start)) { c = 'm'; }
      else if (slice_index + bit < arena->info_slices) { c = 'i'; }
      // else if (mi_bitmap_is_setN(arena->pages_purge, slice_index + bit, NULL)) { c = '*'; }
      else if (mi_bitmap_is_set(arena->slices_free, slice_index+bit)) {
        if (mi_bitmap_is_set(arena->slices_purge, slice_index + bit)) { c = '~'; }
        else if (mi_bitmap_is_setN(arena->slices_committed, slice_index + bit, 1)) { c = '_'; }
        else { c = '.'; }
      }
      if (bit==MI_BFIELD_BITS-1 && bit_of_page > 1) { c = '>'; }
      buf[bit] = c;
    }    
  }
  return bit_set_count;
}

static size_t mi_debug_show_bitmap(const char* header, size_t slice_count, mi_bitmap_t* bitmap, bool invert, mi_arena_t* arena) {
  _mi_output_message("%s:\n", header);
  size_t bit_count = 0;
  size_t bit_set_count = 0;
  for (size_t i = 0; i < mi_bitmap_chunk_count(bitmap) && bit_count < slice_count; i++) {
    char buf[MI_BCHUNK_BITS + 64]; _mi_memzero(buf, sizeof(buf));
    size_t k = 0;
    mi_bchunk_t* chunk = &bitmap->chunks[i];

    if (i<10)        { buf[k++] = ('0' + (char)i); buf[k++] = ' '; buf[k++] = ' '; }
    else if (i<100)  { buf[k++] = ('0' + (char)(i/10)); buf[k++] = ('0' + (char)(i%10)); buf[k++] = ' '; }
    else if (i<1000) { buf[k++] = ('0' + (char)(i/100)); buf[k++] = ('0' + (char)((i%100)/10)); buf[k++] = ('0' + (char)(i%10)); }

    for (size_t j = 0; j < MI_BCHUNK_FIELDS; j++) {
      if (j > 0 && (j % 4) == 0) {
        buf[k++] = '\n'; _mi_memset(buf+k,' ',5); k += 5;
      }
      if (bit_count < slice_count) {
        mi_bfield_t bfield = chunk->bfields[j];
        if (invert) bfield = ~bfield;
        size_t xcount = (arena!=NULL ? mi_debug_show_page_bfield(bfield, buf + k, arena, bit_count)
                                     : mi_debug_show_bfield(bfield, buf + k));
        if (invert) xcount = MI_BFIELD_BITS - xcount;
        bit_set_count += xcount;
        k += MI_BFIELD_BITS;
        buf[k++] = ' ';
      }
      else {
        _mi_memset(buf + k, 'o', MI_BFIELD_BITS);
        k += MI_BFIELD_BITS;
      }
      bit_count += MI_BFIELD_BITS;
    }
    _mi_output_message("  %s\n", buf);
  }
  _mi_output_message("  total ('x'): %zu\n", bit_set_count);
  return bit_set_count;
}

void mi_debug_show_arenas(bool show_pages, bool show_inuse, bool show_committed) mi_attr_noexcept {
  size_t max_arenas = mi_arena_get_count();
  size_t free_total = 0;
  size_t slice_total = 0;
  //size_t abandoned_total = 0;
  size_t page_total = 0;
  for (size_t i = 0; i < max_arenas; i++) {
    mi_arena_t* arena = mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[i]);
    if (arena == NULL) break;
    slice_total += arena->slice_count;
    _mi_output_message("arena %zu at %p: %zu slices (%zu MiB)%s\n", i, arena, arena->slice_count, mi_size_of_slices(arena->slice_count)/MI_MiB, (arena->memid.is_pinned ? ", pinned" : ""));
    if (show_inuse) {
      free_total += mi_debug_show_bitmap("in-use slices", arena->slice_count, arena->slices_free, true, NULL);
    }
    if (show_committed) {
      mi_debug_show_bitmap("committed slices", arena->slice_count, arena->slices_committed, false, NULL);
    }
    // todo: abandoned slices
    //if (show_purge) {
    //  purge_total += mi_debug_show_bitmap("purgeable slices", arena->slice_count, arena->slices_purge, false, NULL);
    //}
    if (show_pages) {
      page_total += mi_debug_show_bitmap("pages (p:page, a:abandoned, f:full-abandoned, s:singleton-abandoned, i:arena-info, m:heap-meta-data, ~:free-purgable, _:free-committed, .:free-reserved)", arena->slice_count, arena->pages, false, arena);
    }
  }
  if (show_inuse)     _mi_output_message("total inuse slices    : %zu\n", slice_total - free_total);
  // if (show_abandoned) _mi_verbose_message("total abandoned slices: %zu\n", abandoned_total);
  if (show_pages)     _mi_output_message("total pages in arenas: %zu\n", page_total);
}


/* -----------------------------------------------------------
  Reserve a huge page arena.
----------------------------------------------------------- */
// reserve at a specific numa node
int mi_reserve_huge_os_pages_at_ex(size_t pages, int numa_node, size_t timeout_msecs, bool exclusive, mi_arena_id_t* arena_id) mi_attr_noexcept {
  if (arena_id != NULL) *arena_id = -1;
  if (pages==0) return 0;
  if (numa_node < -1) numa_node = -1;
  if (numa_node >= 0) numa_node = numa_node % _mi_os_numa_node_count();
  size_t hsize = 0;
  size_t pages_reserved = 0;
  mi_memid_t memid;
  void* p = _mi_os_alloc_huge_os_pages(pages, numa_node, timeout_msecs, &pages_reserved, &hsize, &memid);
  if (p==NULL || pages_reserved==0) {
    _mi_warning_message("failed to reserve %zu GiB huge pages\n", pages);
    return ENOMEM;
  }
  _mi_verbose_message("numa node %i: reserved %zu GiB huge pages (of the %zu GiB requested)\n", numa_node, pages_reserved, pages);

  if (!mi_manage_os_memory_ex2(p, hsize, true, numa_node, exclusive, memid, arena_id)) {
    _mi_os_free(p, hsize, memid);
    return ENOMEM;
  }
  return 0;
}

int mi_reserve_huge_os_pages_at(size_t pages, int numa_node, size_t timeout_msecs) mi_attr_noexcept {
  return mi_reserve_huge_os_pages_at_ex(pages, numa_node, timeout_msecs, false, NULL);
}

// reserve huge pages evenly among the given number of numa nodes (or use the available ones as detected)
int mi_reserve_huge_os_pages_interleave(size_t pages, size_t numa_nodes, size_t timeout_msecs) mi_attr_noexcept {
  if (pages == 0) return 0;

  // pages per numa node
  size_t numa_count = (numa_nodes > 0 ? numa_nodes : _mi_os_numa_node_count());
  if (numa_count <= 0) numa_count = 1;
  const size_t pages_per = pages / numa_count;
  const size_t pages_mod = pages % numa_count;
  const size_t timeout_per = (timeout_msecs==0 ? 0 : (timeout_msecs / numa_count) + 50);

  // reserve evenly among numa nodes
  for (size_t numa_node = 0; numa_node < numa_count && pages > 0; numa_node++) {
    size_t node_pages = pages_per;  // can be 0
    if (numa_node < pages_mod) node_pages++;
    int err = mi_reserve_huge_os_pages_at(node_pages, (int)numa_node, timeout_per);
    if (err) return err;
    if (pages < node_pages) {
      pages = 0;
    }
    else {
      pages -= node_pages;
    }
  }

  return 0;
}

int mi_reserve_huge_os_pages(size_t pages, double max_secs, size_t* pages_reserved) mi_attr_noexcept {
  MI_UNUSED(max_secs);
  _mi_warning_message("mi_reserve_huge_os_pages is deprecated: use mi_reserve_huge_os_pages_interleave/at instead\n");
  if (pages_reserved != NULL) *pages_reserved = 0;
  int err = mi_reserve_huge_os_pages_interleave(pages, 0, (size_t)(max_secs * 1000.0));
  if (err==0 && pages_reserved!=NULL) *pages_reserved = pages;
  return err;
}





/* -----------------------------------------------------------
  Arena purge
----------------------------------------------------------- */

static long mi_arena_purge_delay(void) {
  // <0 = no purging allowed, 0=immediate purging, >0=milli-second delay
  return (mi_option_get(mi_option_purge_delay) * mi_option_get(mi_option_arena_purge_mult));
}

// reset or decommit in an arena and update the commit bitmap
// assumes we own the area (i.e. slices_free is claimed by us)
static void mi_arena_purge(mi_arena_t* arena, size_t slice_index, size_t slice_count) {
  mi_assert_internal(!arena->memid.is_pinned);
  mi_assert_internal(mi_bitmap_is_clearN(arena->slices_free, slice_index, slice_count));

  const size_t size = mi_size_of_slices(slice_count);
  void* const p = mi_arena_slice_start(arena, slice_index);
  const bool all_committed = mi_bitmap_is_setN(arena->slices_committed, slice_index, slice_count);
  const bool needs_recommit = _mi_os_purge_ex(p, size, all_committed);

  // update committed bitmap
  if (needs_recommit) {
    mi_bitmap_clearN(arena->slices_committed, slice_index, slice_count);
  }
}


// Schedule a purge. This is usually delayed to avoid repeated decommit/commit calls.
// Note: assumes we (still) own the area as we may purge immediately
static void mi_arena_schedule_purge(mi_arena_t* arena, size_t slice_index, size_t slice_count) {
  const long delay = mi_arena_purge_delay();
  if (delay < 0 || _mi_preloading()) return;  // is purging allowed at all?

  mi_assert_internal(mi_bitmap_is_clearN(arena->slices_free, slice_index, slice_count));
  if (delay == 0) {
    // purge directly
    mi_arena_purge(arena, slice_index, slice_count);
  }
  else {
    // schedule purge
    mi_msecs_t expire = mi_atomic_loadi64_relaxed(&arena->purge_expire);
    if (expire == 0) {
      mi_atomic_storei64_release(&arena->purge_expire, _mi_clock_now() + delay);
      mi_atomic_storei64_release(&arena->purge_expire_extend, 0);      
    }
    else if (mi_atomic_loadi64_acquire(&arena->purge_expire_extend) < 10*delay) {     // limit max extension time
      mi_atomic_addi64_acq_rel(&arena->purge_expire_extend, (mi_msecs_t)(delay/10));  // add smallish extra delay
    }
    mi_bitmap_setN(arena->slices_purge, slice_index, slice_count, NULL);
  }
}

typedef struct mi_purge_visit_info_s {
  mi_msecs_t now;
  mi_msecs_t delay;
  bool all_purged;
  bool any_purged;
} mi_purge_visit_info_t;

static bool mi_arena_try_purge_range(mi_arena_t* arena, size_t slice_index, size_t slice_count) {
  if (mi_bitmap_try_clearN(arena->slices_free, slice_index, slice_count)) {
    // purge
    mi_arena_purge(arena, slice_index, slice_count);
    mi_assert_internal(mi_bitmap_is_clearN(arena->slices_committed, slice_index, slice_count));
    // and reset the free range
    mi_bitmap_setN(arena->slices_free, slice_index, slice_count, NULL);
    return true;
  }
  else {
    // was allocated again already
    return false;
  }
}

static bool mi_arena_try_purge_visitor(size_t slice_index, size_t slice_count, mi_arena_t* arena, void* arg) {
  mi_purge_visit_info_t* vinfo = (mi_purge_visit_info_t*)arg;  
  // try to purge: first claim the free blocks
  if (mi_arena_try_purge_range(arena, slice_index, slice_count)) {
    vinfo->any_purged = true;
    vinfo->all_purged = true;
  }
  else {
    // failed to claim the full range, try per slice instead
    for (size_t i = 0; i < slice_count; i++) {
      const bool purged = mi_arena_try_purge_range(arena, slice_index + i, 1);
      vinfo->any_purged = vinfo->any_purged || purged;
      vinfo->all_purged = vinfo->all_purged && purged;
    }
  }
  // done: clear the purge bits
  mi_bitmap_clearN(arena->slices_purge, slice_index, slice_count);
  return true; // continue
}

// returns true if anything was purged
static bool mi_arena_try_purge(mi_arena_t* arena, mi_msecs_t now, bool force)
{
  // check pre-conditions
  if (arena->memid.is_pinned) return false;
  mi_msecs_t expire_base = mi_atomic_loadi64_relaxed(&arena->purge_expire);
  mi_msecs_t expire_extend = mi_atomic_loadi64_relaxed(&arena->purge_expire_extend);
  const mi_msecs_t expire = expire_base + expire_extend;
  if (expire == 0) return false;

  // expired yet?
  if (!force && expire > now) return false;

  // reset expire (if not already set concurrently)
  if (mi_atomic_casi64_strong_acq_rel(&arena->purge_expire, &expire_base, (mi_msecs_t)0)) {
    mi_atomic_storei64_release(&arena->purge_expire_extend, (mi_msecs_t)0); // and also reset the extend
  }
  _mi_stat_counter_increase(&_mi_stats_main.arena_purges, 1);

  // go through all purge info's  (with max MI_BFIELD_BITS ranges at a time)
  mi_purge_visit_info_t vinfo = { now, mi_arena_purge_delay(), true /*all?*/, false /*any?*/};
  _mi_bitmap_forall_set_ranges(arena->slices_purge, &mi_arena_try_purge_visitor, arena, &vinfo);

  // if not fully purged, make sure to purge again in the future
  if (!vinfo.all_purged) {
    const long delay = mi_arena_purge_delay();
    mi_msecs_t expected = 0;
    if (mi_atomic_casi64_strong_acq_rel(&arena->purge_expire, &expected, _mi_clock_now() + delay)) {
      mi_atomic_storei64_release(&arena->purge_expire_extend, (mi_msecs_t)0);
    }
  }
  return vinfo.any_purged;
}


static void mi_arenas_try_purge(bool force, bool visit_all) {
  if (_mi_preloading() || mi_arena_purge_delay() <= 0) return;  // nothing will be scheduled

  const size_t max_arena = mi_arena_get_count();
  if (max_arena == 0) return;

  // allow only one thread to purge at a time
  static mi_atomic_guard_t purge_guard;
  mi_atomic_guard(&purge_guard)
  {
    const mi_msecs_t now = _mi_clock_now();
    const size_t arena_start = _mi_tld()->tseq % max_arena;
    size_t max_purge_count = (visit_all ? max_arena : 1);
    for (size_t _i = 0; _i < max_arena; _i++) {
      size_t i = _i + arena_start;
      if (i >= max_arena) { i -= max_arena; }
      mi_arena_t* arena = mi_arena_from_index(i);
      if (arena != NULL) {
        if (mi_arena_try_purge(arena, now, force)) {
          if (max_purge_count <= 1) break;
          max_purge_count--;
        }
      }
    }
  }
}

bool mi_abandoned_visit_blocks(mi_subproc_id_t subproc_id, int heap_tag, bool visit_blocks, mi_block_visit_fun* visitor, void* arg) {
  MI_UNUSED(subproc_id); MI_UNUSED(heap_tag); MI_UNUSED(visit_blocks); MI_UNUSED(visitor); MI_UNUSED(arg);
  _mi_error_message(EINVAL, "implement mi_abandoned_visit_blocks\n");
  return false;
}


/* -----------------------------------------------------------
  Unloading and reloading an arena.
----------------------------------------------------------- */
static bool mi_arena_page_register(size_t slice_index, size_t slice_count, mi_arena_t* arena, void* arg) {
  MI_UNUSED(arg); MI_UNUSED(slice_count);
  mi_assert_internal(slice_count == 1);
  mi_page_t* page = (mi_page_t*)mi_arena_slice_start(arena, slice_index);
  mi_assert_internal(mi_bitmap_is_setN(page->memid.mem.arena.arena->pages, page->memid.mem.arena.slice_index, 1));
  _mi_page_map_register(page);
  mi_assert_internal(_mi_ptr_page(page)==page);
  return true;
}

static bool mi_arena_pages_reregister(mi_arena_t* arena) {
  return _mi_bitmap_forall_set(arena->pages, &mi_arena_page_register, arena, NULL);
}

mi_decl_export bool mi_arena_unload(mi_arena_id_t arena_id, void** base, size_t* accessed_size, size_t* full_size) {
  const size_t count = mi_arena_get_count();
  const size_t arena_idx = mi_arena_id_index(arena_id);
  if (count <= arena_idx) {
    _mi_warning_message("arena id is invalid (%zu)\n", arena_id);
    return false;
  }
  mi_arena_t* arena = mi_arena_from_id(arena_id);
  if (arena==NULL) {
    return false;
  }
  else if (!arena->is_exclusive) {
    _mi_warning_message("cannot unload a non-exclusive arena (id %zu at %p)\n", arena_id, arena);
    return false;
  }
  else if (arena->memid.memkind != MI_MEM_EXTERNAL) {
    _mi_warning_message("can only unload managed arena's for external memory (id %zu at %p)\n", arena_id, arena);
    return false;
  }

  // find accessed size
  size_t asize;
  // scan the commit map for the highest entry
  size_t idx;
  if (mi_bitmap_bsr(arena->slices_committed, &idx)) {
    asize = (idx + 1)* MI_ARENA_SLICE_SIZE;
  }
  else {
    asize = mi_arena_info_slices(arena) * MI_ARENA_SLICE_SIZE;
  }
  if (base != NULL) { *base = (void*)arena; }
  if (full_size != NULL) { *full_size = arena->memid.mem.os.size;  }
  if (accessed_size != NULL) { *accessed_size = asize; }

  // unregister the pages
  _mi_page_map_unregister_range(arena, asize);

  // set the entry to NULL
  mi_atomic_store_ptr_release(mi_arena_t, &mi_arenas[arena_idx], NULL);
  if (arena_idx + 1 == count) { // try adjust the count?
    size_t expected = count;
    mi_atomic_cas_strong_acq_rel(&mi_arena_count, &expected, count-1);
  }
  return true;
}

mi_decl_export bool mi_arena_reload(void* start, size_t size, bool is_committed, bool is_large, bool is_zero, mi_arena_id_t* arena_id) {
  // assume the memory area is already containing the arena
  if (arena_id != NULL) { *arena_id = _mi_arena_id_none(); }
  if (start == NULL || size == 0) return false;
  mi_arena_t* arena = (mi_arena_t*)start;
  mi_memid_t memid = arena->memid;
  if (memid.memkind != MI_MEM_EXTERNAL) {
    _mi_warning_message("can only reload arena's from external memory (%p)\n", arena);
    return false;
  }
  if (memid.mem.os.base != start) {
    _mi_warning_message("the reloaded arena base address differs from the external memory (arena: %p, external: %p)\n", arena, start);
    return false;
  }
  if (memid.mem.os.size != size) {
    _mi_warning_message("the reloaded arena size differs from the external memory (arena size: %zu, external size: %zu)\n", arena->memid.mem.os.size, size);
    return false;
  }
  if (!arena->is_exclusive) {
    _mi_warning_message("the reloaded arena is not exclusive\n");
    return false;
  }
  arena->memid.is_pinned = is_large;
  arena->memid.initially_committed = is_committed;
  arena->memid.initially_zero = is_zero;
  arena->is_exclusive = true;
  arena->is_large = is_large;
  arena->id = _mi_arena_id_none();
  if (!mi_arena_add(arena, arena_id, &_mi_stats_main)) {
    return false;
  }
  mi_arena_pages_reregister(arena);
  return true;
}

