/* ----------------------------------------------------------------------------
Copyright (c) 2026-2026, Microsoft Research, Daniel Schwartz-Narbonne, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Tests for the mimalloc heap profiler (src/profile.c).

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "mimalloc.h"
#include "mimalloc-profile.h"
#include "mimalloc/internal.h"
#include "testhelper.h"

// ---------------------------------------------------------------------------
// Shared callback state (not thread safe!)
// ---------------------------------------------------------------------------

typedef struct {
  int64_t alloc_count;
  int64_t free_count;
  size_t  last_size;
  size_t  last_upscaled;
  void*   last_ptr;
} profile_state_t;

// We store ptr in user_data so on_free can verify the round-trip.

static profile_state_t g_state;

#define TEST_THRESHOLD (16 * 1024)

static size_t mi_cdecl on_alloc(mi_profiler_data_t* data, void* ptr, size_t threshold, size_t bytes_since_last_sample, const mi_heap_t* heap, void* profiler_arg) {
  MI_UNUSED(threshold); MI_UNUSED(heap); MI_UNUSED(profiler_arg);
  assert(profiler_arg==&g_state);
  assert(bytes_since_last_sample >= data->requested_size);
  g_state.alloc_count++;
  g_state.last_ptr      = ptr;
  g_state.last_size     = data->requested_size;
  g_state.last_upscaled = bytes_since_last_sample;   
  // store ptr to verify round-trip
  data->user_data[0] = ptr; 
  return TEST_THRESHOLD;
}

static void mi_cdecl on_free(mi_profiler_data_t* data, void* ptr, const mi_heap_t* heap, void* profiler_arg) {
  MI_UNUSED(heap); MI_UNUSED_RELEASE(profiler_arg); MI_UNUSED_RELEASE(data);
  assert(profiler_arg==&g_state);
  g_state.free_count++;
  // verify the user_data round-trip
  assert(data->user_data[0] == ptr);
  if (g_state.last_ptr == ptr) { assert(data->requested_size == g_state.last_size); }
}

mi_profiler_t my_profiler = {
  NULL, NULL, NULL,  // reserved
  &g_state,          // profiler_arg
  3*sizeof(void*),   // needed data size
  &on_alloc,       
  &on_free,
  NULL
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Force at least one sample by allocating well over the threshold.
static void allocate_past_threshold(void) {
  size_t total = 0;
  while (total < TEST_THRESHOLD * 10) {
    void* p = mi_malloc(4096);
    mi_free(p);
    total += 4096;
    // mi_collect()
  }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

bool test_profiler_samples(void) {
  CHECK_BODY("profiler: on_alloc called after threshold") {
    int before = g_state.alloc_count;
    allocate_past_threshold();
    result = (g_state.alloc_count > before);
  }
  return true;
}

bool test_profiler_record_fields(void) {
  CHECK_BODY("profiler: record ptr and size are non-zero") {
    int before = g_state.alloc_count;
    while (g_state.alloc_count == before) {
      void* p = mi_malloc(1024);
      mi_free(p);
    }
    result = (g_state.last_ptr != NULL && g_state.last_size > 0 && g_state.last_upscaled > 0);
  }
  return true;
}

bool test_profiler_on_free_called(void) {
  CHECK_BODY("profiler: on_free called for sampled allocation") {
    int alloc_before = g_state.alloc_count;
    int free_before  = g_state.free_count;

    // Keep the pointer live until we confirm a sample was taken, then free it.
    void* sampled = NULL;
    while (g_state.alloc_count == alloc_before) {
      if (sampled) { mi_free(sampled); }
      sampled = mi_malloc(1024);
    }
    // At this point g_state.last_ptr is the sampled pointer.
    // Free it and check on_free fires.
    void* expected = g_state.last_ptr;
    mi_free(expected);
    sampled = NULL;

    result = (g_state.free_count > free_before);
  }
  return true;
}

bool test_profiler_upscaled_at_least_size(void) {
  CHECK_BODY("profiler: upscaled_size >= size") {
    int before = g_state.alloc_count;
    while (g_state.alloc_count == before) {
      void* p = mi_malloc(256);
      mi_free(p);
    }
    result = (g_state.last_upscaled >= g_state.last_size);
  }
  return true;
}

bool test_profiler_free_count_le_alloc_count(void) {
  CHECK_BODY("profiler: on_free never called more times than on_alloc") {
    // Free can only fire for sampled allocations, so free_count <= alloc_count
    // must hold at all times.
    allocate_past_threshold();
    result = (g_state.free_count <= g_state.alloc_count);
  }
  return true;
}


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
  mi_profile(&my_profiler);
  mi_profiler_start(&my_profiler);

  test_profiler_upscaled_at_least_size();
  test_profiler_samples();
  test_profiler_record_fields();
  test_profiler_on_free_called();
  test_profiler_free_count_le_alloc_count();

  mi_profiler_stop(&my_profiler);

  return print_test_summary();
}
