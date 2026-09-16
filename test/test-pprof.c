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
#include <string.h>

#include "mimalloc.h"
#include "mimalloc-profile.h"
#include "testhelper.h"

// ---------------------------------------------------------------------------
// The pprof profiler is not (yet) exposed through a public header;
// declare the API here as it is exported from `src/profile/pprof.c`.
// ---------------------------------------------------------------------------
mi_profiler_t* mi_pprof_profiler_new(size_t initial_threshold);
void           mi_pprof_profiler_delete(mi_profiler_t* profiler);
void           mi_pprof_profiler_dump(mi_profiler_t* profiler, const char* base_file_name);

#define TEST_THRESHOLD (16 * 1024)
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
// (and thus the pprof location) differs between sites.
// ---------------------------------------------------------------------------
#define MI_TEST_PPROF_SITE_COUNT 10

#define MI_TEST_PPROF_DEFINE_SITE(N) \
  static void alloc_site_##N(size_t block_size, size_t i) { \
    void* p = mi_malloc(block_size + (i % 37)); \
    mi_free(p); \
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

typedef void (*mi_test_alloc_site_fun_t)(size_t block_size, size_t i);

static const mi_test_alloc_site_fun_t alloc_sites[MI_TEST_PPROF_SITE_COUNT] = {
  &alloc_site_0, &alloc_site_1, &alloc_site_2, &alloc_site_3, &alloc_site_4,
  &alloc_site_5, &alloc_site_6, &alloc_site_7, &alloc_site_8, &alloc_site_9
};

// Allocate and free from each of the `MI_TEST_PPROF_SITE_COUNT` distinct call
// sites, `n` times each, so each site accumulates enough bytes to be sampled.
static void allocate_and_free_multi_site(size_t n, size_t block_size) {
  for (size_t site = 0; site < MI_TEST_PPROF_SITE_COUNT; site++) {
    for (size_t i = 0; i < n; i++) {
      (*alloc_sites[site])(block_size, i);
    }
  }
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
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);
      allocate_and_free(200000, 64);
      mi_profiler_stop(prof);

      mi_pprof_profiler_dump(prof, PROFILE_BASE_NAME);

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
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);
      allocate_and_free(200000, 64);
      mi_profiler_stop(prof);

      mi_pprof_profiler_dump(prof, PROFILE_BASE_NAME);

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
    pprof_remove_dump_files(PROFILE_BASE_NAME, 1);
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);
      // allocate plenty from each of the 10 distinct call sites so every
      // site accumulates enough bytes to trigger at least one sample.
      allocate_and_free_multi_site(20000, 64);
      mi_profiler_stop(prof);      
      mi_pprof_profiler_dump(prof, PROFILE_BASE_NAME);
      mi_profiler_start(prof);
      allocate_and_free_multi_site(20000, 64);
      mi_pprof_profiler_dump(prof, PROFILE_BASE_NAME);

      char fname[1024];
      pprof_dump_file_name(fname, sizeof(fname), PROFILE_BASE_NAME, 1);
      char* contents = read_file(fname);
      // an entry line looks like: `     0:        0 [ 1234:  56789] @ 0x... 0x... ...`
      const size_t entry_count = (contents == NULL ? 0 : count_location_entries(contents));
      result = (contents != NULL && entry_count >= MI_TEST_PPROF_SITE_COUNT);
      mi_free(contents);

      mi_profile(NULL);
      mi_pprof_profiler_delete(prof);
    }
    // pprof_remove_dump_files(PROFILE_BASE_NAME, 1);
  }
  return true;
}

bool test_pprof_dump_increments_sequence(void) {
  CHECK_BODY("pprof: repeated dumps use an incrementing sequence number") {
    #define MI_TEST_PPROF_DUMP_COUNT 3
    pprof_remove_dump_files(PROFILE_BASE_NAME, MI_TEST_PPROF_DUMP_COUNT);
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD);
    result = (prof != NULL);
    if (result) {
      mi_profile(prof);
      mi_profiler_start(prof);

      for (unsigned seq = 1; seq <= MI_TEST_PPROF_DUMP_COUNT && result; seq++) {
        allocate_and_free(20000, 64);
        mi_pprof_profiler_dump(prof, PROFILE_BASE_NAME);
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
    mi_profiler_t* prof = mi_pprof_profiler_new(TEST_THRESHOLD);
    result = (prof != NULL);
    if (prof != NULL) {
      mi_pprof_profiler_delete(prof);
    }
  }
  return true;
}

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
  return print_test_summary();
}
