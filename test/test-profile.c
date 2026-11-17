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
#include <stddef.h>

#include "mimalloc.h"
#include "mimalloc-profile.h"
#include "mimalloc/decl.h"
#include "testhelper.h"

// ---------------------------------------------------------------------------
// Shared callback state (not thread safe!)
// ---------------------------------------------------------------------------

typedef struct {
  mi_profiler_t profiler;
  uint64_t  alloc_count;
  uint64_t  free_count;
  size_t    last_size;
  uint64_t  last_upscaled;
  void*     last_ptr;
} my_profiler_t;

static inline my_profiler_t* downcast( mi_profiler_t* prof ) {
  return (my_profiler_t*)prof;
}

// We store ptr in user_data so on_free can verify the round-trip.

#define TEST_THRESHOLD (16 * 1024)

static mi_heap_t* observed_heap;
static uint64_t observed_heap_allocs;

static size_t mi_cdecl on_alloc(mi_profiler_t* profiler, mi_profiler_sample_data_t* data, void* ptr, size_t requested_size, size_t threshold, uint64_t bytes_since_last_sample, mi_heap_t* heap) {
  MI_UNUSED(threshold); MI_UNUSED(requested_size);
  if (heap == observed_heap) { observed_heap_allocs++; }
  my_profiler_t* prof = downcast(profiler);
  assert(bytes_since_last_sample >= requested_size);
  prof->alloc_count++;
  prof->last_ptr      = ptr;
  prof->last_size     = requested_size;
  prof->last_upscaled = bytes_since_last_sample;
  // store ptr to verify round-trip
  assert(data->user_data_size >= sizeof(void*));
  assert(data->user_data_size >= prof->profiler.sample_data_size);
  data->user_data[0] = ptr;
  return TEST_THRESHOLD;
}

static void mi_cdecl on_free(mi_profiler_t* profiler, mi_profiler_sample_data_t* data, void* ptr, mi_heap_t* heap) {
  MI_UNUSED(heap); MI_UNUSED_RELEASE(data); MI_UNUSED_RELEASE(ptr);
  my_profiler_t* prof = downcast(profiler);
  prof->free_count++;
  // verify the user_data round-trip
  assert(data->user_data[0] == ptr);
}

static my_profiler_t my_profiler = {
  { // profiler_t
    NULL,            // reserved
    sizeof(void*),   // needed sample data size
    0,               // initial sample rate (default)
    &on_alloc,
    &on_free,
    NULL,            // on_realloc
    NULL,            // on_snapshot
  },
  0, 0, 0, 0, NULL
};




// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Force at least one sample by allocating well over the threshold.
// (with MI_SAMPLE==1 allocations are counted in batches of up to a page, e.g. 219 KiB in secure mode on 32-bit)
static void allocate_past_threshold(void) {
  size_t total = 0;
  while (total < TEST_THRESHOLD * 128) {
    void* p = mi_malloc(4096);
    mi_free(p);
    total += 4096;
    // mi_collect()
  }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#if MI_GUARDED >= 2
static void test_profiler_restart_with_guarded_sampling(void) {
  CHECK_BODY("profiler: restart accounts for the shared guarded countdown") {
    mi_theap_t* theap = mi_theap_get_default();
    mi_theap_guarded_set_sample_rate(theap, 1024, 1);
    mi_theap_guarded_set_size_bound(theap, 0, SIZE_MAX);
    void* sampled = mi_malloc(1024 * 1024);
    // Let a guarded sample restart the shared countdown before restarting profiling.
    void* guarded = mi_malloc(64);
    mi_profiler_stop(&my_profiler.profiler);
    mi_profiler_start(&my_profiler.profiler);
    const uint64_t before = my_profiler.alloc_count;
    void* p = mi_malloc(4096);
    result = (sampled != NULL && guarded != NULL && p != NULL &&
              my_profiler.alloc_count == before + 1 &&
              my_profiler.last_size == 4096 && my_profiler.last_upscaled >= 4096);
    mi_free(p);
    mi_free(guarded);
    mi_free(sampled);
    mi_theap_guarded_set_sample_rate(theap, (size_t)mi_option_get(mi_option_guarded_sample_rate),
                                   (size_t)mi_option_get(mi_option_guarded_sample_seed));
    mi_theap_guarded_set_size_bound(theap, (size_t)mi_option_get(mi_option_guarded_min),
                                   (size_t)mi_option_get(mi_option_guarded_max));
  }
}
#endif

bool test_profiler_samples(void) {
  CHECK_BODY("profiler: on_alloc called after threshold") {
    uint64_t before = my_profiler.alloc_count;
    allocate_past_threshold();
    result = (my_profiler.alloc_count > before);
  }
  return true;
}

#define MAXLOOP 100000

bool test_profiler_record_fields(void) {
  CHECK_BODY("profiler: record ptr and size are non-zero") {
    uint64_t before = my_profiler.alloc_count;
    int count;
    for (count = 0; my_profiler.alloc_count == before && count < MAXLOOP; count++) {
      void* p = mi_malloc(1024);
      mi_free(p);
    }
    assert(count!=MAXLOOP);
    result = (my_profiler.last_ptr != NULL && my_profiler.last_size > 0 && my_profiler.last_upscaled > 0 && count!=MAXLOOP);
  }
  return true;
}

bool test_profiler_on_free_called(void) {
  CHECK_BODY("profiler: on_free called for sampled allocation") {
    uint64_t alloc_before = my_profiler.alloc_count;
    uint64_t free_before  = my_profiler.free_count;

    // Keep the pointer live until we confirm a sample was taken, then free it.
    void* sampled = NULL;
    int count;
    for (count = 0; my_profiler.alloc_count == alloc_before && count < MAXLOOP; count++) {
      if (sampled) { mi_free(sampled); }
      sampled = mi_malloc(1024);
    }
    // At this point my_profiler.last_ptr is the sampled pointer.
    // Free it and check on_free fires.
    void* expected = my_profiler.last_ptr;
    mi_free(expected);
    sampled = NULL;
    assert(count!=MAXLOOP);
    result = (my_profiler.free_count > free_before && count!=MAXLOOP);
  }
  return true;
}

bool test_profiler_upscaled_at_least_size(void) {
  CHECK_BODY("profiler: upscaled_size >= size") {
    uint64_t before = my_profiler.alloc_count;
    int count;
    for (count = 0; my_profiler.alloc_count == before && count < MAXLOOP; count++) {
      void* p = mi_malloc(256);
      mi_free(p);
    }
    assert(count!=MAXLOOP);
    result = (my_profiler.last_upscaled >= my_profiler.last_size && count!=MAXLOOP);
  }
  return true;
}

bool test_profiler_free_count_le_alloc_count(void) {
  CHECK_BODY("profiler: on_free never called more times than on_alloc") {
    // Free can only fire for sampled allocations, so free_count <= alloc_count
    // must hold at all times.
    allocate_past_threshold();
    result = (my_profiler.free_count <= my_profiler.alloc_count);
  }
  return true;
}

static my_profiler_t my_profiler2 = {
  { NULL, sizeof(void*), 0, &on_alloc, &on_free, NULL, NULL },
  0, 0, 0, 0, NULL
};

// allocate until `prof` samples an allocation and return it (still live)
static void* allocate_sampled(my_profiler_t* prof) {
  const uint64_t before = prof->alloc_count;
  void* p = NULL;
  for (int count = 0; prof->alloc_count == before && count < MAXLOOP; count++) {
    mi_free(p);
    p = mi_malloc(1024);
  }
  return (prof->alloc_count == before ? NULL : p);
}

bool test_profiler_switch(void) {
  CHECK_BODY("profiler: on_free not called for allocations sampled before a re-attach or by another profiler") {
    void* p0 = allocate_sampled(&my_profiler);
    void* p1 = allocate_sampled(&my_profiler);
    void* p2 = allocate_sampled(&my_profiler);
    // pausing keeps passing earlier allocations to on_free
    mi_profiler_stop(&my_profiler.profiler);
    mi_profiler_start(&my_profiler.profiler);
    uint64_t free_before = my_profiler.free_count;
    mi_free(p0);
    const bool pause_ok = (p0 != NULL && my_profiler.free_count == free_before + 1);
    // re-attach: allocations sampled before are no longer passed to on_free
    mi_profile(NULL);
    mi_profile(&my_profiler.profiler);
    mi_profiler_start(&my_profiler.profiler);
    free_before = my_profiler.free_count;
    mi_free(p1);
    const bool restart_ok = (p1 != NULL && my_profiler.free_count == free_before);
    // switch to another profiler: allocations sampled by the previous profiler are not passed to on_free
    mi_profiler_stop(&my_profiler.profiler);
    mi_profile(NULL);
    mi_profile(&my_profiler2.profiler);
    mi_profiler_start(&my_profiler2.profiler);
    mi_free(p2);
    const bool switch_ok = (p2 != NULL && my_profiler2.free_count == 0 && my_profiler.free_count == free_before);
    // but allocations sampled by the new profiler are
    void* p3 = allocate_sampled(&my_profiler2);
    mi_free(p3);
    const bool new_ok = (p3 != NULL && my_profiler2.free_count == 1);
    mi_profiler_stop(&my_profiler2.profiler);
    mi_profile(NULL);
    result = (pause_ok && restart_ok && switch_ok && new_ok);
  }
  return true;
}


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static bool allocate_from_heap(mi_heap_t* heap) {
  for (size_t i = 0; i < 4096; i++) {
    void* p = mi_heap_malloc(heap, 1024 * 1024);
    if (p == NULL) return false;
    mi_free(p);
  }
  return true;
}

static void test_profiler_disabled_heap(void) {
  CHECK_BODY("profiler: disabled heap stays excluded after detach and reattach") {
    mi_heap_t* disabled = mi_heap_new();
    mi_heap_t* enabled = mi_heap_new();
    result = (disabled != NULL && enabled != NULL);
    if (result) {
      mi_heap_profile_disable(disabled);
      for (size_t i = 0; result && i < 2; i++) {
        result = mi_profile(&my_profiler.profiler);
        mi_profiler_start(&my_profiler.profiler);
        observed_heap = disabled;
        uint64_t before = observed_heap_allocs;
        result = allocate_from_heap(disabled) && observed_heap_allocs == before && result;
        observed_heap = enabled;
        before = observed_heap_allocs;
        result = allocate_from_heap(enabled) && observed_heap_allocs > before && result;
        mi_profiler_stop(&my_profiler.profiler);
        result = mi_profile(NULL) && result;
      }
    }
    if (disabled != NULL) { mi_heap_delete(disabled); }
    if (enabled != NULL) { mi_heap_delete(enabled); }
    observed_heap = NULL;
  }
}

int main(void) {
  mi_profile(&my_profiler.profiler);
  mi_profiler_start(&my_profiler.profiler);

  #if MI_GUARDED >= 2
  test_profiler_restart_with_guarded_sampling();
  #endif
  test_profiler_upscaled_at_least_size();
  test_profiler_samples();
  test_profiler_record_fields();
  test_profiler_on_free_called();
  test_profiler_free_count_le_alloc_count();
  test_profiler_switch();
  test_profiler_disabled_heap();

  return print_test_summary();
}
