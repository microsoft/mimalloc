/* ----------------------------------------------------------------------------
Copyright (c) 2026-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Tests for the mimalloc pprof heap profiler (src/profile/pprof.c).

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#endif

#include "mimalloc.h"
#include "mimalloc-profile.h"
#include "testhelper.h"

// ---------------------------------------------------------------------------
// The pprof profiler is not (yet) exposed through a public header;
// declare the API here as it is exported from `src/profile/pprof.c`.
// ---------------------------------------------------------------------------
mi_profiler_t* mi_pprof_profiler_new(size_t initial_threshold, const char* base_file_name);
void           mi_pprof_profiler_delete(mi_profiler_t* profiler);
void           mi_pprof_profiler_dump(mi_profiler_t* profiler);

#define TEST_THRESHOLD (1 * 1024)
#define PROFILE_BASE_NAME "test-pprof-profile"

// Build the name of the `seq`-th dump file for `base`, matching the
// `<base>.<seq>.heap` naming used by `mi_pprof_profiler_dump`.
static void pprof_dump_file_name(char* buf, size_t bufsize, const char* base, unsigned seq) {
  snprintf(buf, bufsize, "%s.%04u.heap", base, seq);
}

// Remove any dump files (`<base>.0001.heap` .. `<base>.<max_seq>.heap`) left behind.
static void pprof_remove_dump_files(const char* base, unsigned max_seq) {
  char fname[1024];
  for (unsigned seq = 1; seq <= max_seq; seq++) {
    pprof_dump_file_name(fname, sizeof(fname), base, seq);
    remove(fname);
  }
}

// Allocate and free enough memory to be reasonably sure that samples were taken.
static void allocate_and_free(size_t n, size_t block_size) {
  for (size_t i = 0; i < n; i++) {
    void* p = mi_malloc(block_size + (i % 37));
    mi_free(p);
  }
}

// ---------------------------------------------------------------------------
// Ten distinct allocation call sites: each `alloc_site_N` below has its own
// `mi_malloc` call at a unique source location so the captured call stack
// (and thus the pprof location) differs between sites. Each site only
// allocates; freeing is managed by the caller (see `allocate_and_free_multi_site`)
// so that some allocations are still alive (i.e. positive inuse counts) at
// the point of an intermediate dump.
// ---------------------------------------------------------------------------
#define MI_TEST_PPROF_SITE_COUNT 10

// Written to (but never meaningfully read) after each allocation below, purely
// to give the `mi_malloc` call a side effect that must happen before the
// function returns. Without this, an optimizing (Release) compiler can turn
// `return mi_malloc(...);` into a tail call, which reuses the caller's return
// address instead of pushing a new stack frame -- collapsing what should be
// distinct call-site backtraces from `alloc_site_0`..`alloc_site_9` into far
// fewer distinct ones (or even just one).
static void* volatile mi_test_pprof_sink;

#define MI_TEST_PPROF_DEFINE_SITE(N) \
  static void* alloc_site_##N(size_t block_size, size_t i) { \
    void* p = mi_malloc(block_size + (i % 37)); \
    mi_test_pprof_sink = p; \
    return p; \
  }

MI_TEST_PPROF_DEFINE_SITE(0)
MI_TEST_PPROF_DEFINE_SITE(1)
MI_TEST_PPROF_DEFINE_SITE(2)
MI_TEST_PPROF_DEFINE_SITE(3)
MI_TEST_PPROF_DEFINE_SITE(4)
MI_TEST_PPROF_DEFINE_SITE(5)
MI_TEST_PPROF_DEFINE_SITE(6)
MI_TEST_PPROF_DEFINE_SITE(7)
MI_TEST_PPROF_DEFINE_SITE(8)
MI_TEST_PPROF_DEFINE_SITE(9)

typedef void* (*mi_test_alloc_site_fun_t)(size_t block_size, size_t i);

static const mi_test_alloc_site_fun_t alloc_sites[MI_TEST_PPROF_SITE_COUNT] = {
  &alloc_site_0, &alloc_site_1, &alloc_site_2, &alloc_site_3, &alloc_site_4,
  &alloc_site_5, &alloc_site_6, &alloc_site_7, &alloc_site_8, &alloc_site_9
};

// Number of allocations to keep alive (per site, per call) before freeing;
// this needs to comfortably exceed the sampling gap (allocations are only
// sampled roughly every `TEST_THRESHOLD / block_size` bytes) so that at any
// point in time -- including at the intermediate dump halfway through --
// enough *sampled* allocations are still kept alive to show positive inuse
// counts. We simply keep every allocation alive until explicitly freed by
// `allocate_and_free_multi_site_cleanup`.
static void** alloc_site_kept = NULL;
static size_t alloc_site_kept_count = 0;
static size_t alloc_site_kept_capacity = 0;

static void alloc_site_keep_alive(void* p) {
  if (alloc_site_kept_count == alloc_site_kept_capacity) {
    const size_t new_capacity = (alloc_site_kept_capacity == 0 ? 4096 : alloc_site_kept_capacity * 2);
    alloc_site_kept = (void**)mi_realloc(alloc_site_kept, new_capacity * sizeof(void*));
    alloc_site_kept_capacity = new_capacity;
  }
  alloc_site_kept[alloc_site_kept_count++] = p;
}

// Allocate from each of the `MI_TEST_PPROF_SITE_COUNT` distinct call sites,
// `n` times each, so each site accumulates enough bytes to be sampled. None
// of the allocations are freed here (see `alloc_site_keep_alive`); they are
// only freed later by `allocate_and_free_multi_site_cleanup`, so that at any
// point in time -- including at the intermediate dump halfway through --
// there are allocations still in use (i.e. positive inuse counts).
static void allocate_and_free_multi_site(mi_profiler_t* prof, size_t n, size_t block_size) {
  for (size_t site = 0; site < MI_TEST_PPROF_SITE_COUNT; site++) {
    if (site == MI_TEST_PPROF_SITE_COUNT/2) {
      mi_pprof_profiler_dump(prof);
    }
    for (size_t i = 0; i < n; i++) {
      void* p = (*alloc_sites[site])(block_size, i);
      alloc_site_keep_alive(p);
    }
  }
}

// Free all allocations kept alive by `alloc_site_keep_alive` (see above).
static void allocate_and_free_multi_site_cleanup(void) {
  for (size_t i = 0; i < alloc_site_kept_count; i++) {
    mi_free(alloc_site_kept[i]);
  }
  mi_free(alloc_site_kept);
  alloc_site_kept = NULL;
  alloc_site_kept_count = 0;
  alloc_site_kept_capacity = 0;
}

// Count the number of location entry lines in a pprof dump, i.e. lines of the
// form `<inuse>: <inuse_bytes> [<alloc>: <alloc_bytes>] @ <addr1> <addr2> ...`.
static size_t count_location_entries(const char* contents) {
  size_t count = 0;
  const char* p = contents;
  while ((p = strstr(p, "] @ 0x")) != NULL) {
    count++;
    p += 6;
  }
  return count;
}

// Parse the total inuse object count from the pprof header line, i.e. the
// first number in `heap profile: <inuse_objects>: <inuse_bytes> [...] @ heapprofile`.
static int64_t parse_total_inuse_objects(const char* contents) {
  const char* p = (contents == NULL ? NULL : strstr(contents, "heap profile:"));
  if (p == NULL) return -1;
  p += strlen("heap profile:");
  return (int64_t)strtoll(p, NULL, 10);
}

// Read the whole contents of a file into a heap allocated (0-terminated) buffer.
static char* read_file(const char* fname) {
  FILE* f = fopen(fname, "rb");
  if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len < 0) { fclose(f); return NULL; }
  char* buf = (char*)mi_malloc((size_t)len + 1);
  if (buf != NULL) {
    const size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = 0;
  }
  fclose(f);
  return buf;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

bool test_pprof_dump_creates_file(void) {
  CHECK_BODY("pprof: dump creates a <base>.0001.heap file") {
    pprof_remove_dump_files(PROFILE_BASE_NAME, 1);
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD, PROFILE_BASE_NAME);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);
      allocate_and_free(200000, 64);
      mi_profiler_stop(prof);

      mi_pprof_profiler_dump(prof);

      char fname[1024];
      pprof_dump_file_name(fname, sizeof(fname), PROFILE_BASE_NAME, 1);
      char* contents = read_file(fname);
      result = (contents != NULL && contents[0] != 0);
      mi_free(contents);

      mi_profile(NULL);  // unregister before deleting
      mi_pprof_profiler_delete(prof);
    }
    pprof_remove_dump_files(PROFILE_BASE_NAME, 1);
  }
  return true;
}

bool test_pprof_dump_format(void) {
  CHECK_BODY("pprof: dump uses the pprof heap profile text format") {
    pprof_remove_dump_files(PROFILE_BASE_NAME, 1);
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD, PROFILE_BASE_NAME);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);
      allocate_and_free(200000, 64);
      mi_profiler_stop(prof);

      mi_pprof_profiler_dump(prof);

      char fname[1024];
      pprof_dump_file_name(fname, sizeof(fname), PROFILE_BASE_NAME, 1);
      char* contents = read_file(fname);
      result = (contents != NULL &&
                strncmp(contents, "heap profile:", 13) == 0 &&
                strstr(contents, "@ heapprofile") != NULL &&
                strstr(contents, "MAPPED_LIBRARIES:") != NULL);
      mi_free(contents);

      mi_profile(NULL);
      mi_pprof_profiler_delete(prof);
    }
    pprof_remove_dump_files(PROFILE_BASE_NAME, 1);
  }
  return true;
}

bool test_pprof_dump_records_samples(void) {
  CHECK_BODY("pprof: dump records samples from multiple distinct call sites") {
    // `allocate_and_free_multi_site` itself dumps once, halfway through its
    // 10 call sites, so each call produces 2 dump files: one with only the
    // first half of the sites recorded, and one (after the call returns)
    // with all `MI_TEST_PPROF_SITE_COUNT` sites recorded. We call it twice
    // below, for a total of 4 dumps; only the *last* one is guaranteed to
    // have seen every site, so that is the one we check. A 5th, final dump
    // is taken after freeing everything, to check inuse drops back to 0.
    #define MI_TEST_PPROF_RECORDS_DUMP_COUNT 4
    #define MI_TEST_PPROF_FINAL_DUMP_SEQ (MI_TEST_PPROF_RECORDS_DUMP_COUNT + 1)
    pprof_remove_dump_files(PROFILE_BASE_NAME, MI_TEST_PPROF_FINAL_DUMP_SEQ);
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD, PROFILE_BASE_NAME);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);
      // allocate plenty from each of the 10 distinct call sites so every
      // site accumulates enough bytes to trigger at least one sample.
      allocate_and_free_multi_site(prof, 20000, 64);
      mi_pprof_profiler_dump(prof);
      allocate_and_free_multi_site(prof, 20000, 64);
      mi_pprof_profiler_dump(prof);
      allocate_and_free_multi_site_cleanup();  // free any allocations still kept alive by the ring buffers
      mi_pprof_profiler_dump(prof);  // final dump, taken after everything was freed

      // the intermediate dump (seq 1, taken halfway through the first call)
      // should show a positive inuse count: some allocations are still kept
      // alive in the ring buffers at that point.
      char fname1[1024];
      pprof_dump_file_name(fname1, sizeof(fname1), PROFILE_BASE_NAME, 1);
      char* contents1 = read_file(fname1);
      result = (contents1 != NULL && parse_total_inuse_objects(contents1) > 0);
      mi_free(contents1);

      char fname[1024];
      pprof_dump_file_name(fname, sizeof(fname), PROFILE_BASE_NAME, MI_TEST_PPROF_RECORDS_DUMP_COUNT);
      char* contents = read_file(fname);
      // an entry line looks like: `     0:        0 [ 1234:  56789] @ 0x... 0x... ...`
      const size_t entry_count = (contents == NULL ? 0 : count_location_entries(contents));
      result = (result && contents != NULL && entry_count >= MI_TEST_PPROF_SITE_COUNT);
      mi_free(contents);

      // the final dump (taken after everything was freed) should show a
      // total inuse count of 0.
      char fname_final[1024];
      pprof_dump_file_name(fname_final, sizeof(fname_final), PROFILE_BASE_NAME, MI_TEST_PPROF_FINAL_DUMP_SEQ);
      char* contents_final = read_file(fname_final);
      result = (result && contents_final != NULL && parse_total_inuse_objects(contents_final) == 0);
      mi_free(contents_final);

      mi_profile(NULL);
      mi_pprof_profiler_delete(prof);
    }
    // pprof_remove_dump_files(PROFILE_BASE_NAME, MI_TEST_PPROF_FINAL_DUMP_SEQ);
    #undef MI_TEST_PPROF_FINAL_DUMP_SEQ
    #undef MI_TEST_PPROF_RECORDS_DUMP_COUNT
  }
  return true;
}

bool test_pprof_dump_increments_sequence(void) {
  CHECK_BODY("pprof: repeated dumps use an incrementing sequence number") {
    #define MI_TEST_PPROF_DUMP_COUNT 3
    pprof_remove_dump_files(PROFILE_BASE_NAME, MI_TEST_PPROF_DUMP_COUNT);
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD, PROFILE_BASE_NAME);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);

      for (unsigned seq = 1; seq <= MI_TEST_PPROF_DUMP_COUNT && result; seq++) {
        allocate_and_free(20000, 64);
        mi_pprof_profiler_dump(prof);
        char fname[1024];
        pprof_dump_file_name(fname, sizeof(fname), PROFILE_BASE_NAME, seq);
        char* contents = read_file(fname);
        result = (contents != NULL && contents[0] != 0);
        mi_free(contents);
      }

      mi_profiler_stop(prof);
      mi_profile(NULL);
      mi_pprof_profiler_delete(prof);
    }
    pprof_remove_dump_files(PROFILE_BASE_NAME, MI_TEST_PPROF_DUMP_COUNT);
    #undef MI_TEST_PPROF_DUMP_COUNT
  }
  return true;
}

bool test_pprof_profiler_new_delete(void) {
  CHECK_BODY("pprof: profiler can be created and deleted without use") {
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD, PROFILE_BASE_NAME);
    result = (prof != NULL);
    if (prof != NULL) {
      mi_pprof_profiler_delete(prof);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Concurrent test: several threads allocate and free from a shared pool of
// pointers at random, so a block allocated by one thread is often freed by a
// *different* thread -- exercising the thread safety of `on_alloc`/`on_free`
// and the lock-free locations hash table. A few dumps are taken while the
// threads are still running (and are left on disk afterwards for inspection).
// Not available on Windows (no pthreads); guarded by `!defined(_WIN32)`.
// ---------------------------------------------------------------------------
#if !defined(_WIN32)

#define PROFILE_THREADS_BASE_NAME  "test-pprof-profile-threads"
#define MI_TEST_PPROF_THREAD_COUNT      8
#define MI_TEST_PPROF_THREAD_ITERS      50000
#define MI_TEST_PPROF_THREAD_POOL_SIZE  1024
// number of dump files to retain: 3 taken while threads are running, plus 1 final one
#define MI_TEST_PPROF_THREAD_DUMP_COUNT 4

static pthread_mutex_t thread_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static void* thread_pool[MI_TEST_PPROF_THREAD_POOL_SIZE];

typedef struct {
  unsigned seed;
} thread_arg_t;

// Repeatedly pick a random slot in the shared pool: if it holds a block
// (possibly allocated by another thread), free it; otherwise allocate a new
// block and try to install it. The pool is only ever touched while holding
// `thread_pool_mutex`, but the actual `mi_malloc`/`mi_free` calls happen
// outside the lock so that they run genuinely concurrently across threads.
static void* thread_alloc_free_fun(void* varg) {
  thread_arg_t* arg = (thread_arg_t*)varg;
  unsigned seed = arg->seed;
  for (size_t i = 0; i < MI_TEST_PPROF_THREAD_ITERS; i++) {
    const size_t idx = (size_t)(rand_r(&seed) % MI_TEST_PPROF_THREAD_POOL_SIZE);

    pthread_mutex_lock(&thread_pool_mutex);
    void* p = thread_pool[idx];
    if (p != NULL) { thread_pool[idx] = NULL; }
    pthread_mutex_unlock(&thread_pool_mutex);

    if (p != NULL) {
      mi_free(p);  // may free a block that was allocated by a different thread
    }
    else {
      const size_t size = 8 + (rand_r(&seed) % 249);
      void* np = mi_malloc(size);
      pthread_mutex_lock(&thread_pool_mutex);
      if (thread_pool[idx] == NULL) {
        thread_pool[idx] = np;
        np = NULL;
      }
      pthread_mutex_unlock(&thread_pool_mutex);
      if (np != NULL) { mi_free(np); }  // slot got filled by another thread meanwhile
    }
  }
  return NULL;
}

// Free any blocks still left in the shared pool (e.g. if a thread allocated
// but the corresponding free never got scheduled before the threads stopped).
static void thread_pool_cleanup(void) {
  for (size_t i = 0; i < MI_TEST_PPROF_THREAD_POOL_SIZE; i++) {
    mi_free(thread_pool[i]);
    thread_pool[i] = NULL;
  }
}

bool test_pprof_concurrent_threads(void) {
  CHECK_BODY("pprof: dump is thread safe with concurrently allocating/freeing threads") {
    pprof_remove_dump_files(PROFILE_THREADS_BASE_NAME, MI_TEST_PPROF_THREAD_DUMP_COUNT);
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD, PROFILE_THREADS_BASE_NAME);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);

      pthread_t threads[MI_TEST_PPROF_THREAD_COUNT];
      thread_arg_t args[MI_TEST_PPROF_THREAD_COUNT];
      for (int t = 0; t < MI_TEST_PPROF_THREAD_COUNT && result; t++) {
        args[t].seed = (unsigned)(t + 1);
        result = (pthread_create(&threads[t], NULL, &thread_alloc_free_fun, &args[t]) == 0);
      }

      // take a few dumps while the threads are still running
      for (unsigned seq = 1; seq < MI_TEST_PPROF_THREAD_DUMP_COUNT; seq++) {
        usleep(2000);  // 2ms
        mi_pprof_profiler_dump(prof);
      }

      for (int t = 0; t < MI_TEST_PPROF_THREAD_COUNT; t++) {
        pthread_join(threads[t], NULL);
      }
      thread_pool_cleanup();

      mi_pprof_profiler_dump(prof);  // final dump, after all threads have finished

      // every retained dump should be a valid, well formed pprof dump with a
      // non-negative inuse count (a negative inuse count would indicate a
      // race in the alloc/free counters).
      for (unsigned seq = 1; seq <= MI_TEST_PPROF_THREAD_DUMP_COUNT && result; seq++) {
        char fname[1024];
        pprof_dump_file_name(fname, sizeof(fname), PROFILE_THREADS_BASE_NAME, seq);
        char* contents = read_file(fname);
        result = (contents != NULL &&
                  strncmp(contents, "heap profile:", 13) == 0 &&
                  strstr(contents, "MAPPED_LIBRARIES:") != NULL &&
                  parse_total_inuse_objects(contents) >= 0);
        mi_free(contents);
      }

      mi_profiler_stop(prof);
      mi_profile(NULL);
      mi_pprof_profiler_delete(prof);
    }
    // dump files are intentionally left on disk for inspection.
  }
  return true;
}

#endif // !defined(_WIN32)

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
  test_pprof_profiler_new_delete();
  test_pprof_dump_creates_file();
  test_pprof_dump_format();
  test_pprof_dump_increments_sequence();
  // last test leaves the pprof files
  test_pprof_dump_records_samples();
#if !defined(_WIN32)
  // this test also leaves its pprof files
  test_pprof_concurrent_threads();
#endif
  return print_test_summary();
}
