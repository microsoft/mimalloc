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
#include <string.h>     // strcmp

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "mimalloc-profile.h"

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

mi_profiler_t* mi_pprof_profiler_new(size_t initial_threshold, const char* base_file_name);
void           mi_pprof_profiler_delete(mi_profiler_t* profiler);
void           mi_pprof_profiler_dump(mi_profiler_t* profiler);



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

static void mi_pprof_write_mapped_libraries(FILE* f);

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
//
// Locations are only ever inserted, or mutated in-place after publication, and a location is only ever looked up using its own
// `hash`/`thread_id`/`callstack` which allows a lock-free lookup of locations in the hash table.
struct mi_location_s {
  _Atomic(struct mi_location_s*) next;  // next location in the same hash bucket (for chaining on collision);
  mi_threadid_t     thread_id;
  mi_callstack_t    callstack;
  size_t            hash;         // hash of the thread_id and callstack
  int64_t           alloc_count;  // only ever written by the single (allocating) owning thread: not atomic
  int64_t           alloc_bytes;
  int64_t           free_count;   // can be written concurrently by different freeing threads: always
  int64_t           free_bytes;   // accessed through `mi_atomic_addi64_relaxed`/`mi_atomic_loadi64_relaxed`
};

// A basic hash table of locations. The bucket array is fixed-size (allocated
// once in `mi_locations_init`) and each bucket is a lock-free singly-linked
// list that only ever grows by prepending (see `mi_locations_find_or_insert`).
struct mi_locations_s {
  size_t                      bucket_count;  // number of buckets (fixed size)
  _Atomic(mi_location_t*)*    buckets;       // array of `bucket_count` chain heads
  _Atomic(size_t)             count;         // number of locations currently stored (just a statistic)
};

// Our profiler.
typedef struct {
  mi_profiler_t     profiler;             // mimalloc profiler info
  mi_heap_t*        profile_heap;         // heap used for all profiler allocations (so these don't interfere)
  mi_locations_t    locations;            // hash table of call locations
  size_t            sample_threshold;     // current sample threshold
  size_t            dump_count;           // number of times `mi_pprof_profiler_dump` was called (used to number the dump files)
  char*             base_file_name;       // base file name for dump files, e.g. "<base_file_name>.<seq>.heap"
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
    // `on_alloc` for a given location only ever runs on its one owning (allocating)
    // thread, so plain increments are safe here (no concurrent writers possible).
    loc->alloc_bytes += (int64_t)alloc_size;
    loc->alloc_count += 1;
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
      // different threads can free allocations that share the same (allocating-thread,
      // callstack) location concurrently, so these updates must be atomic.
      mi_atomic_addi64_relaxed(&loc->free_bytes, (int64_t)((uintptr_t)data->user_data[1]));
      mi_atomic_addi64_relaxed(&loc->free_count, 1);
    }
  }
}

mi_profiler_t* mi_pprof_profiler_new(size_t initial_threshold, const char* base_file_name) {
  // heap just for the profiler itself
  mi_heap_t* heap = mi_heap_new();
  mi_heap_profile_disable(heap);  // don't sample allocations in this heap

  // allocate and initialize the profiler structure from this heap
  pprof_profiler_t* prof = mi_heap_zalloc_tp(pprof_profiler_t,heap);
  if (prof == NULL) return NULL;
  prof->sample_threshold = initial_threshold;
  prof->profile_heap = heap;
  prof->base_file_name = (base_file_name != NULL ? mi_heap_strndup(heap, base_file_name, 1024) : NULL);
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
  mi_free(prof->base_file_name);
  mi_free(prof);
  mi_heap_delete(heap);
}

// ---------------------------------------------------------------------------
// Dumping the profile in the (original, textual) pprof heap profile format
// ---------------------------------------------------------------------------

// Write out the current profiler data in the pprof heap profile text format:
// a header line with the totals, followed by one line per unique call
// location, and a trailer with the mapped libraries (used by `pprof` to
// symbolize the addresses). Each call writes a new file named
// `<base_file_name>.<seq>.heap` with an incrementing sequence number,
// mimicking the naming used by the original (gperftools) pprof heap profiler.
// The base file name is set when the profiler is created (see `mi_pprof_profiler_new`);
// if it is NULL, no dump is written and this function does nothing.
void mi_pprof_profiler_dump(mi_profiler_t* profiler) {
  if (profiler == NULL) return;
  bool was_running = mi_profiler_stop(profiler);
  pprof_profiler_t* prof = downcast(profiler);
  mi_locations_t* locations = &prof->locations;
  if (locations->buckets == NULL || prof->base_file_name == NULL) return;

  char fname[1024];
  const size_t seq = ++prof->dump_count;
  snprintf(fname, sizeof(fname), "%s.%04" PRIu64 ".heap", prof->base_file_name, (uint64_t)seq);

  FILE* f = fopen(fname, "w");
  if (f == NULL) return;

  // first pass: compute the totals over all locations
  // (dump is assumed to run single-threaded while sampling is stopped, so
  // `alloc_count`/`alloc_bytes` can be read directly; `free_count`/`free_bytes`
  // are still `_Atomic` fields so we use a relaxed load for those)
  uint64_t total_inuse_objects = 0;
  uint64_t total_inuse_bytes   = 0;
  uint64_t total_alloc_objects = 0;
  uint64_t total_alloc_bytes   = 0;
  for (size_t i = 0; i < locations->bucket_count; i++) {
    for (mi_location_t* loc = mi_atomic_load_ptr_relaxed(mi_location_t, &locations->buckets[i]); loc != NULL; loc = mi_atomic_load_ptr_relaxed(mi_location_t, &loc->next)) {
      const int64_t free_count = mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&loc->free_count);
      const int64_t free_bytes = mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&loc->free_bytes);
      total_inuse_objects += (uint64_t)(loc->alloc_count - free_count);
      total_inuse_bytes   += (uint64_t)(loc->alloc_bytes - free_bytes);
      total_alloc_objects += (uint64_t)loc->alloc_count;
      total_alloc_bytes   += (uint64_t)loc->alloc_bytes;
    }
  }

  fprintf(f, "heap profile: %6" PRIu64 ": %8" PRIu64 " [%6" PRIu64 ": %8" PRIu64 "] @ heapprofile\n",
             total_inuse_objects, total_inuse_bytes, total_alloc_objects, total_alloc_bytes);

  // second pass: write one line per unique call location
  for (size_t i = 0; i < locations->bucket_count; i++) {
    for (mi_location_t* loc = mi_atomic_load_ptr_relaxed(mi_location_t, &locations->buckets[i]); loc != NULL; loc = mi_atomic_load_ptr_relaxed(mi_location_t, &loc->next)) {
      const int64_t free_count = mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&loc->free_count);
      const int64_t free_bytes = mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&loc->free_bytes);
      fprintf(f, "%6" PRIu64 ": %8" PRIu64 " [%6" PRIu64 ": %8" PRIu64 "] @",
                 (uint64_t)(loc->alloc_count - free_count), (uint64_t)(loc->alloc_bytes - free_bytes),
                 (uint64_t)loc->alloc_count, (uint64_t)loc->alloc_bytes);
      for (size_t j = 0; j < loc->callstack.count; j++) {
        fprintf(f, " 0x%" PRIxPTR, (uintptr_t)loc->callstack.frames[j]);
      }
      fprintf(f, "\n");
    }
  }

  mi_pprof_write_mapped_libraries(f);
  fclose(f);
  if (was_running) { mi_profiler_start(profiler); }
}

// ---------------------------------------------------------------------------
// A very basic hash table of locations. All memory (the bucket array, the
// locations, and their callstack frames) is allocated from the profiler's
// own heap so it does not interfere with the profiled allocations.
// Collisions are resolved with simple chaining.
//
// `on_alloc` and `on_free` (and thus `mi_locations_find_or_insert`) can run
// concurrently on multiple threads and are made thread-safe without a lock:
// a bucket is a singly-linked list that only ever grows by prepending a new,
// fully initialized location; once published, a location's identifying
// fields (`hash`, `thread_id`, `callstack`) never change again. Moreover,
// since the hash (and thus the location itself) is derived from the calling
// thread's own id, a location is only ever looked up by the same thread
// that creates it, so there is never a concurrent insert race for the
// same location. 
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
  locations->buckets = (_Atomic(mi_location_t*)*)mi_heap_zalloc(heap, locations->bucket_count * sizeof(*locations->buckets));
  mi_atomic_store_relaxed(&locations->count, (size_t)0);
  return (locations->buckets != NULL);
}

// Free all memory associated with the location hash table.
// Not thread-safe: assumes no concurrent `on_alloc`/`on_free` (see the note above).
static void mi_locations_done(mi_heap_t* heap, mi_locations_t* locations) {
  MI_UNUSED(heap);
  if (locations->buckets == NULL) return;
  for (size_t i = 0; i < locations->bucket_count; i++) {
    mi_location_t* loc = mi_atomic_load_ptr_relaxed(mi_location_t, &locations->buckets[i]);
    while (loc != NULL) {
      mi_location_t* next = mi_atomic_load_ptr_relaxed(mi_location_t, &loc->next);
      if (loc->callstack.frames != NULL) {
        mi_free(loc->callstack.frames);
      }
      mi_free(loc);
      loc = next;
    }
  }
  mi_free(locations->buckets);
  locations->buckets = NULL;
  mi_atomic_store_relaxed(&locations->count, (size_t)0);
}

// Match a location's identifying fields; these never change after publication.
static bool mi_location_matches(const mi_location_t* loc, size_t hash, mi_threadid_t thread_id, const mi_callstack_t* callstack) {
  return (loc->hash == hash && loc->thread_id == thread_id &&
          loc->callstack.count == callstack->count &&
          (callstack->count == 0 || _mi_memcmp(loc->callstack.frames, callstack->frames, callstack->count * sizeof(void*)) == 0));
}

// Find an existing location matching `thread_id` and `callstack`, or insert
// a fresh, zero-initialized one (with `hash` and identifying fields filled in)
// if none exists yet. Returns NULL only on allocation failure.
// Thread-safe (see the note above): the common case (an existing location)
// is a lock-free, read-only traversal using relaxed loads; only inserting a
// genuinely new location uses a CAS (and even then, only ever races against
// other threads inserting *different* locations into the same bucket).
static mi_location_t* mi_locations_find_or_insert(mi_heap_t* heap, mi_locations_t* locations, mi_threadid_t thread_id, const mi_callstack_t* callstack) {
  if (locations->buckets == NULL) return NULL;
  const size_t hash = mi_location_hash(thread_id, callstack);
  const size_t idx = hash % locations->bucket_count;
  _Atomic(mi_location_t*)* bucket = &locations->buckets[idx];

  // fast path: lock-free, read-only lookup in the (possibly concurrently growing) chain
  for (mi_location_t* loc = mi_atomic_load_ptr_relaxed(mi_location_t, bucket); loc != NULL;
       loc = mi_atomic_load_ptr_relaxed(mi_location_t, &loc->next))
  {
    if (mi_location_matches(loc, hash, thread_id, callstack)) return loc;
  }

  // not found: allocate and fully initialize a new location; it is not yet
  // visible to any other thread so plain (non-atomic) stores are fine here.
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

  // publish the new location at the head of the bucket with a CAS loop. Since
  // a location's key includes the calling thread's own id, no other thread
  // can ever race to insert this *same* key -- a CAS can only fail because
  // some other thread prepended an unrelated location to this bucket in the
  // meantime, so we simply retry with the updated head (no duplicate check
  // needed, unlike a typical lock-free insert).
  mi_location_t* head = mi_atomic_load_ptr_relaxed(mi_location_t, bucket);
  do {
    mi_atomic_store_ptr_relaxed(mi_location_t, &loc->next, head);  // `loc` not yet published: plain store is fine
  } while (!mi_atomic_cas_ptr_weak_release(mi_location_t, bucket, &head, loc));  // `head` is updated to the current value on failure
  mi_atomic_increment_relaxed(&locations->count);  // just a statistic
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


// ---------------------------------------------------------------------------
// Write out mapped libraries
// ---------------------------------------------------------------------------

// On Linux, `pprof` can symbolize addresses using the `MAPPED_LIBRARIES` trailer
// which is just a copy of `/proc/self/maps`. On macOS there is no equivalent
// file so we reconstruct a similarly formatted line (`start-end perm ... path`)
// per loaded Mach-O image using the dyld APIs: the range is taken from the
// image's `__TEXT` segment, which is also the "load address" that tools like
// `atos` use to symbolize addresses (`atos -o <path> -l <start> <addr>`). On
// Windows we walk the process address space with `VirtualQuery` 
// On other platforms we leave this section empty (`pprof` can still work
// without it if not using PIE, or when combined with tools like `atos` /
// `addr2line` / a symbol server).
#if defined(_WIN32)
#include <windows.h>

// Walk the process address space and write one line per loaded module:
// `<base>-<end> r-xp 00000000 00:00 0            <path>`. Consecutive
// `MEM_IMAGE` regions that belong to the same module (same allocation base)
// are merged into a single range.
static void mi_win32_write_mapped_libraries(FILE* f) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  uint8_t* addr = (uint8_t*)si.lpMinimumApplicationAddress;
  uint8_t* const addr_max = (uint8_t*)si.lpMaximumApplicationAddress;
  void* cur_base = NULL;
  uintptr_t cur_end = 0;
  while (addr < addr_max) {
    MEMORY_BASIC_INFORMATION mbi;
    const SIZE_T n = VirtualQuery(addr, &mbi, sizeof(mbi));
    if (n == 0) break;  // no more (queryable) regions
    if (mbi.Type == MEM_IMAGE && mbi.State != MEM_FREE) {
      if (mbi.AllocationBase == cur_base) {
        // extend the current module's range
        cur_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
      }
      else {
        // a new module starts: flush the previous one first
        if (cur_base != NULL) {
          char path[MAX_PATH];
          const DWORD len = GetModuleFileNameA((HMODULE)cur_base, path, (DWORD)sizeof(path));
          fprintf(f, "%" PRIxPTR "-%" PRIxPTR " r-xp 00000000 00:00 0            %s\n",
                  (uintptr_t)cur_base, cur_end, (len > 0 ? path : ""));
        }
        cur_base = mbi.AllocationBase;
        cur_end  = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
      }
    }
    else if (cur_base != NULL) {
      // the module's regions ended: flush it
      char path[MAX_PATH];
      const DWORD len = GetModuleFileNameA((HMODULE)cur_base, path, (DWORD)sizeof(path));
      fprintf(f, "%" PRIxPTR "-%" PRIxPTR " r-xp 00000000 00:00 0            %s\n",
              (uintptr_t)cur_base, cur_end, (len > 0 ? path : ""));
      cur_base = NULL;
    }
    const uint8_t* next = (const uint8_t*)mbi.BaseAddress + mbi.RegionSize;
    if (next <= addr) break;  // guard against a non-advancing region
    addr = (uint8_t*)next;
  }
  if (cur_base != NULL) {
    char path[MAX_PATH];
    const DWORD len = GetModuleFileNameA((HMODULE)cur_base, path, (DWORD)sizeof(path));
    fprintf(f, "%" PRIxPTR "-%" PRIxPTR " r-xp 00000000 00:00 0            %s\n",
            (uintptr_t)cur_base, cur_end, (len > 0 ? path : ""));
  }
}

#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

// Find the (slid) virtual address range of the `__TEXT` segment of a loaded
// Mach-O image; this is the image's load address as used for symbolication.
static bool mi_macho_image_range(const struct mach_header* mh, intptr_t slide, uintptr_t* start, uintptr_t* end) {
  const bool is64 = (mh->magic == MH_MAGIC_64 || mh->magic == MH_CIGAM_64);
  const uint8_t* cmd_ptr = (const uint8_t*)mh + (is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header));
  for (uint32_t c = 0; c < mh->ncmds; c++) {
    const struct load_command* lc = (const struct load_command*)cmd_ptr;
    if (is64 && lc->cmd == LC_SEGMENT_64) {
      const struct segment_command_64* seg = (const struct segment_command_64*)lc;
      if (strcmp(seg->segname, SEG_TEXT) == 0) {
        *start = (uintptr_t)seg->vmaddr + (uintptr_t)slide;
        *end   = *start + (uintptr_t)seg->vmsize;
        return true;
      }
    }
    else if (!is64 && lc->cmd == LC_SEGMENT) {
      const struct segment_command* seg = (const struct segment_command*)lc;
      if (strcmp(seg->segname, SEG_TEXT) == 0) {
        *start = (uintptr_t)seg->vmaddr + (uintptr_t)slide;
        *end   = *start + (uintptr_t)seg->vmsize;
        return true;
      }
    }
    cmd_ptr += lc->cmdsize;
  }
  return false;  // no __TEXT segment found
}

static void mi_write_mapped_libraries_macos(FILE* f) {
  const uint32_t count = _dyld_image_count();
  for (uint32_t i = 0; i < count; i++) {
    const struct mach_header* mh = _dyld_get_image_header(i);
    const char* name = _dyld_get_image_name(i);
    if (mh == NULL || name == NULL) continue;
    const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
    uintptr_t start = 0;
    uintptr_t end = 0;
    if (!mi_macho_image_range(mh, slide, &start, &end)) continue;
    fprintf(f, "%" PRIxPTR "-%" PRIxPTR " r-xp 00000000 00:00 0            %s\n", start, end, name);
  }
}

#elif defined(__linux__)
// Copy `/proc/self/maps` verbatim; this is exactly the format `pprof` expects.
static void mi_write_mapped_libraries_linux(FILE* f) {
  FILE* maps = fopen("/proc/self/maps", "r");
  if (maps != NULL) {
    char line[512];
    while (fgets(line, sizeof(line), maps) != NULL) {
      fputs(line, f);
    }
    fclose(maps);
  }
}
#endif

static void mi_pprof_write_mapped_libraries(FILE* f) {
  fprintf(f, "MAPPED_LIBRARIES:\n");
  #if defined(__linux__)
  mi_write_mapped_libraries_linux(f);
  #elif defined(_WIN32)
  mi_write_mapped_libraries_win32(f);
  #elif defined(__APPLE__)
  mi_write_mapped_libraries_macos(f);
  #endif
}