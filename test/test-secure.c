/* ----------------------------------------------------------------------------
Copyright (c) 2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Test the memory protections in secure mode (`MI_SECURE`): each test accesses protected memory
// in a forked child process that is expected to fault (so these tests are skipped on Windows).
// See `MI_SECURE_STATIC_SEALED` in `heap/process.h` and `mi_theap_page_guards` in `arena/arena-page.c`.
#include "mimalloc.h"
#include "mimalloc/alloc/page.h"
#include "mimalloc/heap/process.h"
#include "mimalloc/prim/os.h"
#include "testhelper.h"

#if !defined(_WIN32) && !MI_TRACK_ENABLED
#define MI_TEST_FAULTS  (1)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#define MI_TEST_FAULTS  (0)
#endif

#if MI_SECURE_STATIC_SEALED && !MI_SECURE_NO_STATIC_GUARD
#define MI_TEST_STATIC_GUARD  (1)
#else
#define MI_TEST_STATIC_GUARD  (0)
#endif

// Run a test only if the condition holds (and skip it otherwise)
#define CHECK_IF(name,cond,reason,expr) \
  fprintf(stderr,"test: %s...  ", name ); \
  errno = 0; \
  for(bool done = (!(cond) && check_skip(reason)), result = true; !done; done = check_result(result,name,__FILE__,__LINE__)) { result = (expr); }


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#if MI_TEST_FAULTS
// Return `true` if reading (or writing) at `p` faults in a child process.
static bool test_access_faults(void* p, bool write) {
  fflush(NULL);
  const pid_t pid = fork();
  if (pid == 0) {
    volatile uint8_t* const q = (volatile uint8_t*)p;
    const uint8_t x = *q;
    if (write) { *q = x; }
    _exit(0);
  }
  int status = 0;
  return (pid > 0 && waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
}
#else
static bool test_access_faults(void* p, bool write) {
  MI_UNUSED(p); MI_UNUSED(write);
  return true;
}
#endif

// Can the static process data be protected? (it cannot if the OS page size is larger than expected; this warns at startup)
static bool static_pages_fit(void) {
  return ((MI_OS_PROBABLE_PAGE_SIZE % _mi_os_page_size()) == 0);
}


// ---------------------------------------------------------------------------
// Static process data
// ---------------------------------------------------------------------------

// The sealed process data is read-only
static bool test_sealed_readonly(void) {
  if (!mi_process_get_sealed_once()->is_protected) return false;
  return (test_access_faults(mi_process_get_sealed_once(), true) &&
          test_access_faults(mi_process_get_sealed(), true));
}

// The static process data is surrounded by guard pages
static bool test_static_guard_front(void) {
  return test_access_faults((uint8_t*)mi_process_get_sealed_once() - 1, false);
}

static bool test_static_guard_end(void) {
  return test_access_faults(mi_align_up_ptr(&_mi_process.guard_end, _mi_os_page_size()), false);
}


// ---------------------------------------------------------------------------
// Huge blocks (and with `MI_SECURE=FULL` all mimalloc pages) are followed by a guard page
// (at the end of the mimalloc page, so a linear overflow faults before reaching other data)
// ---------------------------------------------------------------------------

static bool test_page_guard(size_t size, size_t alignment) {
  uint8_t* const p = (uint8_t*)(alignment == 0 ? mi_malloc(size) : mi_malloc_aligned(size, alignment));
  if (p == NULL) return false;
  const size_t usable = mi_usable_size(p);
  memset(p, 0, usable);  // all usable memory is accessible
  const mi_page_t* const page = mi_ptr_page(p);
  uint8_t* const guard = mi_page_slice_start(page) + mi_page_guard_end_offset(page);
  const bool ok = (usable >= size && page->memid.guard_end && guard >= p + usable && test_access_faults(guard, false));
  mi_free(p);
  return ok;
}


// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  mi_free(mi_malloc(8));  // initialize
  const bool faults = (MI_TEST_FAULTS != 0);
  const bool static_sealed = (MI_SECURE_STATIC_SEALED && static_pages_fit());
  const bool static_guard  = (static_sealed && MI_TEST_STATIC_GUARD && faults);
  const bool huge_guard    = (MI_SECURE >= 1 && _mi_os_secure_guard_page_size() > 0 && faults);
  const bool page_guard    = (MI_SECURE >= 5 && huge_guard);

  CHECK_IF("sealed-readonly", static_sealed, "no sealed process data", test_sealed_readonly());
  CHECK_IF("static-guard-front", static_guard, "no static guard pages", test_static_guard_front());
  CHECK_IF("static-guard-end", static_guard, "no static guard pages", test_static_guard_end());
  CHECK_IF("huge-guard", huge_guard, "no guard pages", test_page_guard(MI_LARGE_MAX_OBJ_SIZE + 1, 0));
  CHECK_IF("huge-guard-unaligned-size", huge_guard, "no guard pages", test_page_guard(4*MI_MiB + 17, 0));
  CHECK_IF("huge-guard-aligned", huge_guard, "no guard pages", test_page_guard(MI_LARGE_MAX_OBJ_SIZE + 1, 1*MI_MiB));
  CHECK_IF("small-page-guard", page_guard, "no guard pages per page", test_page_guard(64, 0));
  CHECK_IF("medium-page-guard", page_guard, "no guard pages per page", test_page_guard(MI_SMALL_MAX_OBJ_SIZE + 1, 0));
  CHECK_IF("large-page-guard", page_guard, "no guard pages per page", test_page_guard(MI_LARGE_MAX_OBJ_SIZE, 0));

  return print_test_summary();
}
