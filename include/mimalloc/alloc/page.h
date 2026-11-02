/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_PAGE_H
#define MI_INTERNAL_PAGE_H

#include "../types.h"
#include "../prim/os.h"
#include "../heap/heap.h"
#include "../heap/page-map.h"
#include "../prim/prim-pac.h"
#include "../arena/arena.h"
#include "../track.h"

mi_decl_restrict void* _mi_malloc_generic(mi_theap_t* theap, size_t size, size_t zero_huge_alignment, mi_page_t** ppage) mi_attr_noexcept mi_attr_malloc;
mi_decl_restrict void* _mi_malloc_generic_no_sample(mi_theap_t* theap, size_t size, bool zero, mi_page_t** ppage) mi_attr_noexcept mi_attr_malloc;

void          _mi_page_retire(mi_page_t* page) mi_attr_noexcept;       // free the page if there are no other pages with many free blocks
void          _mi_page_unfull(mi_page_t* page);
bool          _mi_page_free(mi_page_t* page, mi_page_queue_t* pq);     // false if page release is delayed
void          _mi_page_abandon(mi_page_t* page, mi_page_queue_t* pq);  // abandon the page, to be picked up by another thread...
void          _mi_deferred_free(mi_theap_t* theap, bool force);
bool          _mi_page_free_collect(mi_page_t* page, bool force);  // returns `true` if cross-thread free'd blocks were collected
mi_block_t*   _mi_page_free_collect_partly(mi_page_t* page, mi_block_t* head);
mi_decl_nodiscard bool _mi_page_init(mi_theap_t* theap, mi_page_t* page);
void          _mi_page_update_stats(mi_page_t* page);

void          _mi_theap_collect_retired(mi_theap_t* theap, bool force);
bool          _mi_theap_page_reclaim(mi_theap_t* theap, mi_page_t* page);  // true if an empty page was freed
#if MI_DEBUG>1
bool          _mi_page_is_valid(mi_page_t* page);
#endif

// in page-queue.c
bool          _mi_page_queue_is_valid(mi_theap_t* theap, const mi_page_queue_t* pq);
size_t        _mi_page_stats_bin(const mi_page_t* page); // for stats
size_t        _mi_bin_size(size_t bin);                  // for stats
size_t        _mi_bin(size_t size);                      // for stats


static inline mi_page_queue_t* mi_page_queue(const mi_theap_t* theap, size_t size) {
  mi_page_queue_t* const pq = &((mi_theap_t*)theap)->pages[_mi_bin(size)];
  if (size <= MI_LARGE_MAX_OBJ_SIZE) { mi_assert_internal(pq->block_size <= MI_LARGE_MAX_OBJ_SIZE); }
  return pq;
}

// Get the block size of a page
static inline size_t mi_page_block_size(const mi_page_t* page) {
  mi_assert_internal(page->block_size > 0);
  return page->block_size;
}

// Page start
static inline uint8_t* mi_page_start(const mi_page_t* page) {
  return page->page_start;
}

static inline size_t mi_page_size(const mi_page_t* page) {
  return mi_page_block_size(page) * page->reserved;
}

static inline uint8_t* mi_page_area(const mi_page_t* page, size_t* size) {
  if (size) { *size = mi_page_size(page); }
  return mi_page_start(page);
}

static inline size_t mi_page_info_size(void) {
  return mi_align_up(sizeof(mi_page_t), MI_MAX_ALIGN_SIZE);
}

// Can the first block in a page with blocks of size `bsize` have a cache offset? (see `MI_PAGE_CACHE_OFFSET`)
static inline bool mi_block_size_has_cache_offset(size_t bsize) {
  return (MI_PAGE_CACHE_OFFSET != 0 && bsize > MI_SMALL_MAX_OBJ_SIZE);
}

// Is a (non-small) block of size `bsize` allocated in a medium page?
// Blocks of 1/8th of a medium page (64 KiB) leave no space for a cache offset and use a large page instead.
static inline bool mi_block_size_is_medium(size_t bsize) {
  #if MI_ENABLE_LARGE_PAGES
  if (mi_block_size_has_cache_offset(bsize) && bsize == MI_MEDIUM_PAGE_SIZE/8) return false;
  #endif
  return (bsize <= MI_MEDIUM_MAX_OBJ_SIZE);
}

// The alignment that every block of size `bsize` is guaranteed to have in any page (or 0 if `bsize==0`).
// This is guaranteed by the block start (see `arena/arena-page.c:mi_page_meta_in_front_block_start` and `mi_page_meta_select`),
// and used by `mi_malloc_aligned` to avoid over-allocation for naturally aligned blocks.
static inline size_t mi_natural_alignment_of_size(size_t bsize) {
  const size_t lowbit = bsize & (~bsize + 1);  // largest power of 2 that divides `bsize`
  if (mi_is_power_of_two(bsize) && bsize <= MI_PAGE_MAX_START_BLOCK_ALIGN2) return bsize;   // power-of-two blocks up to 4KiB
  if (bsize <= MI_SMALL_SIZE_MAX) return lowbit;                                            // small blocks start at a multiple of their size
  if ((bsize % MI_PAGE_OSPAGE_BLOCK_ALIGN2) == 0) {                                         // multiples of 4KiB are 4KiB aligned (or cache offset unit aligned)
    return (mi_block_size_has_cache_offset(bsize) ? MI_PAGE_CACHE_OFFSET_UNIT : MI_PAGE_OSPAGE_BLOCK_ALIGN2);
  }
  return (lowbit < MI_MAX_ALIGN_SIZE ? lowbit : MI_MAX_ALIGN_SIZE);                         // otherwise blocks start at least `MI_MAX_ALIGN_SIZE` aligned
}

static inline bool mi_page_contains_address(const mi_page_t* page, const void* p) {
  size_t psize;
  const mi_addr_t start = mi_addr(mi_page_area(page, &psize));
  return (start <= mi_addr(p) && mi_addr(p) < start + psize);
}

static inline bool mi_page_is_in_arena(const mi_page_t* page) {
  return (page->memid.memkind == MI_MEM_ARENA);
}

static inline bool mi_page_is_singleton(const mi_page_t* page) {
  return (page->reserved == 1);
}

// Get the usable block size of a page without fixed padding.
// This may still include internal padding due to alignment and rounding up size classes.
static inline size_t mi_page_usable_block_size(const mi_page_t* page) {
  return mi_page_block_size(page) - MI_PADDING_SIZE;
}

static inline bool mi_page_meta_is_separated(const mi_page_t* page) {
  #if MI_PAGE_META_IS_ALIGNED
    #if MI_PAGE_META_SMALL_IS_ALIGNED
    return (mi_addr(page) != mi_align_down(mi_addr(mi_page_start(page)), MI_ARENA_SLICE_ALIGN));
    #else
    MI_UNUSED_RELEASE(page);
    mi_assert_internal(mi_addr(page) != mi_align_down(mi_addr(mi_page_start(page)), MI_ARENA_SLICE_ALIGN));
    return true;
    #endif
  #elif MI_PAGE_META_OS_IS_SEPARATED && !MI_PAGE_META_SMALL_IS_ALIGNED
  MI_UNUSED_RELEASE(page);
  mi_assert_internal(mi_addr(page) != mi_align_down(mi_addr(mi_page_start(page)), MI_ARENA_SLICE_ALIGN));
  return true;
  #elif MI_PAGE_META_IS_SEPARATED
  // usually separated but can still be in front for direct OS allocations (due to size or alignment) or due to MI_PAGE_META_SMALL_IS_ALIGNED
  return (page->memid.memkind == MI_MEM_ARENA && mi_addr(page) != mi_align_down(mi_addr(mi_page_start(page)), MI_ARENA_SLICE_ALIGN));
  #else
  MI_UNUSED(page);
  return false;
  #endif
}

static inline uint8_t* mi_page_slice_start(const mi_page_t* page) {
  #if MI_CHERI
  // derive from the arena or OS memory as both the page meta data and the page start are bounded
  if (page->memid.memkind == MI_MEM_ARENA) {
    return mi_arena_start(mi_memid_arena(page->memid)) + ((size_t)page->memid.mem.arena.slice_index * MI_ARENA_SLICE_SIZE);
  }
  else {
    void* const slice_start = (mi_page_meta_is_separated(page) ? mi_align_down_ptr(mi_page_start(page), MI_ARENA_SLICE_ALIGN) : (void*)page);
    return (uint8_t*)mi_ptr_rederive(page->memid.mem.os.base, slice_start);
  }
  #else
  if (mi_page_meta_is_separated(page)) {
    // page meta info is at a separate location (at `arena->pages`)
    return (uint8_t*)mi_align_down_ptr(mi_page_start(page), MI_ARENA_SLICE_ALIGN);
  }
  else {
    // page meta info is at the start of the page slices (without its memory tag)
    return (uint8_t*)mi_ptr_memtag_meta(page);
  }
  #endif
}

// Re-derive `p` in the page memory with the authority to change its protection
static inline void* mi_page_vmem_ptr(const mi_page_t* page, void* p) {
  #if MI_CHERI
  return mi_ptr_rederive(mi_page_slice_start(page), p);
  #else
  MI_UNUSED(page);
  return p;
  #endif
}

// This gives the offset relative to the start slice of a page.
static inline size_t mi_page_slice_offset_of(const mi_page_t* page, size_t offset_relative_to_page_start) {
  return (mi_page_start(page) - mi_page_slice_start(page)) + offset_relative_to_page_start;
}

// The end of the blocks relative to the slice start rounded up to the OS page size.
// This is where the end guard page is if `page->memid.guard_end` is set.
static inline size_t mi_page_guard_end_offset(const mi_page_t* page) {
  return mi_align_up(mi_page_slice_offset_of(page, mi_page_size(page)), _mi_os_page_size());
}

// How much of the page is committed relative to the slice start? (or 0 if fully committed already)
static inline size_t mi_page_slice_committed(const mi_page_t* page) {
  return ((size_t)page->slice_pcommitted * _mi_os_page_size());
}

// Currently committed part of a page
static inline size_t mi_page_committed(const mi_page_t* page) {
  const size_t slice_committed = mi_page_slice_committed(page);
  return (slice_committed == 0 ? mi_page_size(page) : slice_committed - mi_page_slice_offset_of(page,0));
}

static inline size_t mi_page_used(const mi_page_t* page) {
  mi_assert_internal(page != NULL);
  return mi_xused_used_count(page->xused);
}

static inline void mi_page_used_reset(mi_page_t* page) {
  page->xused = mi_xused_used_reset(page->xused);
}

static inline size_t mi_page_alloc_count(const mi_page_t* page) {
  return mi_xused_alloc_count(page->xused);
}

static inline size_t mi_page_last_used(const mi_page_t* page) {
  #if MI_SIZE_SIZE >= 8
  return (page->xused.used_alloc >> 32) & 0xFFFF;
  #else
  return page->xlast_used;
  #endif
}

static inline size_t mi_page_last_alloc(const mi_page_t* page) {
  #if MI_SIZE_SIZE >= 8
  return (page->xused.used_alloc >> 48) & 0xFFFF;
  #else
  return page->xlast_alloc;
  #endif
}


// are all blocks in a page freed?
// note: needs up-to-date used count, (as the `xthread_free` list may not be empty). see `_mi_page_collect_free`.
static inline bool mi_page_all_free(const mi_page_t* page) {
  mi_assert_internal(page != NULL);
  return (mi_page_used(page)==0);
}

// are there immediately available blocks, i.e. blocks available on the free list.
static inline bool mi_page_immediate_available(const mi_page_t* page) {
  mi_assert_internal(page != NULL);
  return (page->free != NULL);
}


// is the page not yet used up to its reserved space?
static inline bool mi_page_is_expandable(const mi_page_t* page) {
  mi_assert_internal(page != NULL);
  mi_assert_internal(page->capacity <= page->reserved);
  return (page->capacity < page->reserved);
}


static inline bool mi_page_is_full(const mi_page_t* page) {
  const bool full = (page->reserved == mi_page_used(page));
  mi_assert_internal(!full || page->free == NULL);
  return full;
}

// is more than (1 - 1/n)'th of a page in use?
static inline bool mi_page_is_used_at_frac(const mi_page_t* page, size_t n) {
  if (page==NULL) return true;
  const size_t frac = page->reserved / n;
  return (page->reserved - mi_page_used(page) <= frac);
}

// is more than 7/8th of a page in use?
static inline bool mi_page_is_mostly_used(const mi_page_t* page) {
  return mi_page_is_used_at_frac(page, 8);
}

static inline bool mi_page_is_small(const mi_page_t* page) {
  return (page->block_size <= MI_SMALL_MAX_OBJ_SIZE && !mi_page_is_singleton(page));
}

static inline bool mi_page_is_huge(const mi_page_t* page) {
  return (mi_page_is_singleton(page) &&
          (page->block_size > MI_LARGE_MAX_OBJ_SIZE ||
           (mi_memkind_is_os(page->memid.memkind) && page->memid.mem.os.base < (void*)page)));
}

static inline size_t mi_page_min_commit_size(void) {
  const size_t psize = _mi_os_page_size();
  return (MI_PAGE_MIN_COMMIT_SIZE >= psize ? MI_PAGE_MIN_COMMIT_SIZE : psize);
}

//-----------------------------------------------------------
// Page thread id and flags
//-----------------------------------------------------------

// Thread id of thread that owns this page (with flags in the bottom 2 bits)
static inline mi_thread_id_t mi_page_xthread_id(const mi_page_t* page) {
  return mi_atomic_load_relaxed(&((mi_page_t*)page)->xthread_id);
}

// Plain thread id of the thread that owns this page
static inline mi_thread_id_t mi_page_thread_id(const mi_page_t* page) {
  return (mi_page_xthread_id(page) & ~MI_PAGE_FLAG_MASK);
}

static inline mi_page_flags_t mi_page_flags(const mi_page_t* page) {
  return (mi_page_xthread_id(page) & MI_PAGE_FLAG_MASK);
}

static inline bool mi_page_flags_set(mi_page_t* page, bool set, mi_page_flags_t newflag) {
  mi_page_flags_t old;
  if (set) { old = mi_atomic_or_relaxed(&page->xthread_id, newflag); }
      else { old = mi_atomic_and_relaxed(&page->xthread_id, ~newflag); }
  return ((old & newflag) == newflag);
}

static inline bool mi_page_is_in_full(const mi_page_t* page) {
  return ((mi_page_flags(page) & MI_PAGE_IN_FULL_QUEUE) != 0);
}

static inline void mi_page_set_in_full(mi_page_t* page, bool in_full) {
  const bool was_in_full = mi_page_flags_set(page, in_full, MI_PAGE_IN_FULL_QUEUE);
  if (was_in_full != in_full) {
    // optimize: maintain pages_full_size to avoid visiting the full queue (issue #1220)
    mi_theap_t* const theap = page->theap;
    mi_assert_internal(theap!=NULL);
    if (theap != NULL) {
      mi_assert_internal(page->capacity==page->reserved);
      const size_t size = page->reserved * mi_page_block_size(page);
      if (in_full) { theap->pages_full_size += size; }
              else { mi_assert_internal(size <= theap->pages_full_size); theap->pages_full_size -= size; }
    }
  }
}

static inline bool mi_page_has_interior_pointers(const mi_page_t* page) {
  return ((mi_page_flags(page) & MI_PAGE_HAS_INTERIOR_POINTERS) != 0);
}

static inline void mi_page_set_has_interior_pointers(mi_page_t* page, bool has_aligned) {
  mi_page_flags_set(page, has_aligned, MI_PAGE_HAS_INTERIOR_POINTERS);
}

// Are the blocks in this page memory tagged? (always false without `MI_MTE`)
static inline bool mi_page_is_memtag(const mi_page_t* page) {
  return ((mi_page_flags(page) & MI_PAGE_IS_MEMTAGGED) != 0);
}

// The tag class parity of the blocks in a memory tagged page: block `i` has the class of `i + parity` (see `prim-mte.h`).
// Adjacent pages must not share a tag class across their boundary, so a page starts with an even block (or a tag 0 gap)
// and ends with an odd block (or a tag 0 gap). When there is a (tag 0) cache offset gap in front of an odd number of
// blocks, we flip the parity such that the last block is odd. (A page without a front gap and an odd number of blocks
// that fills its slices exactly can only be a singleton (as regular page sizes are a power of two), which gets a tag 0
// unit at the end, see `mi_arenas_page_singleton_alloc`.)
// (note: with page meta data in front of the slices, which happens only for OS-allocated pages, the meta data has the odd class)
static inline size_t mi_page_memtag_parity(const mi_page_t* page) {
  return ((page->reserved & 1) != 0 && mi_page_meta_is_separated(page) && mi_page_start(page) != mi_page_slice_start(page) ? 1 : 0);
}

// Are freed blocks in this page zeroed (except for the free list link)? This holds for small memory tagged blocks
// (always false without `MI_MTE` or in debug mode as freed blocks are filled then)
static inline bool mi_page_is_zero_on_free(const mi_page_t* page) {
  #if (MI_DEBUG>0) && !MI_TRACK_ENABLED && !MI_TSAN
  MI_UNUSED(page);
  return false;
  #else
  return (mi_page_is_memtag(page) && mi_page_block_size(page) <= MI_MEMTAG_ZERO_MAX);
  #endif
}

#if MI_QSBR
// For qsbr, `qsbr_xthread_id` keeps the thread id of the last owning thread (so it is still available when the page is abandoned)
// The lowest bit is set if the page is in the qsbr queue of its theap. It is only written by the owning thread.
#define MI_PAGE_QSBR_IN_QUEUE  MI_ZU(0x01)

static inline mi_thread_id_t mi_page_qsbr_xthread_id(const mi_page_t* page) {
  return mi_atomic_load_relaxed(&((mi_page_t*)page)->qsbr_xthread_id);
}

static inline mi_thread_id_t mi_page_qsbr_thread_id(const mi_page_t* page) {
  return (mi_page_qsbr_xthread_id(page) & ~MI_PAGE_FLAG_MASK);
}

static inline void mi_page_qsbr_set_thread_id(mi_page_t* page, mi_thread_id_t tid) {
  mi_assert_internal((tid & MI_PAGE_FLAG_MASK) == 0);
  const mi_thread_id_t xtid = mi_page_qsbr_xthread_id(page);
  mi_atomic_store_relaxed(&page->qsbr_xthread_id, tid | (xtid & MI_PAGE_QSBR_IN_QUEUE));
}

static inline bool mi_page_qsbr_is_in_queue(const mi_page_t* page) {
  return ((mi_page_qsbr_xthread_id(page) & MI_PAGE_QSBR_IN_QUEUE) != 0);
}

static inline void mi_page_qsbr_set_in_queue(mi_page_t* page, bool in_queue) {
  const mi_thread_id_t xtid = mi_page_qsbr_xthread_id(page);
  mi_atomic_store_relaxed(&page->qsbr_xthread_id, (in_queue ? (xtid | MI_PAGE_QSBR_IN_QUEUE) : (xtid & ~MI_PAGE_QSBR_IN_QUEUE)));
}
#endif

static inline void mi_page_set_theap(mi_page_t* page, mi_theap_t* theap) {
  // mi_assert_internal(!mi_page_is_in_full(page));  // can happen when destroying pages on theap_destroy
  page->theap = theap;
  const mi_thread_id_t tid = (theap == NULL ? MI_THREAD_ID_ABANDONED : theap->tld->thread_id);
  mi_assert_internal((tid & MI_PAGE_FLAG_MASK) == 0);

  // Preserve flags set by concurrent aligned allocations.
  if (theap == NULL) {
    mi_atomic_and_release(&page->xthread_id, MI_PAGE_FLAG_MASK);
    return;
  }
  mi_thread_id_t xtid_old = mi_page_xthread_id(page);
  mi_thread_id_t xtid;
  do {
    xtid = tid | (xtid_old & MI_PAGE_FLAG_MASK);
  } while (!mi_atomic_cas_weak_release(&page->xthread_id, &xtid_old, xtid));

  #if MI_QSBR
  // keep the last owning thread id around for `mi_thread_id_of`
  if (tid > MI_THREAD_ID_DETACHED) { mi_page_qsbr_set_thread_id(page, tid); }
  #endif
}

static inline bool mi_page_is_abandoned(const mi_page_t* page) {
  // note: the xtheap field of an abandoned theap is set to the subproc (for fast reclaim-on-free)
  return (mi_page_thread_id(page) <= MI_THREAD_ID_ABANDONED_MAPPED);
}

static inline bool mi_page_is_abandoned_mapped(const mi_page_t* page) {
  return (mi_page_thread_id(page) == MI_THREAD_ID_ABANDONED_MAPPED);
}

static inline void mi_page_set_abandoned_mapped(mi_page_t* page) {
  mi_assert_internal(mi_page_is_abandoned(page));
  mi_atomic_or_relaxed(&page->xthread_id, (mi_thread_id_t)MI_THREAD_ID_ABANDONED_MAPPED);
}

static inline void mi_page_clear_abandoned_mapped(mi_page_t* page) {
  mi_assert_internal(mi_page_is_abandoned_mapped(page));
  mi_atomic_and_relaxed(&page->xthread_id, (mi_thread_id_t)MI_PAGE_FLAG_MASK);
}


static inline mi_theap_t* mi_page_theap(const mi_page_t* page) {
  mi_assert_internal(!mi_page_is_abandoned(page));
  mi_assert_internal(page->theap != NULL && !_mi_is_empty_theap(page->theap));
  return page->theap;
}

static inline mi_tld_t* mi_page_tld(const mi_page_t* page) {
  mi_assert_internal(!mi_page_is_abandoned(page));
  mi_assert_internal(page->theap != NULL);
  return page->theap->tld;
}


static inline mi_heap_t* mi_page_heap(const mi_page_t* page) {
  mi_heap_t* heap = page->heap;
  mi_assert_internal(heap != NULL);
  return heap;
}

static inline mi_subproc_t* mi_page_subproc(const mi_page_t* page) {
  mi_heap_t* const heap = mi_page_heap(page);
  return heap->subproc;
}

//-----------------------------------------------------------
// Thread free list and ownership
//-----------------------------------------------------------

// Thread free flag helpers
static inline mi_block_t* mi_tf_block(mi_thread_free_t tf) {
  return (mi_block_t*)(tf & ~1);
}

static inline bool mi_tf_is_owned(mi_thread_free_t tf) {
  return ((tf & 1) == 1);
}

static inline mi_thread_free_t mi_tf_create(mi_block_t* block, bool owned) {
  const uintptr_t base = (uintptr_t)block | (owned ? 1 : 0);
  return (mi_thread_free_t)base;
}

static inline mi_thread_free_t mi_tf_set_owned(mi_thread_free_t tf, bool owned) {
  return mi_tf_create(mi_tf_block(tf), owned);
}

// Thread free access
static inline mi_block_t* mi_page_thread_free(const mi_page_t* page) {
  return mi_tf_block(mi_atomic_load_relaxed(&((mi_page_t*)page)->xthread_free));
}

// are there any available blocks?
static inline bool mi_page_has_any_available(const mi_page_t* page) {
  mi_assert_internal(page != NULL && page->reserved > 0);
  return (mi_page_used(page) < page->reserved || (mi_page_thread_free(page) != NULL));
}

// Owned?
static inline bool mi_page_is_owned(const mi_page_t* page) {
  return mi_tf_is_owned(mi_atomic_load_relaxed(&((mi_page_t*)page)->xthread_free));
}

// get ownership; returns true if the page was not owned before.
static inline bool mi_page_claim_ownership(mi_page_t* page) {
  const uintptr_t old = mi_atomic_or_acq_rel(&page->xthread_free, (uintptr_t)1);
  return ((old&1)==0);
}

/* -----------------------------------------------------------
  Delayed page release
----------------------------------------------------------- */

static inline void mi_page_qsbr_unlink(mi_page_t* page) {
#if MI_QSBR
  if (!mi_page_qsbr_is_in_queue(page)) {
    mi_assert_internal(page->qsbr_next == NULL && page->qsbr_prev == NULL);
    return;
  }
  mi_theap_t* theap = mi_page_theap(page);
  if (page->qsbr_prev != NULL) { page->qsbr_prev->qsbr_next = page->qsbr_next; }
  else {
    mi_assert_internal(theap->qsbr_first == page);
    theap->qsbr_first = page->qsbr_next;
  }
  if (page->qsbr_next != NULL) { page->qsbr_next->qsbr_prev = page->qsbr_prev; }
  else {
    mi_assert_internal(theap->qsbr_last == page);
    theap->qsbr_last = page->qsbr_prev;
  }
  page->qsbr_next = page->qsbr_prev = NULL;
  mi_page_qsbr_set_in_queue(page, false);
#else
  MI_UNUSED(page);
#endif
}

static inline void mi_page_qsbr_enqueue(mi_page_t* page, mi_theap_t* theap) {
#if MI_QSBR
  mi_assert_internal(mi_page_theap(page) == theap);
  mi_assert_internal(mi_page_all_free(page));
  mi_assert_internal(mi_heap_qsbr_is_enabled(mi_page_heap(page)));
  mi_assert_internal(!mi_page_qsbr_is_clear(&page->qsbr));
  if (mi_page_qsbr_is_in_queue(page)) return;
  mi_assert_internal(page->qsbr_next == NULL && page->qsbr_prev == NULL);
  page->qsbr_prev = theap->qsbr_last;
  if (theap->qsbr_last != NULL) { theap->qsbr_last->qsbr_next = page; }
  else { theap->qsbr_first = page; }
  theap->qsbr_last = page;
  mi_page_qsbr_set_in_queue(page, true);
#else
  MI_UNUSED(page);
  MI_UNUSED(theap);
#endif
}

static inline void _mi_page_qsbr_clear(mi_page_t* page) {
#if MI_QSBR
  mi_page_qsbr_unlink(page);
  mi_page_qsbr_clear(&page->qsbr);
#else
  MI_UNUSED(page);
#endif
}

static inline bool _mi_page_qsbr_is_clear(const mi_page_t* page) {
#if MI_QSBR
  return (!mi_page_qsbr_is_in_queue(page) && page->qsbr_next == NULL && page->qsbr_prev == NULL &&
          mi_page_qsbr_is_clear(&page->qsbr));
#else
  MI_UNUSED(page);
  return true;
#endif
}

static inline bool _mi_page_qsbr_should_delay(mi_page_t* page) {
#if MI_QSBR
  mi_assert_internal(mi_page_is_owned(page) && mi_page_all_free(page));
  mi_heap_t* heap = mi_page_heap(page);
  if (!mi_heap_qsbr_is_enabled(heap)) return false;
  const bool delayed = mi_page_qsbr_should_delay(&page->qsbr, &heap->qsbr, page->capacity * mi_page_block_size(page));
  mi_assert_internal(!delayed || !mi_page_qsbr_is_clear(&page->qsbr));
  return delayed;
#else
  MI_UNUSED(page);
  return false;
#endif
}

/* -------------------------------------------------------------------
Encoding/Decoding the free list next pointers

This is to protect against buffer overflow exploits where the
free list is mutated. Many hardened allocators xor the next pointer `p`
with a secret key `k`, as `p^k`. This is weak: if the attacker can guess
`p` this reveals `k`, and xor-ing two entries cancels the key. A non-linear
encoding like `((p^k2)<<<k1)+k1` avoids this but can still be broken with
just a few known `p`.

Instead, each page has two random keys (`keys[0]` and `keys[1]`), and each
link is bound to the address `b` of the block it is stored in (so links
cannot be copied). We use one of three encodings:

1. Tagged offsets (on 64-bit platforms).
   We store the 32-bit page offset `v` of the next block (with `NULL` as
   the reserved offset `MI_BLOCK_OFS_NULL`) together with a keyed 32-bit tag:

     x      = (T(b,v) << 32) | (v ^ m)
     T(b,v) = let t = mulxor64(v ^ keys[0], b ^ keys[1]) in t ^ (t >> 32)

   where `mulxor64` xor's the low and high halves of the full 128-bit
   product, and the mask `m` (the upper 32 bits of `keys[0]`) hides the
   (randomized) free list order. Decoding is just an xor and an add; the tag
   is verified alongside with a predictable branch so the multiply mostly costs
   throughput. A corrupted or forged link validates with
   probability 2^-32, and it is difficult to recover the keys
   from known `(b,v,x)` triples (except for `m`), leaving ~96 bits of key.
   With `MI_PAC=WEAK` the process key `K` is also mixed in as
   `mulxor64(v ^ keys[0], b ^ keys[1] ^ K)` (see `prim/prim-pac.h:mi_keyed_mac`).

2. Authenticated offsets (with `MI_PAC=ON`).
   As the tagged offsets, but the tag is computed with the (unreadable)
   hardware pointer authentication key:

     x      = (T(b,v) << 32) | v
     T(b,v) = let t = pacga(v, b ^ keys[0]) in t | (t >> 32)

   The page keys need not be secret anymore: the offset is not masked, and
   `keys[0]` only serves as a nonce such that links from a previous use of the
   page do not validate. A forged link validates with probability 2^-32 even
   if the page keys are known.

3. Xor-shift (the default on 32-bit, where we lack a cheap wide multiply).
   We store the full next address `p` (with `NULL` encoded as the page address):

     x    = L(p - t) <<< keys[0]
     t    = L(b ^ keys[0]) + keys[1]
     L(y) = y ^ (y << (MI_SIZE_BITS/2))      (which is its own inverse)

   The tweak `t` binds the link to the block `b`, the xor-shift `L` diffuses
   low bits into the high bits (such that partial overwrites decode outside
   the page), and the secret rotation prevents linear combinations of
   additions and xor's. Decoded links are validated by checking that they
   are in the page.

The padding canary encodes `NULL` at the unaligned address `b|1` (so it can
never be used as a valid free list link).
------------------------------------------------------------------- */

static inline bool mi_is_in_same_page(const void* p, const void* q) {
  mi_page_t* page = mi_ptr_page(p);
  return mi_page_contains_address(page,q);
  // return (mi_ptr_page(p) == mi_ptr_page(q));
}

#if (MI_ENCODE_FREELIST || MI_PADDING)
#if MI_SIZE_BITS>=64  // tagged offsets

// On 64-bit, a free list link stores the offset `d` of the next block relative to the page start, masked with
// a key, and a keyed MAC over `d` and the address of the link in all other bits. With memory tagging, `d` includes
// the memory tag of the next block (at `MI_PTR_MEMTAG_MASK`, as the page start has tag 0) and we use a smaller
// offset to keep a longer MAC (pages with more than one block are at most `MI_LARGE_PAGE_SIZE`). Without memory
// tagging we use a 32-bit offset as that gives the best codegen (as the mask folds into the add, e.g. `uxtw` on arm64).
#define MI_BLOCK_OFS_BITS  (MI_PTR_MEMTAG_MASK==0 ? 32 : 24)
#define MI_BLOCK_OFS_MASK  (MI_PTR_MEMTAG_MASK | ((MI_ADDR(1) << MI_BLOCK_OFS_BITS) - 1))
#define MI_BLOCK_OFS_NULL  ((MI_ADDR(1) << MI_BLOCK_OFS_BITS) - 1)   // odd, so never a block offset

#if MI_LARGE_PAGE_SIZE >= (1UL<<24)
#error "the block offsets in a page with multiple blocks must fit in a free list link (see MI_BLOCK_OFS_BITS)"
#endif

#if MI_PAC>=2
static inline mi_addr_t mi_ptr_mac(const mi_page_t* page, mi_addr_t baddr, mi_addr_t d) {
  return mi_keyed_mac(d, baddr ^ page->keys[0]);
}
static inline mi_addr_t mi_ptr_ofs_key(const mi_page_t* page) {
  MI_UNUSED(page);
  return 0;
}
#else
static inline mi_addr_t mi_ptr_mac(const mi_page_t* page, mi_addr_t baddr, mi_addr_t d) {
  return mi_keyed_mac(d ^ page->keys[0], baddr ^ page->keys[1]);
}
static inline mi_addr_t mi_ptr_ofs_key(const mi_page_t* page) {
  return mi_rotl(page->keys[0], 32);
}
#endif

// Encode a pointer `p` (in the page, or NULL) to be stored at address `baddr`
static inline mi_encoded_t mi_ptr_encode(const mi_page_t* page, mi_addr_t baddr, const void* p) {
  mi_assert_internal(p==NULL || mi_page_contains_address(page,p));
  const mi_addr_t d = (p==NULL ? MI_BLOCK_OFS_NULL : mi_ptr_pack(p) - mi_addr(mi_page_start(page)));
  return (mi_encoded_t)(((d ^ mi_ptr_ofs_key(page)) & MI_BLOCK_OFS_MASK) | (mi_ptr_mac(page,baddr,d) & ~MI_BLOCK_OFS_MASK));
}

// Decode a value `x` stored at address `baddr`; `valid` is false if `x` was not a valid encoding
static inline void* mi_ptr_decode(const mi_page_t* page, mi_addr_t baddr, mi_encoded_t x, bool* valid) {
  const mi_addr_t e = (mi_addr_t)x;
  const mi_addr_t d = (e ^ mi_ptr_ofs_key(page)) & MI_BLOCK_OFS_MASK;
  *valid = (((e ^ mi_ptr_mac(page,baddr,d)) & ~MI_BLOCK_OFS_MASK) == 0);
  return (d == MI_BLOCK_OFS_NULL ? NULL : mi_page_start(page) + d);
}

#else

static inline mi_addr_t mi_xorshift_half(mi_addr_t y) {
  return (y ^ (y << (MI_SIZE_BITS/2)));
}

static inline mi_addr_t mi_ptr_tweak(const mi_page_t* page, mi_addr_t baddr) {
  return (mi_xorshift_half(baddr ^ page->keys[0]) + page->keys[1]);
}

// Encode a pointer `p` (in the page, or NULL) to be stored at address `baddr`
static inline mi_encoded_t mi_ptr_encode(const mi_page_t* page, mi_addr_t baddr, const void* p) {
  mi_assert_internal(p==NULL || mi_page_contains_address(page,p));
  const mi_addr_tagged_t addrt = (p==NULL ? (mi_addr_tagged_t)mi_addr(page) : mi_ptr_pack(p));
  return (mi_encoded_t)mi_rotl(mi_xorshift_half(addrt - mi_ptr_tweak(page,baddr)), page->keys[0]);
}

// Decode a value `x` stored at address `baddr`; `valid` is false if `x` was not a valid encoding
static inline void* mi_ptr_decode(const mi_page_t* page, mi_addr_t baddr, mi_encoded_t x, bool* valid) {
  const mi_addr_tagged_t addrt = mi_xorshift_half(mi_rotr((mi_addr_t)x, page->keys[0])) + mi_ptr_tweak(page,baddr);
  if (addrt == (mi_addr_tagged_t)mi_addr(page)) { *valid = true; return NULL; }
  void* const p = mi_ptr_unpack(mi_page_start(page), addrt);
  *valid = mi_page_contains_address(page, p);
  return p;
}

#endif

static inline uint32_t mi_ptr_encode_canary(const mi_page_t* page, const mi_block_t* block) {
  const mi_addr_t e = (mi_addr_t)mi_ptr_encode(page, mi_addr(block)|1, NULL);
  const uint32_t x = (uint32_t)(e ^ (e >> (MI_SIZE_BITS/2)));
  // make the lowest byte 0 to prevent spurious read overflows which could be a security issue (issue #951)
  // also clear bit 9 which we set only when a block is freed.
  #if MI_BIG_ENDIAN
  return (x & 0x00FFFEFF);
  #else
  return (x & 0xFFFFFE00);
  #endif
}

static inline uint32_t mi_ptr_encode_canary_freed(void) {
  return (0x00DEAD00);  // set bit 9 so it is different from any valid canary
}

static inline bool mi_ptr_decode_canary_is_freed(uint32_t canary) {
  return (canary == mi_ptr_encode_canary_freed());
}

#endif // MI_ENCODE_FREELIST || MI_PADDING

// Decode the free list link `x` stored in `block`; `valid` is false if the link was corrupted
static inline mi_block_t* mi_block_decode(const mi_page_t* page, const mi_block_t* block, mi_encoded_t x, bool* valid) {
  #if MI_ENCODE_FREELIST
  return (mi_block_t*)mi_ptr_decode(page, mi_addr(block), x, valid);
  #else
  MI_UNUSED(block);
  *valid = true;
  const mi_addr_tagged_t addrt = (mi_addr_tagged_t)x;
  return (addrt==0 ? NULL : (mi_block_t*)mi_ptr_unpack(mi_page_start(page), addrt));
  #endif
}

static inline mi_encoded_t mi_block_encode(const mi_page_t* page, const mi_block_t* block, const mi_block_t* next) {
  #if MI_ENCODE_FREELIST
  return mi_ptr_encode(page, mi_addr(block), next);
  #else
  MI_UNUSED(page); MI_UNUSED(block);
  return (mi_encoded_t)mi_ptr_pack(next);  // never store a capability in a (freed) block
  #endif
}

static inline mi_encoded_t mi_block_link(const mi_block_t* block) {
  mi_track_mem_defined(block,sizeof(mi_block_t));
  const mi_encoded_t x = block->next;
  mi_track_mem_noaccess(block,sizeof(mi_block_t));
  return x;
}

static inline mi_block_t* mi_block_next(const mi_page_t* page, const mi_block_t* block) {
  const mi_encoded_t x = mi_block_link(block);
  bool valid;
  mi_block_t* const next = mi_block_decode(page, block, x, &valid);
  if mi_unlikely(!valid) {
    return _mi_block_next_is_corrupted(page, block, x); // returns NULL
  }
  return next;
}

static inline void mi_block_set_next(const mi_page_t* page, mi_block_t* block, const mi_block_t* next) {
  mi_track_mem_undefined(block,sizeof(mi_block_t));
  block->next = mi_block_encode(page, block, next);
  mi_track_mem_noaccess(block,sizeof(mi_block_t));
}

#endif
