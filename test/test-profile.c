/* ----------------------------------------------------------------------------
Copyright (c) 2018-2024, Microsoft Research, Daan Leijen
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
#include "mimalloc/internal.h"
#include "mimalloc/profile.h"
#include "testhelper.h"

// ---------------------------------------------------------------------------
// Shared callback state
// ---------------------------------------------------------------------------

typedef struct {
  int    alloc_count;
  int    free_count;
  size_t last_size;
  size_t last_upscaled;
  void*  last_ptr;
} profile_state_t;

// We store ptr in user_data so on_free can verify the round-trip.

static profile_state_t g_state;

#define TEST_THRESHOLD (16 * 1024)

static size_t on_alloc(void* user_data, void* ptr, size_t requested_size, size_t usable_size, size_t threshold, size_t bytes_since_last_sample, uint8_t heap_tag) {
  (void)usable_size;
  (void)threshold;
  (void)heap_tag;
  g_state.alloc_count++;
  g_state.last_ptr      = ptr;
  g_state.last_size     = requested_size;
  g_state.last_upscaled = bytes_since_last_sample;
  // store ptr in user_data so on_free can verify the round-trip
  memcpy(user_data, &ptr, sizeof(ptr));
  return TEST_THRESHOLD;
}

static void on_free(void* user_data, void* ptr) {
  g_state.free_count++;
  // verify the user_data round-trip
  void* stored;
  memcpy(&stored, user_data, sizeof(stored));
  assert(stored == ptr);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Force at least one sample by allocating well over the threshold.
static void allocate_past_threshold(void) {
  size_t total = 0;
  while (total < TEST_THRESHOLD * 3) {
    void* p = mi_malloc(4096);
    mi_free(p);
    total += 4096;
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

bool test_profiler_enable_invalid_params(void) {
  CHECK_BODY("profiler: enable rejects NULL on_alloc") {
    result = !mi_profiler_enable(0, NULL, NULL);
  }
  CHECK_BODY("profiler: enable rejects non-zero record_extra_bytes with NULL on_free") {
    result = !mi_profiler_enable(sizeof(void*), on_alloc, NULL);
  }
  CHECK_BODY("profiler: enable rejects record_extra_bytes overflow") {
    result = !mi_profiler_enable(SIZE_MAX, on_alloc, on_free);
  }
  return true;
}

bool test_profiler_enable_once(void) {
  CHECK_BODY("profiler: enable returns true on first call") {
    result = mi_profiler_enable(sizeof(void*), on_alloc, on_free);
  }
  CHECK_BODY("profiler: enable returns false on second call") {
    result = !mi_profiler_enable(sizeof(void*), on_alloc, on_free);
  }
  return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
  // Invalid-param and double-call tests must run before profiling is enabled.
  test_profiler_enable_invalid_params();
  test_profiler_enable_once();  // enables profiling with sizeof(void*) extra bytes

  test_profiler_samples();
  test_profiler_record_fields();
  test_profiler_on_free_called();
  test_profiler_upscaled_at_least_size();
  test_profiler_free_count_le_alloc_count();

  return print_test_summary();
}
