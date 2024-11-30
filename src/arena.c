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


// A memory arena descriptor
typedef struct mi_arena_s {
  mi_memid_t          memid;                // memid of the memory area
  mi_arena_id_t       id;                   // arena id; 0 for non-specific

  size_t              block_count;          // size of the area in arena blocks (of `MI_ARENA_BLOCK_SIZE`)
  int                 numa_node;            // associated NUMA node
  bool                exclusive;            // only allow allocations if specifically for this arena
  bool                is_large;             // memory area consists of large- or huge OS pages (always committed)
  mi_lock_t           abandoned_visit_lock; // lock is only used when abandoned segments are being visited
  _Atomic(mi_msecs_t) purge_expire;         // expiration time when blocks should be decommitted from `blocks_decommit`.
  mi_subproc_t*       subproc;

  mi_bitmap_t         blocks_free;          // is the block free?
  mi_bitmap_t         blocks_committed;     // is the block committed? (i.e. accessible)
  mi_bitmap_t         blocks_purge;         // can the block be purged? (block in purge => block in free)
  mi_bitmap_t         blocks_dirty;         // is the block potentially non-zero?
  mi_bitmap_t         blocks_abandoned[MI_BIN_COUNT];  // abandoned pages per size bin (a set bit means the start of the page)
                                            // the full queue contains abandoned full pages
} mi_arena_t;

#define MI_MAX_ARENAS         (1024)        // Limited for now (and takes up .bss)

// The available arenas
static mi_decl_cache_align _Atomic(mi_arena_t*) mi_arenas[MI_MAX_ARENAS];
static mi_decl_cache_align _Atomic(size_t)      mi_arena_count; // = 0


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
    return mi_arena_id_is_suitable(memid.mem.arena.id, memid.mem.arena.is_exclusive, request_arena_id);
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
  return mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[idx]);
}

mi_arena_t* mi_arena_from_id(mi_arena_id_t id) {
  return mi_arena_from_index(mi_arena_id_index(id));
}


/* -----------------------------------------------------------
  Util
----------------------------------------------------------- */


// Size of an arena
static size_t mi_arena_size(mi_arena_t* arena) {
  return mi_size_of_blocks(arena->block_count);
}

static size_t mi_arena_info_blocks(void) {
  const size_t os_page_size = _mi_os_page_size();
  const size_t info_size    = _mi_align_up(sizeof(mi_arena_t), os_page_size) + os_page_size; // + guard page
  const size_t info_blocks  = mi_block_count_of_size(info_size);
  return info_blocks;
}


// Start of the arena memory area
static uint8_t* mi_arena_start(mi_arena_t* arena) {
  return ((uint8_t*)arena);
}

// Start of a block
void* mi_arena_block_start(mi_arena_t* arena, size_t block_index) {
  return (mi_arena_start(arena) + mi_size_of_blocks(block_index));
}

// Arena area
void* mi_arena_area(mi_arena_id_t arena_id, size_t* size) {
  if (size != NULL) *size = 0;
  const size_t arena_index = mi_arena_id_index(arena_id);
  if (arena_index >= MI_MAX_ARENAS) return NULL;
  mi_arena_t* arena = mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[arena_index]);
  if (arena == NULL) return NULL;
  if (size != NULL) { *size = mi_size_of_blocks(arena->block_count); }
  return mi_arena_start(arena);
}


// Create an arena memid
static mi_memid_t mi_memid_create_arena(mi_arena_id_t id, bool is_exclusive, size_t block_index) {
  mi_memid_t memid = _mi_memid_create(MI_MEM_ARENA);
  memid.mem.arena.id = id;
  memid.mem.arena.block_index = block_index;
  memid.mem.arena.is_exclusive = is_exclusive;
  return memid;
}

// returns if the arena is exclusive
static bool mi_arena_memid_indices(mi_memid_t memid, size_t* arena_index, size_t* block_index) {
  mi_assert_internal(memid.memkind == MI_MEM_ARENA);
  *arena_index = mi_arena_id_index(memid.mem.arena.id);
  *block_index = memid.mem.arena.block_index;
  return memid.mem.arena.is_exclusive;
}

// get the arena and block index
static mi_arena_t* mi_arena_from_memid(mi_memid_t memid, size_t* block_index) {
  size_t arena_index;
  mi_arena_memid_indices(memid, &arena_index, block_index);
  return mi_arena_from_index(arena_index);
}


static mi_arena_t* mi_page_arena(mi_page_t* page, size_t* block_index) {
  // todo: maybe store the arena* directly in the page?
  return mi_arena_from_memid(page->memid, block_index);
}

/* -----------------------------------------------------------
  Arena Allocation
----------------------------------------------------------- */

static mi_decl_noinline void* mi_arena_try_alloc_at(
  mi_arena_t* arena, size_t needed_bcount, bool commit, size_t tseq, mi_memid_t* memid)
{  
  size_t block_index;
  if (!mi_bitmap_try_find_and_clearN(&arena->blocks_free, needed_bcount, tseq, &block_index)) return NULL;

  // claimed it!
  void* p = mi_arena_block_start(arena, block_index);
  *memid = mi_memid_create_arena(arena->id, arena->exclusive, block_index);
  memid->is_pinned = arena->memid.is_pinned;

  // set the dirty bits
  if (arena->memid.initially_zero) {
    memid->initially_zero = mi_bitmap_xsetN(MI_BIT_SET, &arena->blocks_dirty, block_index, needed_bcount, NULL);
  }

  // set commit state
  if (commit) {
    // commit requested, but the range may not be committed as a whole: ensure it is committed now
    memid->initially_committed = true;

    bool all_already_committed;
    mi_bitmap_xsetN(MI_BIT_SET, &arena->blocks_committed, block_index, needed_bcount, &all_already_committed);
    if (!all_already_committed) {
      bool commit_zero = false;
      if (!_mi_os_commit(p, mi_size_of_blocks(needed_bcount), &commit_zero, NULL)) {
        memid->initially_committed = false;
      }
      else {
        if (commit_zero) { memid->initially_zero = true; }
      }
    }
  }
  else {
    // no need to commit, but check if already fully committed
    memid->initially_committed = mi_bitmap_is_xsetN(MI_BIT_SET, &arena->blocks_committed, block_index, needed_bcount);
  }

  mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_CLEAR, &arena->blocks_free, block_index, needed_bcount));
  if (commit) { mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_SET, &arena->blocks_committed, block_index, needed_bcount)); }
  mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_SET, &arena->blocks_dirty, block_index, needed_bcount));
  // mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_CLEAR, &arena->blocks_purge, block_index, needed_bcount));

  return p;
}


// try to reserve a fresh arena space
static bool mi_arena_reserve(size_t req_size, bool allow_large, mi_arena_id_t req_arena_id, mi_arena_id_t* arena_id)
{
  if (_mi_preloading()) return false;  // use OS only while pre loading
  if (req_arena_id != _mi_arena_id_none()) return false;

  const size_t arena_count = mi_atomic_load_acquire(&mi_arena_count);
  if (arena_count > (MI_MAX_ARENAS - 4)) return false;

  // calc reserve
  size_t arena_reserve = mi_option_get_size(mi_option_arena_reserve);
  if (arena_reserve == 0) return false;

  if (!_mi_os_has_virtual_reserve()) {
    arena_reserve = arena_reserve/4;  // be conservative if virtual reserve is not supported (for WASM for example)
  }
  arena_reserve = _mi_align_up(arena_reserve, MI_ARENA_BLOCK_SIZE);

  if (arena_count >= 8 && arena_count <= 128) {
    // scale up the arena sizes exponentially every 8 entries (128 entries get to 589TiB)
    const size_t multiplier = (size_t)1 << _mi_clamp(arena_count/8, 0, 16);
    size_t reserve = 0;
    if (!mi_mul_overflow(multiplier, arena_reserve, &reserve)) {
      arena_reserve = reserve;
    }
  }

  // check arena bounds
  const size_t min_reserve = mi_size_of_blocks(mi_arena_info_blocks() + 1);
  const size_t max_reserve = MI_BITMAP_MAX_BITS * MI_ARENA_BLOCK_SIZE;
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

  return (mi_reserve_os_memory_ex(arena_reserve, arena_commit, allow_large, false /* exclusive? */, arena_id) == 0);
}




/* -----------------------------------------------------------
  Arena iteration
----------------------------------------------------------- */

static inline bool mi_arena_is_suitable(mi_arena_t* arena, mi_arena_id_t req_arena_id, mi_subproc_t* subproc, int numa_node, bool allow_large) {
  if (subproc != NULL && arena->subproc != subproc) return false;
  if (!allow_large && arena->is_large) return false;
  if (!mi_arena_id_is_suitable(arena->id, arena->exclusive, req_arena_id)) return false;
  if (req_arena_id == _mi_arena_id_none()) { // if not specific, check numa affinity
    const bool numa_suitable = (numa_node < 0 || arena->numa_node < 0 || arena->numa_node == numa_node);
    if (!numa_suitable) return false;
  }
  return true;
}

#define MI_THREADS_PER_ARENA  (16)

#define mi_forall_arenas(req_arena_id, subproc, allow_large, tseq, var_arena_id, var_arena) \
  { \
  size_t _max_arena; \
  size_t _start; \
  if (req_arena_id == _mi_arena_id_none()) { \
    _max_arena = mi_atomic_load_relaxed(&mi_arena_count); \
    _start = (_max_arena <= 1 ? 0 : (tseq / MI_THREADS_PER_ARENA) % _max_arena); \
  } \
  else { \
    _max_arena = 1; \
    _start = mi_arena_id_index(req_arena_id); \
    mi_assert_internal(mi_atomic_load_relaxed(&mi_arena_count) > _start); \
  } \
  for (size_t i = 0; i < _max_arena; i++) { \
    size_t _idx = i + _start; \
    if (_idx >= _max_arena) { _idx -= _max_arena; } \
    const mi_arena_id_t var_arena_id = mi_arena_id_create(_idx); MI_UNUSED(var_arena_id);\
    mi_arena_t* const   var_arena = mi_arena_from_index(_idx); \
    if (mi_arena_is_suitable(var_arena,req_arena_id,subproc,-1 /* todo: numa node */,allow_large)) \
    {

#define mi_forall_arenas_end()  }}} 


/* -----------------------------------------------------------
  Arena allocation
----------------------------------------------------------- */

// allocate blocks from the arenas
static mi_decl_noinline void* mi_arena_try_find_free(
  size_t block_count, size_t alignment,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, mi_memid_t* memid, mi_tld_t* tld)
{
  mi_assert_internal(block_count <= mi_block_count_of_size(MI_ARENA_MAX_OBJ_SIZE));
  mi_assert(alignment <= MI_ARENA_BLOCK_ALIGN);
  if (alignment > MI_ARENA_BLOCK_ALIGN) return NULL;

  // search arena's
  mi_subproc_t* const subproc = tld->subproc;
  const size_t tseq = tld->tseq;
  mi_forall_arenas(req_arena_id, subproc, allow_large, tseq, arena_id, arena)
  {
    void* p = mi_arena_try_alloc_at(arena, block_count, commit, tseq, memid);
    if (p != NULL) return p;
  }
  mi_forall_arenas_end();
  return NULL;
}

// Allocate blocks from the arena's -- potentially allocating a fresh arena
static mi_decl_noinline void* mi_arena_try_alloc(
  size_t block_count, size_t alignment,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, mi_memid_t* memid, mi_tld_t* tld)
{
  mi_assert(block_count <= MI_ARENA_MAX_OBJ_BLOCKS);
  mi_assert(alignment <= MI_ARENA_BLOCK_ALIGN);

  // try to find free blocks in the arena's
  void* p = mi_arena_try_find_free(block_count, alignment, commit, allow_large, req_arena_id, memid, tld);
  if (p != NULL) return p;

  // otherwise, try to first eagerly reserve a new arena
  if (req_arena_id == _mi_arena_id_none()) {
    mi_arena_id_t arena_id = 0;
    if (mi_arena_reserve(mi_size_of_blocks(block_count), allow_large, req_arena_id, &arena_id)) {
      // and try allocate in there
      mi_assert_internal(req_arena_id == _mi_arena_id_none());
      p = mi_arena_try_find_free(block_count, alignment, commit, allow_large, req_arena_id, memid, tld);
      if (p != NULL) return p;
    }
  }

  return NULL;
}

// Allocate from the OS (if allowed)
static void* mi_arena_os_alloc_aligned(
  size_t size, size_t alignment, size_t align_offset,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, mi_memid_t* memid, mi_tld_t* tld)
{
  // if we cannot use OS allocation, return NULL
  if (mi_option_is_enabled(mi_option_disallow_os_alloc) || req_arena_id != _mi_arena_id_none()) {
    errno = ENOMEM;
    return NULL;
  }

  if (align_offset > 0) {
    return _mi_os_alloc_aligned_at_offset(size, alignment, align_offset, commit, allow_large, memid, &tld->stats);
  }
  else {
    return _mi_os_alloc_aligned(size, alignment, commit, allow_large, memid, &tld->stats);
  }
}


// Allocate large sized memory
void* _mi_arena_alloc_aligned(
  size_t size, size_t alignment, size_t align_offset,
  bool commit, bool allow_large,
  mi_arena_id_t req_arena_id, mi_memid_t* memid, mi_tld_t* tld)
{
  mi_assert_internal(memid != NULL && tld != NULL);
  mi_assert_internal(size > 0);

  // *memid = _mi_memid_none();
  // const int numa_node = _mi_os_numa_node(&tld->os); // current numa node

  // try to allocate in an arena if the alignment is small enough and the object is not too small (as for heap meta data)
  if (!mi_option_is_enabled(mi_option_disallow_arena_alloc) && // is arena allocation allowed?
      req_arena_id == _mi_arena_id_none() &&                   // not a specific arena?
      size >= MI_ARENA_MIN_OBJ_SIZE && size <= MI_ARENA_MAX_OBJ_SIZE &&  // and not too small/large
      alignment <= MI_ARENA_BLOCK_ALIGN && align_offset == 0)            // and good alignment
  {
    const size_t block_count = mi_block_count_of_size(size);
    void* p = mi_arena_try_alloc(block_count, alignment, commit, allow_large, req_arena_id, memid, tld);
    if (p != NULL) return p;
  }

  // fall back to the OS
  void* p = mi_arena_os_alloc_aligned(size, alignment, align_offset, commit, allow_large, req_arena_id, memid, tld);
  return p;
}

void* _mi_arena_alloc(size_t size, bool commit, bool allow_large, mi_arena_id_t req_arena_id, mi_memid_t* memid, mi_tld_t* tld)
{
  return _mi_arena_alloc_aligned(size, MI_ARENA_BLOCK_SIZE, 0, commit, allow_large, req_arena_id, memid, tld);
}



/* -----------------------------------------------------------
  Arena page allocation
----------------------------------------------------------- */

static mi_page_t* mi_arena_page_try_find_abandoned(size_t block_count, size_t block_size, mi_arena_id_t req_arena_id, mi_tld_t* tld)
{
  const size_t bin = _mi_bin(block_size); 
  mi_assert_internal(bin < MI_BIN_COUNT);

  // any abandoned in our size class?
  mi_subproc_t* const subproc = tld->subproc;
  if (mi_atomic_load_relaxed(&subproc->abandoned_count[bin]) == 0) return NULL;

  // search arena's
  const bool allow_large = true;
  size_t tseq = tld->tseq;
  mi_forall_arenas(req_arena_id, subproc, allow_large, tseq, arena_id, arena)
  {
    size_t block_index;
    if (mi_bitmap_try_find_and_clear(&arena->blocks_abandoned[bin], tseq, &block_index)) {
      // found an abandoned page of the right size
      mi_atomic_decrement_relaxed(&subproc->abandoned_count[bin]);
      mi_page_t* page = (mi_page_t*)mi_arena_block_start(arena, block_index);
      mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_CLEAR, &arena->blocks_free, block_index, block_count));
      mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_SET, &arena->blocks_committed, block_index, block_count));
      mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_SET, &arena->blocks_dirty, block_index, block_count));
      mi_assert_internal(mi_bitmap_is_xsetN(MI_BIT_CLEAR, &arena->blocks_purge, block_index, block_count));
      mi_assert_internal(mi_page_block_size(page) == block_size);
      mi_assert_internal(!mi_page_is_full(page));
      mi_assert_internal(mi_page_is_abandoned(page));
      return page;
    }
  }
  mi_forall_arenas_end();
  return NULL;
}

static mi_page_t* mi_arena_page_alloc_fresh(size_t block_count, size_t block_size, mi_arena_id_t req_arena_id, mi_tld_t* tld)
{
  const bool allow_large = true;
  const bool commit = true;
  const size_t alignment = MI_ARENA_BLOCK_ALIGN;

  // try to allocate from free space in arena's
  mi_memid_t memid = _mi_memid_none();
  mi_page_t* page = NULL;
  if (_mi_option_get_fast(mi_option_disallow_arena_alloc)==0 && req_arena_id == _mi_arena_id_none()) {
    page = (mi_page_t*)mi_arena_try_alloc(block_count, alignment, commit, allow_large, req_arena_id, &memid, tld);
  }

  // otherwise fall back to the OS
  if (page == NULL) {
    page = (mi_page_t*)mi_arena_os_alloc_aligned(mi_size_of_blocks(block_count), alignment, 0 /* align offset */, commit, allow_large, req_arena_id, &memid, tld);
  }

  if (page == NULL) return NULL;

  // claimed free blocks: initialize the page partly
  _mi_memzero_aligned(page, sizeof(*page));
  mi_assert(MI_PAGE_INFO_SIZE >= _mi_align_up(sizeof(*page), MI_PAGE_ALIGN));
  const size_t reserved = (mi_size_of_blocks(block_count) - MI_PAGE_INFO_SIZE) / block_size;
  mi_assert_internal(reserved > 0 && reserved <= UINT16_MAX);
  page->reserved = (uint16_t)reserved;
  page->page_start = (uint8_t*)page + MI_PAGE_INFO_SIZE;
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

  mi_assert_internal(mi_page_block_size(page) == block_size);
  mi_assert_internal(mi_page_is_abandoned(page));
  return page;
}

// block_count: arena block count for the page
// block size : page block size
static mi_page_t* mi_arena_page_allocN(mi_heap_t* heap, size_t block_count, size_t block_size) {
  const mi_arena_id_t  req_arena_id = heap->arena_id;
  mi_tld_t* const tld = heap->tld;

  // 1. look for an abandoned page  
  mi_page_t* page = mi_arena_page_try_find_abandoned(block_count, block_size, req_arena_id, tld);
  if (page != NULL) {
    _mi_page_reclaim(heap,page);
    return page;
  }

  // 2. find a free block, potentially allocating a new arena
  page = mi_arena_page_alloc_fresh(block_count, block_size, req_arena_id, tld);
  if (page != NULL) {    
    _mi_page_init(heap, page);
    return page;
  }

  return NULL;
}


static mi_page_t* mi_singleton_page_alloc(mi_heap_t* heap, size_t block_size, size_t page_alignment) {
  MI_UNUSED(heap); MI_UNUSED(block_size); MI_UNUSED(page_alignment);
  _mi_error_message(EINVAL, "singleton page is not yet implemented\n");
  return NULL;
}


mi_page_t* _mi_arena_page_alloc(mi_heap_t* heap, size_t block_size, size_t page_alignment) {
  mi_page_t* page;
  if mi_unlikely(page_alignment > MI_BLOCK_ALIGNMENT_MAX) {
    mi_assert_internal(_mi_is_power_of_two(page_alignment));
    page = mi_singleton_page_alloc(heap, block_size, page_alignment);
  }
  else if (block_size <= MI_SMALL_MAX_OBJ_SIZE) {
    page = mi_arena_page_allocN(heap, mi_block_count_of_size(MI_SMALL_PAGE_SIZE), block_size);
  }
  else if (block_size <= MI_MEDIUM_MAX_OBJ_SIZE) {
    page = mi_arena_page_allocN(heap, mi_block_count_of_size(MI_MEDIUM_PAGE_SIZE), block_size);
  }
  else if (block_size <= MI_LARGE_MAX_OBJ_SIZE) {
    page = mi_arena_page_allocN(heap, mi_block_count_of_size(MI_LARGE_PAGE_SIZE), block_size);
  }
  else {
    page = mi_singleton_page_alloc(heap, block_size, page_alignment);
  }  
  // mi_assert_internal(page == NULL || _mi_page_segment(page)->subproc == tld->subproc);
  return page;
}

static uint8_t* mi_arena_page_allocated_area(mi_page_t* page, size_t* psize) {
  // todo: record real allocated size instead of trying to recalculate?
  size_t page_size;
  uint8_t* const pstart = mi_page_area(page, &page_size);
  const size_t diff = pstart - (uint8_t*)page;
  const size_t size = _mi_align_up(page_size + diff, MI_ARENA_BLOCK_SIZE);
  if (psize != NULL) { *psize = size;  }
  return (uint8_t*)page;
}

void _mi_arena_page_free(mi_page_t* page, mi_tld_t* tld) {
  size_t size;
  uint8_t* pstart = mi_arena_page_allocated_area(page, &size);
  _mi_page_map_unregister(page);
  _mi_arena_free(pstart, size, size, page->memid, &tld->stats);
}

/* -----------------------------------------------------------
  Arena abandon
----------------------------------------------------------- */

void _mi_arena_page_abandon(mi_page_t* page, mi_tld_t* tld) {
  mi_assert_internal(mi_page_is_abandoned(page));
  mi_assert_internal(page->next==NULL);
  
  if (mi_page_all_free(page)) {
    _mi_arena_page_free(page, tld);
  }
  else if (mi_page_is_full(page)) {  // includes singleton pages
    // leave as is; it will be reclaimed on free
  }
  else if (mi_memkind_is_os(page->memid.memkind)) {
    _mi_error_message(EINVAL, "implement page abandon for OS allocated pages\n");
    // leave as is; it will be reclaimed on the first free
  }
  else if (page->memid.memkind==MI_MEM_ARENA) {
    size_t size;
    mi_arena_page_allocated_area(page, &size);
    size_t bin      = _mi_bin(mi_page_block_size(page));
    size_t block_index;
    mi_arena_t* arena = mi_page_arena(page, &block_index);
    bool were_zero = mi_bitmap_xsetN(MI_BIT_SET, &arena->blocks_abandoned[bin], block_index, 1, NULL);
    MI_UNUSED(were_zero); mi_assert_internal(were_zero);
    mi_atomic_increment_relaxed(&tld->subproc->abandoned_count[bin]);
  }
  else {
    _mi_error_message(EINVAL, "implement page abandon for external allocated pages\n");
    // leave as is; it will be reclaimed on the first free
  }
}

bool _mi_arena_try_reclaim(mi_heap_t* heap, mi_page_t* page) {
  mi_assert_internal(mi_page_is_abandoned(page));
  // if (!mi_page_is_abandoned(page)) return false;  // it is not abandoned
  mi_memid_t memid = page->memid;
  if (!_mi_arena_memid_is_suitable(memid, heap->arena_id)) return false;  // don't reclaim between exclusive and non-exclusive arena's

  if mi_likely(memid.memkind == MI_MEM_ARENA) {
    size_t block_index;
    mi_arena_t* arena = mi_page_arena(page, &block_index);
    if (arena->subproc != heap->tld->subproc) return false;  // only reclaim within the same subprocess

    // don't reclaim more from a `free` call than half the current segments
    // this is to prevent a pure free-ing thread to start owning too many segments
    // (but not for out-of-arena segments as that is the main way to be reclaimed for those)
    // if (segment->memid.memkind == MI_MEM_ARENA && heap->tld->segments.reclaim_count * 2 > heap->tld->segments.count) {
    //  return false;
    // }
    const size_t bin = _mi_bin(page->block_size);
    if (mi_bitmap_try_xsetN(MI_BIT_CLEAR, &arena->blocks_abandoned[bin], block_index, 1)) {
      // we got it atomically
      _mi_page_reclaim(heap, page);
      mi_assert_internal(!mi_page_is_abandoned(page));
      return true;
    }
  }
  else {
    _mi_warning_message("implement reclaim for OS allocated pages\n");
  }


  return false;
}

void _mi_arena_reclaim_all_abandoned(mi_heap_t* heap) {
  MI_UNUSED(heap);
  // TODO: implement this
  return;
}


/* -----------------------------------------------------------
  Arena free
----------------------------------------------------------- */
static void mi_arena_schedule_purge(mi_arena_t* arena, size_t block_idx, size_t blocks, mi_stats_t* stats);
static void mi_arenas_try_purge(bool force, bool visit_all, mi_stats_t* stats);

void _mi_arena_free(void* p, size_t size, size_t committed_size, mi_memid_t memid, mi_stats_t* stats) {
  mi_assert_internal(size > 0 && stats != NULL);
  mi_assert_internal(committed_size <= size);
  if (p==NULL) return;
  if (size==0) return;
  const bool all_committed = (committed_size == size);

  // need to set all memory to undefined as some parts may still be marked as no_access (like padding etc.)
  mi_track_mem_undefined(p, size);

  if (mi_memkind_is_os(memid.memkind)) {
    // was a direct OS allocation, pass through
    if (!all_committed && committed_size > 0) {
      // if partially committed, adjust the committed stats (as `_mi_os_free` will increase decommit by the full size)
      _mi_stat_decrease(&_mi_stats_main.committed, committed_size);
    }
    _mi_os_free(p, size, memid, stats);
  }
  else if (memid.memkind == MI_MEM_ARENA) {
    // allocated in an arena
    size_t arena_idx;
    size_t block_idx;
    mi_arena_memid_indices(memid, &arena_idx, &block_idx);
    mi_assert_internal(arena_idx < MI_MAX_ARENAS);
    mi_arena_t* arena = mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[arena_idx]);
    mi_assert_internal(arena != NULL);
    const size_t blocks = mi_block_count_of_size(size);

    // checks
    if (arena == NULL) {
      _mi_error_message(EINVAL, "trying to free from an invalid arena: %p, size %zu, memid: 0x%zx\n", p, size, memid);
      return;
    }
    mi_assert_internal(block_idx < arena->block_count);
    mi_assert_internal(block_idx > mi_arena_info_blocks());
    if (block_idx <= mi_arena_info_blocks() || block_idx > arena->block_count) {
      _mi_error_message(EINVAL, "trying to free from an invalid arena block: %p, size %zu, memid: 0x%zx\n", p, size, memid);
      return;
    }

    // potentially decommit
    if (arena->memid.is_pinned || arena->memid.initially_committed) {
      mi_assert_internal(all_committed);
    }
    else {
      if (!all_committed) {
        // mark the entire range as no longer committed (so we recommit the full range when re-using)
        mi_bitmap_xsetN(MI_BIT_CLEAR, &arena->blocks_committed, blocks, block_idx, NULL);
        mi_track_mem_noaccess(p, size);
        if (committed_size > 0) {
          // if partially committed, adjust the committed stats (is it will be recommitted when re-using)
          // in the delayed purge, we now need to not count a decommit if the range is not marked as committed.
          _mi_stat_decrease(&_mi_stats_main.committed, committed_size);
        }
        // note: if not all committed, it may be that the purge will reset/decommit the entire range
        // that contains already decommitted parts. Since purge consistently uses reset or decommit that
        // works (as we should never reset decommitted parts).
      }
      // (delay) purge the entire range
      mi_arena_schedule_purge(arena, block_idx, blocks, stats);
    }

    // and make it available to others again
    bool all_inuse = mi_bitmap_xsetN(MI_BIT_SET, &arena->blocks_free, block_idx, blocks, NULL);
    if (!all_inuse) {
      _mi_error_message(EAGAIN, "trying to free an already freed arena block: %p, size %zu\n", p, size);
      return;
    };
  }
  else {
    // arena was none, external, or static; nothing to do
    mi_assert_internal(memid.memkind < MI_MEM_OS);
  }

  // purge expired decommits
  mi_arenas_try_purge(false, false, stats);
}

// destroy owned arenas; this is unsafe and should only be done using `mi_option_destroy_on_exit`
// for dynamic libraries that are unloaded and need to release all their allocated memory.
static void mi_arenas_unsafe_destroy(void) {
  const size_t max_arena = mi_atomic_load_relaxed(&mi_arena_count);
  size_t new_max_arena = 0;
  for (size_t i = 0; i < max_arena; i++) {
    mi_arena_t* arena = mi_atomic_load_ptr_acquire(mi_arena_t, &mi_arenas[i]);
    if (arena != NULL) {
      mi_lock_done(&arena->abandoned_visit_lock);
      if (mi_memkind_is_os(arena->memid.memkind)) {
        mi_atomic_store_ptr_release(mi_arena_t, &mi_arenas[i], NULL);
        _mi_os_free(mi_arena_start(arena), mi_arena_size(arena), arena->memid, &_mi_stats_main);
      }
    }
  }

  // try to lower the max arena.
  size_t expected = max_arena;
  mi_atomic_cas_strong_acq_rel(&mi_arena_count, &expected, new_max_arena);
}

// Purge the arenas; if `force_purge` is true, amenable parts are purged even if not yet expired
void _mi_arenas_collect(bool force_purge, mi_stats_t* stats) {
  mi_arenas_try_purge(force_purge, force_purge /* visit all? */, stats);
}

// destroy owned arenas; this is unsafe and should only be done using `mi_option_destroy_on_exit`
// for dynamic libraries that are unloaded and need to release all their allocated memory.
void _mi_arena_unsafe_destroy_all(mi_stats_t* stats) {
  mi_arenas_unsafe_destroy();
  _mi_arenas_collect(true /* force purge */, stats);  // purge non-owned arenas
}

// Is a pointer inside any of our arenas?
bool _mi_arena_contains(const void* p) {
  const size_t max_arena = mi_atomic_load_relaxed(&mi_arena_count);
  for (size_t i = 0; i < max_arena; i++) {
    mi_arena_t* arena = mi_atomic_load_ptr_relaxed(mi_arena_t, &mi_arenas[i]);
    if (arena != NULL && mi_arena_start(arena) <= (const uint8_t*)p && mi_arena_start(arena) + mi_size_of_blocks(arena->block_count) > (const uint8_t*)p) {
      return true;
    }
  }
  return false;
}


/* -----------------------------------------------------------
  Add an arena.
----------------------------------------------------------- */

static bool mi_arena_add(mi_arena_t* arena, mi_arena_id_t* arena_id, mi_stats_t* stats) {
  mi_assert_internal(arena != NULL);
  mi_assert_internal(arena->block_count > 0);
  if (arena_id != NULL) { *arena_id = -1; }

  size_t i = mi_atomic_increment_acq_rel(&mi_arena_count);
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

static bool mi_manage_os_memory_ex2(void* start, size_t size, bool is_large, int numa_node, bool exclusive, mi_memid_t memid, mi_arena_id_t* arena_id) mi_attr_noexcept
{
  mi_assert(!is_large || memid.initially_committed && memid.is_pinned);
  mi_assert(_mi_is_aligned(start,MI_ARENA_BLOCK_SIZE));
  mi_assert(start!=NULL);
  if (start==NULL) return false;
  if (!_mi_is_aligned(start,MI_ARENA_BLOCK_SIZE)) {
    // todo: use alignment in memid to align to blocksize first?
    _mi_warning_message("cannot use OS memory since it is not aligned to %zu KiB (address %p)", MI_ARENA_BLOCK_SIZE/MI_KiB, start);
    return false;
  }

  if (arena_id != NULL) { *arena_id = _mi_arena_id_none(); }

  const size_t info_blocks = mi_arena_info_blocks();
  const size_t bcount      = size / MI_ARENA_BLOCK_SIZE;  // divide down
  if (bcount < info_blocks+1) {
    _mi_warning_message("cannot use OS memory since it is not large enough (size %zu KiB, minimum required is %zu KiB)", size/MI_KiB, mi_size_of_blocks(info_blocks+1)/MI_KiB);
    return false;
  }
  if (bcount > MI_BITMAP_MAX_BITS) {
    // todo: allow larger areas (either by splitting it up in arena's or having larger arena's)
    _mi_warning_message("cannot use OS memory since it is too large (size %zu MiB, maximum is %zu MiB)", size/MI_MiB, mi_size_of_blocks(MI_BITMAP_MAX_BITS)/MI_MiB);
    return false;
  }
  mi_arena_t* arena = (mi_arena_t*)start;

  // commit & zero if needed
  bool is_zero = memid.initially_zero;
  if (!memid.initially_committed) {
    _mi_os_commit(arena, mi_size_of_blocks(info_blocks), &is_zero, &_mi_stats_main);
  }
  if (!is_zero) {
    _mi_memzero(arena, mi_size_of_blocks(info_blocks));
  }

  // init
  arena->id           = _mi_arena_id_none();
  arena->memid        = memid;
  arena->exclusive    = exclusive;
  arena->block_count  = bcount;
  arena->numa_node    = numa_node; // TODO: or get the current numa node if -1? (now it allows anyone to allocate on -1)
  arena->is_large     = is_large;
  arena->purge_expire = 0;
  mi_lock_init(&arena->abandoned_visit_lock);

  // init bitmaps
  mi_bitmap_init(&arena->blocks_free,true);
  mi_bitmap_init(&arena->blocks_committed,true);
  mi_bitmap_init(&arena->blocks_dirty,true);
  mi_bitmap_init(&arena->blocks_purge,true);
  for( int i = 0; i < MI_ARENA_BIN_COUNT; i++) {
    mi_bitmap_init(&arena->blocks_abandoned[i],true);
  }

  // reserve our meta info (and reserve blocks outside the memory area)
  mi_bitmap_unsafe_xsetN(MI_BIT_SET, &arena->blocks_free, info_blocks /* start */, arena->block_count - info_blocks);
  if (memid.initially_committed) {
    mi_bitmap_unsafe_xsetN(MI_BIT_SET, &arena->blocks_committed, 0, arena->block_count);
  }
  else {
    mi_bitmap_xsetN(MI_BIT_SET, &arena->blocks_committed, 0, info_blocks, NULL);
  }
  mi_bitmap_xsetN(MI_BIT_SET, &arena->blocks_dirty, 0, info_blocks, NULL);

  return mi_arena_add(arena, arena_id, &_mi_stats_main);
}


bool mi_manage_os_memory_ex(void* start, size_t size, bool is_committed, bool is_large, bool is_zero, int numa_node, bool exclusive, mi_arena_id_t* arena_id) mi_attr_noexcept {
  mi_memid_t memid = _mi_memid_create(MI_MEM_EXTERNAL);
  memid.initially_committed = is_committed;
  memid.initially_zero = is_zero;
  memid.is_pinned = is_large;
  return mi_manage_os_memory_ex2(start, size, is_large, numa_node, exclusive, memid, arena_id);
}

// Reserve a range of regular OS memory
int mi_reserve_os_memory_ex(size_t size, bool commit, bool allow_large, bool exclusive, mi_arena_id_t* arena_id) mi_attr_noexcept {
  if (arena_id != NULL) *arena_id = _mi_arena_id_none();
  size = _mi_align_up(size, MI_ARENA_BLOCK_SIZE); // at least one block
  mi_memid_t memid;
  void* start = _mi_os_alloc_aligned(size, MI_ARENA_BLOCK_ALIGN, commit, allow_large, &memid, &_mi_stats_main);
  if (start == NULL) return ENOMEM;
  const bool is_large = memid.is_pinned; // todo: use separate is_large field?
  if (!mi_manage_os_memory_ex2(start, size, is_large, -1 /* numa node */, exclusive, memid, arena_id)) {
    _mi_os_free_ex(start, size, commit, memid, &_mi_stats_main);
    _mi_verbose_message("failed to reserve %zu KiB memory\n", _mi_divide_up(size, 1024));
    return ENOMEM;
  }
  _mi_verbose_message("reserved %zu KiB memory%s\n", _mi_divide_up(size, 1024), is_large ? " (in large os pages)" : "");
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

static size_t mi_debug_show_bitmap(const char* prefix, const char* header, size_t block_count, mi_bitmap_t* bitmap) {
  _mi_verbose_message("%s%s:\n", prefix, header);
  size_t bit_count = 0;
  size_t bit_set_count = 0;
  for (int i = 0; i < MI_BFIELD_BITS && bit_count < block_count; i++) {
    char buf[MI_BITMAP_CHUNK_BITS + 1];
    mi_bitmap_chunk_t* chunk = &bitmap->chunks[i];
    for (int j = 0; j < MI_BITMAP_CHUNK_FIELDS; j++) {
      if (bit_count < block_count) {
        bit_set_count += mi_debug_show_bfield(chunk->bfields[j], buf + j*MI_BFIELD_BITS);
      }
      else {
        _mi_memset(buf + j*MI_BFIELD_BITS, ' ', MI_BFIELD_BITS);
      }
      bit_count += MI_BFIELD_BITS;
    }
    buf[MI_BITMAP_CHUNK_BITS] = 0;
    _mi_verbose_message("%s  %s\n", prefix, buf);
  }
  _mi_verbose_message("%s  total ('x'): %zu\n", prefix, bit_set_count);
  return bit_set_count;
}

void mi_debug_show_arenas(bool show_inuse, bool show_abandoned, bool show_purge) mi_attr_noexcept {
  MI_UNUSED(show_abandoned);
  size_t max_arenas = mi_atomic_load_relaxed(&mi_arena_count);
  size_t free_total = 0;
  size_t block_total = 0;
  //size_t abandoned_total = 0;
  size_t purge_total = 0;
  for (size_t i = 0; i < max_arenas; i++) {
    mi_arena_t* arena = mi_atomic_load_ptr_relaxed(mi_arena_t, &mi_arenas[i]);
    if (arena == NULL) break;
    block_total += arena->block_count;
    _mi_verbose_message("arena %zu: %zu blocks%s\n", i, arena->block_count, (arena->memid.is_pinned ? ", pinned" : ""));
    if (show_inuse) {
      free_total += mi_debug_show_bitmap("  ", "free blocks", arena->block_count, &arena->blocks_free);
    }
    mi_debug_show_bitmap("  ", "committed blocks", arena->block_count, &arena->blocks_committed);
    // todo: abandoned blocks
    if (show_purge) {
      purge_total += mi_debug_show_bitmap("  ", "purgeable blocks", arena->block_count, &arena->blocks_purge);
    }
  }
  if (show_inuse)     _mi_verbose_message("total inuse blocks    : %zu\n", block_total - free_total);
  // if (show_abandoned) _mi_verbose_message("total abandoned blocks: %zu\n", abandoned_total);
  if (show_purge)     _mi_verbose_message("total purgeable blocks: %zu\n", purge_total);
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
    _mi_os_free(p, hsize, memid, &_mi_stats_main);
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
  Abandoned pages
----------------------------------------------------------- */

void mi_arena_page_abandon(mi_page_t* page) {
  mi_assert_internal(mi_page_is_abandoned(page));
  if (mi_page_is_full(page)) {}
}



/* -----------------------------------------------------------
  Arena purge
----------------------------------------------------------- */

static long mi_arena_purge_delay(void) {
  // <0 = no purging allowed, 0=immediate purging, >0=milli-second delay
  return (mi_option_get(mi_option_purge_delay) * mi_option_get(mi_option_arena_purge_mult));
}

// reset or decommit in an arena and update the committed/decommit bitmaps
// assumes we own the area (i.e. blocks_free is claimed by us)
static void mi_arena_purge(mi_arena_t* arena, size_t block_idx, size_t blocks, mi_stats_t* stats) {
  mi_assert_internal(!arena->memid.is_pinned);
  const size_t size = mi_size_of_blocks(blocks);
  void* const p = mi_arena_block_start(arena, block_idx);
  bool needs_recommit;
  if (mi_bitmap_is_xsetN(MI_BIT_SET, &arena->blocks_committed, block_idx, blocks)) {
    // all blocks are committed, we can purge freely
    needs_recommit = _mi_os_purge(p, size, stats);
  }
  else {
    // some blocks are not committed -- this can happen when a partially committed block is freed
    // in `_mi_arena_free` and it is conservatively marked as uncommitted but still scheduled for a purge
    // we need to ensure we do not try to reset (as that may be invalid for uncommitted memory),
    // and also undo the decommit stats (as it was already adjusted)
    mi_assert_internal(mi_option_is_enabled(mi_option_purge_decommits));
    needs_recommit = _mi_os_purge_ex(p, size, false /* allow reset? */, stats);
    if (needs_recommit) { _mi_stat_increase(&_mi_stats_main.committed, size); }
  }

  // clear the purged blocks
  mi_bitmap_xsetN(MI_BIT_CLEAR, &arena->blocks_purge, blocks, block_idx, NULL);

  // update committed bitmap
  if (needs_recommit) {
    mi_bitmap_xsetN(MI_BIT_CLEAR, &arena->blocks_committed, blocks, block_idx, NULL);
  }
}


// Schedule a purge. This is usually delayed to avoid repeated decommit/commit calls.
// Note: assumes we (still) own the area as we may purge immediately
static void mi_arena_schedule_purge(mi_arena_t* arena, size_t block_idx, size_t blocks, mi_stats_t* stats) {
  const long delay = mi_arena_purge_delay();
  if (delay < 0) return;  // is purging allowed at all?

  if (_mi_preloading() || delay == 0) {
    // decommit directly
    mi_arena_purge(arena, block_idx, blocks, stats);
  }
  else {
    // schedule decommit
    _mi_error_message(EFAULT, "purging not yet implemented\n");
  }
}


static void mi_arenas_try_purge(bool force, bool visit_all, mi_stats_t* stats) {
  if (_mi_preloading() || mi_arena_purge_delay() <= 0) return;  // nothing will be scheduled

  const size_t max_arena = mi_atomic_load_acquire(&mi_arena_count);
  if (max_arena == 0) return;

  // _mi_error_message(EFAULT, "purging not yet implemented\n");
  MI_UNUSED(stats);
  MI_UNUSED(visit_all);
  MI_UNUSED(force);
}


/* -----------------------------------------------------------
  Special static area for mimalloc internal structures
  to avoid OS calls (for example, for the subproc metadata (~= 721b))
----------------------------------------------------------- */

#define MI_ARENA_STATIC_MAX  ((MI_INTPTR_SIZE/2)*MI_KiB)  // 4 KiB on 64-bit

static mi_decl_cache_align uint8_t mi_arena_static[MI_ARENA_STATIC_MAX];  // must be cache aligned, see issue #895
static mi_decl_cache_align _Atomic(size_t)mi_arena_static_top;

static void* mi_arena_static_zalloc(size_t size, size_t alignment, mi_memid_t* memid) {
  *memid = _mi_memid_none();
  if (size == 0 || size > MI_ARENA_STATIC_MAX) return NULL;
  const size_t toplow = mi_atomic_load_relaxed(&mi_arena_static_top);
  if ((toplow + size) > MI_ARENA_STATIC_MAX) return NULL;

  // try to claim space
  if (alignment < MI_MAX_ALIGN_SIZE) { alignment = MI_MAX_ALIGN_SIZE; }
  const size_t oversize = size + alignment - 1;
  if (toplow + oversize > MI_ARENA_STATIC_MAX) return NULL;
  const size_t oldtop = mi_atomic_add_acq_rel(&mi_arena_static_top, oversize);
  size_t top = oldtop + oversize;
  if (top > MI_ARENA_STATIC_MAX) {
    // try to roll back, ok if this fails
    mi_atomic_cas_strong_acq_rel(&mi_arena_static_top, &top, oldtop);
    return NULL;
  }

  // success
  *memid = _mi_memid_create(MI_MEM_STATIC);
  memid->initially_zero = true;
  const size_t start = _mi_align_up(oldtop, alignment);
  uint8_t* const p = &mi_arena_static[start];
  _mi_memzero_aligned(p, size);
  return p;
}

void* _mi_arena_meta_zalloc(size_t size, mi_memid_t* memid) {
  *memid = _mi_memid_none();

  // try static
  void* p = mi_arena_static_zalloc(size, MI_MAX_ALIGN_SIZE, memid);
  if (p != NULL) return p;

  // or fall back to the OS
  p = _mi_os_alloc(size, memid, &_mi_stats_main);
  if (p == NULL) return NULL;

  // zero the OS memory if needed
  if (!memid->initially_zero) {
    _mi_memzero_aligned(p, size);
    memid->initially_zero = true;
  }
  return p;
}

void _mi_arena_meta_free(void* p, mi_memid_t memid, size_t size) {
  if (mi_memkind_is_os(memid.memkind)) {
    _mi_os_free(p, size, memid, &_mi_stats_main);
  }
  else {
    mi_assert(memid.memkind == MI_MEM_STATIC);
  }
}

bool mi_abandoned_visit_blocks(mi_subproc_id_t subproc_id, int heap_tag, bool visit_blocks, mi_block_visit_fun* visitor, void* arg) {
  MI_UNUSED(subproc_id); MI_UNUSED(heap_tag); MI_UNUSED(visit_blocks); MI_UNUSED(visitor); MI_UNUSED(arg);
  _mi_error_message(EINVAL, "implement mi_abandon_visit_blocks\n");
  return false;
}

