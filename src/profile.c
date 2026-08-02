/* ----------------------------------------------------------------------------
Copyright (c) 2018-2024, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Heap profiler implementation.
//
// Thread safety:
//   _mi_profiler.enabled          — _Atomic(bool).
//                                   Written with release order in mi_profiler_enable.
//                                   Read with relaxed order in the inline fast paths
//                                   (internal.h); acquire order in the slow path.
//                                   Any thread observing enabled=true
//                                   is guaranteed to see on_alloc, on_free,
//                                   and record_extra_bytes written before the
//                                   release store (C11 release-acquire).
//                                   enabled is never set back to false.
//   _mi_profiler.on_alloc/on_free,
//   _mi_profiler.record_extra_bytes — non-atomic; only read after observing
//                                   enabled=true via an acquire load.
//   tld->profiler.*               — owning thread only; no atomics needed.
//   page->metadata                — owning thread only for all accesses:
//                                     alloc:        _mi_profiler_on_alloc (owning thread)
//                                     local free:   _mi_profiler_on_free_local (owning thread)
//                                     collected free: _mi_profiler_on_free_collected called from
//                                       _mi_page_thread_free_collect on the owning thread
//                                       after the atomic claim of xthread_free completes.

#include "mimalloc.h"
#include "mimalloc/internal.h"

// On sampling rate and has_metadata:
//
// The on_alloc callback controls the sampling rate by returning the next
// threshold.  At a constant rate of 1 MiB: E[live records per page] =
// page_capacity / rate, so:
//   small pages  (64 KiB):  ~0.06 records → P(has_metadata) ≈  6%
//   medium pages (512 KiB): ~0.5 records  → P(has_metadata) ≈ 39%
//
// The has_metadata bit (checked on every free) is therefore a reliable
// fast-path skip for small pages but not for medium pages.  A rate of
// ~5 MiB would bring medium pages below 10%, at the cost of coarser
// profiling resolution.
//
// A future improvement would be adaptive per-size-class sampling: separate
// (bytes_since_sample, next_threshold) pairs per size class in the tld,
// with bytes from each class counted independently.  This makes has_metadata
// unlikely across all classes without biasing the profile.

// ------------------------------------------------------------------
// Global state
// ------------------------------------------------------------------

mi_decl_cache_align mi_profiler_t _mi_profiler = { .enabled = false, .on_alloc = NULL, .on_free = NULL, .record_extra_bytes = 0 };

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

bool mi_profiler_enable(size_t record_extra_bytes,
                        mi_profiler_alloc_cb on_alloc, mi_profiler_free_cb on_free) {
  if (on_alloc == NULL) return false;
  if (on_free == NULL && record_extra_bytes > 0) return false;  // user_data would always be NULL
  if (record_extra_bytes > (SIZE_MAX - sizeof(mi_alloc_record_t))) return false;  // overflow in mi_record_alloc

  // Claim exclusive write rights to the non-atomic fields.  The CAS ensures
  // that if two threads call enable concurrently only one proceeds; the other
  // returns false without touching the fields (avoiding a data race).
  static _Atomic(bool) s_claimed;
  bool expected = false;
  if (!mi_atomic_cas_strong_acq_rel(&s_claimed, &expected, true)) return false;

  _mi_profiler.record_extra_bytes = record_extra_bytes;
  _mi_profiler.on_alloc           = on_alloc;
  _mi_profiler.on_free            = on_free;
  // Release store: any thread that subsequently observes enabled=true via an
  // acquire load is guaranteed to see all the fields written above.
  mi_atomic_store_release(&_mi_profiler.enabled, true);
  return true;
}

// ------------------------------------------------------------------
// Record memory management
//
// Each record is sizeof(mi_alloc_record_t) + record_extra_bytes bytes.
// The extra bytes (user_data) are passed directly to the callbacks
// but are not initialized; the callback must set any fields it will read.
//
// Records are allocated from the same mimalloc heap as the sampled
// object by calling _mi_heap_malloc_zero from inside the allocator.
// This is safe because:
//   1. The profiler hook fires at the end of _mi_page_malloc_zero, after
//      page->free and page->used have already been updated.  The page is
//      in a fully consistent state before mi_record_alloc is entered.
//   2. in_profiler is set to true before calling _mi_heap_malloc_zero,
//      so the inline fast path returns immediately if the record
//      allocation itself triggers the threshold check, preventing
//      infinite recursion.
//   3. mimalloc holds no locks at the point the hook fires; all
//      allocation state is thread-local, so there is nothing to
//      deadlock against.
// Note: if the heap's free pages are exhausted, _mi_heap_malloc_zero
// may fall through to _mi_malloc_generic and perform segment allocation
// or OS calls.  This only occurs on the slow path (when a sample is
// actually taken) so it does not affect steady-state allocation cost.
// ------------------------------------------------------------------

static mi_alloc_record_t* mi_record_alloc(mi_heap_t* heap) {
  size_t sz = sizeof(mi_alloc_record_t) + _mi_profiler.record_extra_bytes;
  heap->tld->profiler.in_profiler = true;
  mi_alloc_record_t* rec = (mi_alloc_record_t*)_mi_heap_malloc_zero(heap, sz, false);
  heap->tld->profiler.in_profiler = false;
  return rec;
}

static void mi_record_free(mi_heap_t* heap, mi_alloc_record_t* rec) {
  heap->tld->profiler.in_profiler = true;
  if (_mi_profiler.on_free != NULL) {
    void* user_data = (_mi_profiler.record_extra_bytes > 0) ? rec->user_data : NULL;
    _mi_profiler.on_free(user_data, rec->ptr);
  }
  mi_free(rec);
  heap->tld->profiler.in_profiler = false;
}

// ------------------------------------------------------------------
// Per-page record list
// ------------------------------------------------------------------

static void mi_page_record_push(mi_page_t* page, mi_alloc_record_t* rec) {
  rec->next         = page->metadata;
  page->metadata    = rec;
  page->has_metadata = true;
}

// Returns the matching record (removed from the list), or NULL.
// Linear search: at typical sampling rates the expected list length << 1.
static mi_alloc_record_t* mi_page_record_pop(mi_page_t* page, void* ptr) {
  mi_alloc_record_t** pp  = &page->metadata;
  mi_alloc_record_t*  rec = *pp;
  while (rec != NULL) {
    if (rec->ptr == ptr) {
      *pp = rec->next;
      if (page->metadata == NULL) { page->has_metadata = false; }
      return rec;
    }
    pp  = &rec->next;
    rec = *pp;
  }
  return NULL;
}

// ------------------------------------------------------------------
// Slow paths (noinline — only reached when work is needed)
// ------------------------------------------------------------------

void mi_decl_noinline _mi_profiler_on_alloc_slow(mi_heap_t* heap, mi_page_t* page, void* ptr, size_t size) {
  // Acquire load here (not in the fast path) so that on_alloc, on_free, and
  // record_extra_bytes are visible before we read them below.
  if mi_unlikely(!mi_atomic_load_acquire(&_mi_profiler.enabled)) return;

  mi_profiler_tld_t* ptld = &heap->tld->profiler;
  size_t usable_size = mi_page_usable_block_size(page);

  size_t bytes_since_last_sample = ptld->bytes_since_sample;
  ptld->bytes_since_sample       = 0;

  // on_alloc is non-NULL (asserted in mi_profiler_enable) and visible via the
  // acquire load above.  The callback returns the next threshold.
  if (_mi_profiler.on_free != NULL) {
    // Allocate a record to carry user_data from on_alloc to on_free.
    mi_alloc_record_t* rec = mi_record_alloc(heap);
    if mi_unlikely(rec == NULL) return;  // OOM: skip this sample
    rec->ptr = ptr;
    void* user_data = (_mi_profiler.record_extra_bytes > 0) ? rec->user_data : NULL;
    ptld->next_threshold = _mi_profiler.on_alloc(user_data, ptr, size, usable_size, ptld->next_threshold, bytes_since_last_sample, heap->tag);
    mi_page_record_push(page, rec);
  } else {
    // No on_free: no record needed; user_data is NULL.
    ptld->next_threshold = _mi_profiler.on_alloc(NULL, ptr, size, usable_size, ptld->next_threshold, bytes_since_last_sample, heap->tag);
  }
}

void mi_decl_noinline _mi_profiler_on_free_local_slow(mi_page_t* page, void* ptr) {
  mi_heap_t* heap = mi_page_heap(page);
  // Guard against re-entry: mi_record_free calls mi_free to release the record
  // itself, which would re-enter this hook.  The record object is never sampled
  // (in_profiler suppresses sampling during record allocation too), so skipping
  // it here is correct.
  if (heap->tld->profiler.in_profiler) return;

  mi_alloc_record_t* rec = mi_page_record_pop(page, ptr);
  if mi_unlikely(rec != NULL) {
    mi_record_free(heap, rec);
  }
}

void mi_decl_noinline _mi_profiler_on_free_collected_slow(mi_page_t* page, mi_block_t* head) {
  mi_heap_t* heap = mi_page_heap(page);
  // Same re-entry guard as _mi_profiler_on_free_local_slow.
  if (heap->tld->profiler.in_profiler) return;

  mi_block_t* block = head;
  while (block != NULL) {
    mi_block_t* next = mi_block_next(page, block);

    mi_alloc_record_t* rec = mi_page_record_pop(page, (void*)block);
    if mi_unlikely(rec != NULL) {
      mi_record_free(heap, rec);
    }
    if (page->metadata == NULL) break;  // no more records; skip remaining blocks

    block = next;
  }
}
