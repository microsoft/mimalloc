/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// ---------------------------------------------------------------------------
// This file will implement basic memory profiles using pprof text format
// ---------------------------------------------------------------------------

#include <stdio.h>      // FILE, fopen, fprintf, fclose
#include <inttypes.h>   // PRIxPTR

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "mimalloc-profile.h"

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

mi_profiler_t* mi_pprof_profiler_new(size_t initial_threshold);
void           mi_pprof_profiler_delete(mi_profiler_t* profiler);
void           mi_pprof_profiler_dump(mi_profiler_t* profiler, const char* base_file_name);



// ---------------------------------------------------------------------------
// Internal API
// ---------------------------------------------------------------------------

typedef struct mi_location_s  mi_location_t;
typedef struct mi_locations_s mi_locations_t;
typedef struct mi_callstack_s mi_callstack_t;

static size_t mi_prim_backtrace(void** buffer, size_t max_depth);

static bool mi_locations_init(mi_heap_t* heap, mi_locations_t* locations);
static void mi_locations_done(mi_heap_t* heap, mi_locations_t* locations);
static mi_location_t* mi_locations_find_or_insert(mi_heap_t* heap, mi_locations_t* locations, mi_threadid_t thread_id, const mi_callstack_t* callstack);


// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
#define MI_MAX_BACKTRACE_DEPTH 64

struct mi_callstack_s {
  size_t  count;
  void**  frames;
};

// A location is a unique combination of a thread and runtime callstack for 
// which we track allocation and deallocation statistics.
struct mi_location_s {
  struct mi_location_s* next;    // next location in the same hash bucket (for chaining on collision)
  mi_threadid_t     thread_id;
  mi_callstack_t    callstack;
  size_t            hash;         // hash of the thread_id and callstack
  uint64_t          alloc_count;
  uint64_t          alloc_bytes;
  uint64_t          inuse_count;
  uint64_t          inuse_bytes;
};

// A basic hash table of locations.
struct mi_locations_s {
  size_t            bucket_count;      // number of buckets (fixed size)
  mi_location_t**   buckets;           // array of `bucket_count` chain heads
  size_t            count;             // number of locations currently stored
};

// Our profiler.
typedef struct {
  mi_profiler_t     profiler;             // mimalloc profiler info
  mi_heap_t*        profile_heap;         // heap used for all profiler allocations (so these don't interfere)
  mi_locations_t    locations;            // hash table of call locations
  size_t            sample_threshold;     // current sample threshold
  size_t            dump_count;           // number of times `mi_pprof_profiler_dump` was called (used to number the dump files)
} pprof_profiler_t;

static inline pprof_profiler_t* downcast( mi_profiler_t* prof ) { 
  return (pprof_profiler_t*)prof; 
} 


// ---------------------------------------------------------------------------
// Get a location
// ---------------------------------------------------------------------------

static mi_location_t* mi_location_get(pprof_profiler_t* prof) {
  void* frames[MI_MAX_BACKTRACE_DEPTH];
  const size_t depth = mi_prim_backtrace(frames, MI_MAX_BACKTRACE_DEPTH);
  if (depth==0) return NULL;
  mi_callstack_t callstack = { .count = depth, .frames = frames };
  const mi_threadid_t thread_id = _mi_thread_id();
  mi_location_t* loc = mi_locations_find_or_insert(prof->profile_heap, &prof->locations, thread_id, &callstack);
  return loc;
}

// ---------------------------------------------------------------------------
// Profiler callbacks and initialization
// ---------------------------------------------------------------------------

#define TEST_THRESHOLD (16 * 1024)

static size_t mi_cdecl on_alloc(mi_profiler_t* profiler, mi_profiler_sample_data_t* data, void* ptr, size_t requested_size, size_t threshold, uint64_t bytes_since_last_sample, const mi_heap_t* heap) {
  MI_UNUSED(threshold); MI_UNUSED(heap); MI_UNUSED(bytes_since_last_sample);
  pprof_profiler_t* prof = downcast(profiler);
  mi_location_t* loc = mi_location_get(prof);
  const size_t new_threshold = prof->sample_threshold; // todo: be more sophisticated in adjusting the sample threshold
  const size_t alloc_size = requested_size;            // or mi_heap_usable_size(heap,ptr) ?
  if (data!=NULL) {
    mi_assert(data->user_data_size >= 2*sizeof(void*));
    data->user_data[0] = loc;
    data->user_data[1] = (void*)((uintptr_t)alloc_size);
  }
  if (loc!=NULL) {
    loc->alloc_bytes += alloc_size;  
    loc->alloc_count += 1;
    loc->inuse_bytes += alloc_size;
    loc->inuse_count += 1;
  }
  return new_threshold;
}

static void mi_cdecl on_free(mi_profiler_t* profiler, mi_profiler_sample_data_t* data, void* ptr, const mi_heap_t* heap) {
  MI_UNUSED(heap); MI_UNUSED(ptr);
  pprof_profiler_t* prof = downcast(profiler);
  if (data!=NULL) {
    mi_assert(data->user_data_size >= 2*sizeof(void*));
    mi_location_t* loc = (mi_location_t*)data->user_data[0];
    if (loc!=NULL) {
      loc->inuse_bytes -= (size_t)((uintptr_t)data->user_data[1]);
      loc->inuse_count -= 1;
    }
  }
}

mi_profiler_t* mi_pprof_profiler_new(size_t initial_threshold) {
  // heap just for the profiler itself
  mi_heap_t* heap = mi_heap_new();
  mi_heap_profile_disable(heap);  // don't sample allocations in this heap

  // allocate and initialize the profiler structure from this heap
  pprof_profiler_t* prof = mi_heap_zalloc_tp(pprof_profiler_t,heap);
  if (prof == NULL) return NULL;
  prof->sample_threshold = initial_threshold;
  prof->profile_heap = heap;
  if (!mi_locations_init(heap, &prof->locations)) {
    mi_free(prof);
    return NULL;
  }
  prof->profiler.initial_sample_rate = initial_threshold;
  prof->profiler.sample_data_size = 2*sizeof(void*);  // we store the location and the allocation size in the sample data
  prof->profiler.on_alloc = &on_alloc;
  prof->profiler.on_free = &on_free;
  return &prof->profiler;
}

void mi_pprof_profiler_delete(mi_profiler_t* profiler) {
  if (profiler == NULL) return;
  pprof_profiler_t* prof = downcast(profiler);
  mi_heap_t* heap = prof->profile_heap;
  mi_locations_done(heap, &prof->locations);
  mi_free(prof);
  mi_heap_delete(heap);
}

// ---------------------------------------------------------------------------
// Dumping the profile in the (original, textual) pprof heap profile format,
// see e.g. https://gaultier.github.io/blog/roll_your_own_memory_profiling.html
// ---------------------------------------------------------------------------

// On Linux, `pprof` can symbolize addresses using the `MAPPED_LIBRARIES` trailer
// which is just a copy of `/proc/self/maps`. On other platforms we leave this
// section empty (`pprof` can still work without it if not using PIE, or when
// combined with tools like `addr2line` / a symbol server).
static void mi_pprof_write_mapped_libraries(FILE* f) {
  fprintf(f, "MAPPED_LIBRARIES:\n");
  #if defined(__linux__)
  FILE* maps = fopen("/proc/self/maps", "r");
  if (maps != NULL) {
    char line[512];
    while (fgets(line, sizeof(line), maps) != NULL) {
      fputs(line, f);
    }
    fclose(maps);
  }
  #endif
}

// Write out the current profiler data in the pprof heap profile text format:
// a header line with the totals, followed by one line per unique call
// location, and a trailer with the mapped libraries (used by `pprof` to
// symbolize the addresses). Each call writes a new file named
// `<base_file_name>.<seq>.heap` with an incrementing sequence number,
// mimicking the naming used by the original (gperftools) pprof heap profiler.
void mi_pprof_profiler_dump(mi_profiler_t* profiler, const char* base_file_name) {
  if (profiler == NULL || base_file_name == NULL) return;
  pprof_profiler_t* prof = downcast(profiler);
  mi_locations_t* locations = &prof->locations;
  if (locations->buckets == NULL) return;

  char fname[1024];
  const size_t seq = ++prof->dump_count;
  snprintf(fname, sizeof(fname), "%s.%04" PRIu64 ".heap", base_file_name, (uint64_t)seq);

  FILE* f = fopen(fname, "w");
  if (f == NULL) return;

  // first pass: compute the totals over all locations
  uint64_t total_inuse_objects = 0;
  uint64_t total_inuse_bytes   = 0;
  uint64_t total_alloc_objects = 0;
  uint64_t total_alloc_bytes   = 0;
  for (size_t i = 0; i < locations->bucket_count; i++) {
    for (mi_location_t* loc = locations->buckets[i]; loc != NULL; loc = loc->next) {
      total_inuse_objects += loc->inuse_count;
      total_inuse_bytes   += loc->inuse_bytes;
      total_alloc_objects += loc->alloc_count;
      total_alloc_bytes   += loc->alloc_bytes;
    }
  }

  fprintf(f, "heap profile: %6" PRIu64 ": %8" PRIu64 " [%6" PRIu64 ": %8" PRIu64 "] @ heapprofile\n",
             total_inuse_objects, total_inuse_bytes, total_alloc_objects, total_alloc_bytes);

  // second pass: write one line per unique call location
  for (size_t i = 0; i < locations->bucket_count; i++) {
    for (mi_location_t* loc = locations->buckets[i]; loc != NULL; loc = loc->next) {
      fprintf(f, "%6" PRIu64 ": %8" PRIu64 " [%6" PRIu64 ": %8" PRIu64 "] @",
                 loc->inuse_count, loc->inuse_bytes, loc->alloc_count, loc->alloc_bytes);
      for (size_t j = 0; j < loc->callstack.count; j++) {
        fprintf(f, " 0x%" PRIxPTR, (uintptr_t)loc->callstack.frames[j]);
      }
      fprintf(f, "\n");
    }
  }

  mi_pprof_write_mapped_libraries(f);
  fclose(f);
}

// ---------------------------------------------------------------------------
// A very basic hash table of locations. All memory (the bucket array, the
// locations, and their callstack frames) is allocated from the profiler's
// own heap so it does not interfere with the profiled allocations.
// Collisions are resolved with simple chaining.
// ---------------------------------------------------------------------------

// A reasonably large prime bucket count to keep collision chains short 
#define MI_LOCATIONS_BUCKET_COUNT  (16 * 1024 - 3)  /* 16381, prime */

// FNV-1a hash, mixed over the thread id and the callstack frame pointers.
static size_t mi_location_hash(mi_threadid_t thread_id, const mi_callstack_t* callstack) {
  size_t hash = 0xcbf29ce484222325ULL;   // FNV offset basis
  const size_t prime = 0x100000001b3ULL; // FNV prime
  hash = (hash ^ (size_t)thread_id) * prime;
  for (size_t i = 0; i < callstack->count; i++) {
    hash = (hash ^ (size_t)((uintptr_t)callstack->frames[i])) * prime;
  }
  return hash;
}

// Initialize a location hash table
static bool mi_locations_init(mi_heap_t* heap, mi_locations_t* locations) {
  locations->bucket_count = MI_LOCATIONS_BUCKET_COUNT;
  locations->buckets = (mi_location_t**)mi_heap_zalloc(heap, locations->bucket_count * sizeof(mi_location_t*));
  locations->count = 0;
  return (locations->buckets != NULL);
}

// Free all memory associated with the location hash table.
static void mi_locations_done(mi_heap_t* heap, mi_locations_t* locations) {
  MI_UNUSED(heap);
  if (locations->buckets == NULL) return;
  for (size_t i = 0; i < locations->bucket_count; i++) {
    mi_location_t* loc = locations->buckets[i];
    while (loc != NULL) {
      mi_location_t* next = loc->next;
      if (loc->callstack.frames != NULL) {
        mi_free(loc->callstack.frames);
      }
      mi_free(loc);
      loc = next;
    }
  }
  mi_free(locations->buckets);
  locations->buckets = NULL;
  locations->count = 0;
}

// Find an existing location matching `thread_id` and `callstack`, or insert
// a fresh, zero-initialized one (with `hash` and identifying fields filled in)
// if none exists yet. Returns NULL only on allocation failure.
static mi_location_t* mi_locations_find_or_insert(mi_heap_t* heap, mi_locations_t* locations, mi_threadid_t thread_id, const mi_callstack_t* callstack) {
  if (locations->buckets == NULL) return NULL;
  const size_t hash = mi_location_hash(thread_id, callstack);
  const size_t idx = hash % locations->bucket_count;
  for (mi_location_t* loc = locations->buckets[idx]; loc != NULL; loc = loc->next) {
    if (loc->hash == hash && loc->thread_id == thread_id &&
        loc->callstack.count == callstack->count &&
        (callstack->count == 0 || _mi_memcmp(loc->callstack.frames, callstack->frames, callstack->count * sizeof(void*)) == 0))
    {
      return loc;
    }
  }
  // not found: allocate a new location and link it in at the head of the bucket
  mi_location_t* loc = mi_heap_zalloc_tp(mi_location_t,heap);
  if (loc == NULL) return NULL;
  if (callstack->count > 0) {
    loc->callstack.frames = (void**)mi_heap_mallocn(heap, callstack->count, sizeof(void*));
    if (loc->callstack.frames == NULL) return NULL;
    _mi_memcpy(loc->callstack.frames, callstack->frames, callstack->count * sizeof(void*));
  }
  loc->callstack.count = callstack->count;
  loc->thread_id = thread_id;
  loc->hash = hash;
  loc->next = locations->buckets[idx];
  locations->buckets[idx] = loc;
  locations->count++;
  return loc;
}



// ---------------------------------------------------------------------------
// Basic backtraces
// ---------------------------------------------------------------------------

#if MI_HAS_EXECINFOH
#include <execinfo.h>   // backtrace
static size_t mi_prim_backtrace(void** buffer, size_t max_depth) {
  return (size_t)backtrace(buffer, (int)max_depth);
}
#elif _WIN32
#include <windows.h>    // CaptureStackBackTrace
static size_t mi_prim_backtrace(void** buffer, size_t max_depth) {
  // skip this frame itself; no hash is needed as we compute our own
  return (size_t)CaptureStackBackTrace(1, (ULONG)max_depth, buffer, NULL);
}
#else
static size_t mi_prim_backtrace(void** buffer, size_t max_depth) {
  return 0;
}
#endif