/* ----------------------------------------------------------------------------
Copyright (c) 2018-2024, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_PROFILE_H
#define MIMALLOC_PROFILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "mimalloc/atomic.h"

// Forward declarations — full types come from types.h / internal.h.
typedef struct mi_page_s    mi_page_t;
typedef struct mi_block_s   mi_block_t;
typedef struct mi_heap_s    mi_heap_t;

// ------------------------------------------------------------------
// Allocation record: one node per sampled live allocation, stored in
// a singly-linked list at page->metadata.  Opaque to callers; the
// callbacks receive user_data directly.
//
// `ptr`       — the sampled user pointer; used internally to match frees.
// `user_data` — flexible array member for caller-owned metadata.
//               The number of bytes available is
//               _mi_profiler.record_extra_bytes, set at
//               mi_profiler_enable() time.  Typical uses: a captured
//               stack trace, allocation size and weight for on_free,
//               a pointer to an external profiler node, or a timestamp.
//               The profiler does not initialize this region and never
//               reads it.
//               Alignment: user_data begins at offset sizeof(mi_alloc_record_t)
//               from the allocation base (2 * sizeof(void*): 16 bytes on 64-bit,
//               8 bytes on 32-bit), so it is suitably aligned for any scalar or
//               pointer type.  SIMD types requiring > 16-byte alignment are not
//               guaranteed to be aligned.
// ------------------------------------------------------------------
typedef struct mi_alloc_record_s {
  void*                     ptr;
  struct mi_alloc_record_s* next;
  char                      user_data[];  // length = _mi_profiler.record_extra_bytes
} mi_alloc_record_t;

// ------------------------------------------------------------------
// User-supplied callbacks.
//
// on_alloc:  called when a sample is taken.
//   `user_data`            — caller-owned region (record_extra_bytes bytes);
//                            may write anything here for use in on_free.
//                            NULL if record_extra_bytes is 0.
//   `ptr`                  — the sampled user pointer.
//   `requested_size`       — size passed by the caller to malloc/calloc/etc.
//   `usable_size`          — actual usable bytes after size-class rounding;
//                            reflects true memory consumption.
//   `threshold`            — the threshold (bytes) that triggered this sample.
//   `bytes_since_last_sample` — bytes accumulated since the last sample; the
//                            statistical weight of this sample.
//   `heap_tag`             — tag of the heap that made the allocation, set via
//                            mi_heap_new_ex().  Zero for the default heap.
//   Returns the number of bytes to accumulate before the next sample.
//   Returning 0 causes the next allocation to be sampled immediately.
//
// on_free:   called when a sampled allocation is freed.
//   `user_data` — the same region written during on_alloc.  Valid only
//                 for the duration of the callback; do not retain the pointer.
//   `ptr`       — the freed user pointer.
//   May be NULL if free-time notification is not needed.
// ------------------------------------------------------------------
typedef size_t (*mi_profiler_alloc_cb)(void* user_data, void* ptr, size_t requested_size, size_t usable_size, size_t threshold, size_t bytes_since_last_sample, uint8_t heap_tag);
typedef void   (*mi_profiler_free_cb)(void* user_data, void* ptr);

// ------------------------------------------------------------------
// Global profiler configuration.
//
// Profiling is one-way: once enabled it cannot be disabled.
//
// `enabled` is _Atomic(bool) so that mi_profiler_enable() can be called
// from any thread.  The store uses release order; reads in the inline
// fast-path hooks (in internal.h) use relaxed order (sufficient to decide
// whether to do any work); the slow path uses acquire order to ensure
// on_alloc, on_free, and record_extra_bytes are visible before they are read.
// ------------------------------------------------------------------
typedef struct mi_profiler_s {
  _Atomic(bool)            enabled;
  mi_profiler_alloc_cb     on_alloc;            // non-NULL when enabled=true
  mi_profiler_free_cb      on_free;             // may be NULL
  size_t                   record_extra_bytes;  // bytes allocated after each mi_alloc_record_t for user_data
} mi_profiler_t;

extern mi_profiler_t _mi_profiler;

// ------------------------------------------------------------------
// Public API — must be called at most once.  May be called from any
// thread, before or after other threads have started.  Each thread
// samples its first allocation immediately; the on_alloc callback
// controls all subsequent thresholds.
//
// Returns true on success, false if:
//   - profiling was already enabled (called more than once), or
//   - on_alloc is NULL, or
//   - on_free is NULL but record_extra_bytes > 0, or
//   - record_extra_bytes would overflow the record allocation size.
// ------------------------------------------------------------------
bool mi_profiler_enable(size_t record_extra_bytes, mi_profiler_alloc_cb on_alloc, mi_profiler_free_cb on_free);

// ------------------------------------------------------------------
// Slow-path implementations (defined in profile.c).
// The inline fast-path wrappers are in internal.h so they have
// access to the full type definitions they need.
// ------------------------------------------------------------------
void _mi_profiler_on_alloc_slow(mi_heap_t* heap, mi_page_t* page, void* ptr, size_t size);
void _mi_profiler_on_free_local_slow(mi_page_t* page, void* ptr);
void _mi_profiler_on_free_collected_slow(mi_page_t* page, mi_block_t* head);

#endif // MIMALLOC_PROFILE_H
