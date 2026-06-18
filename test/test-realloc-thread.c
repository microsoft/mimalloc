/* ----------------------------------------------------------------------------
Copyright (c) 2018-2025, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/*
Regression test for issue #1304: a `realloc` that is the *first* mimalloc call
on a freshly created thread must not crash.

On platforms with a fixed or dynamic TLS slot (macOS, Windows, OpenBSD) the
thread-local default theap is NULL until the first allocation lazily
initializes it (`MI_THEAP_INITASNULL`). `mi_malloc` tolerates a NULL theap, but
the in-place fast path of `_mi_theap_realloc_zero` used to evaluate
`_mi_theap_heap(theap)` unconditionally, dereferencing the NULL theap and
segfaulting when a fresh thread's first call was an in-place-fitting
`mi_realloc` of a block allocated on another thread.
See https://github.com/microsoft/mimalloc/issues/1304
*/

#include <stdbool.h>
#include <string.h>
#include <mimalloc.h>
#include "testhelper.h"

// ---------------------------------------------------------------------------
// Minimal portable launcher: run `body` once on a brand new thread and join.
// Threads run sequentially (each joined before the next), so a file-scope
// callback pointer is sufficient and keeps the worker's first statement the
// allocation under test.
// ---------------------------------------------------------------------------
static void (*thread_body)(void);

#if defined(_WIN32)
#include <windows.h>
static DWORD WINAPI thread_entry(LPVOID arg) { (void)arg; thread_body(); return 0; }
static void run_in_fresh_thread(void (*body)(void)) {
  thread_body = body;
  HANDLE h = CreateThread(NULL, 0, &thread_entry, NULL, 0, NULL);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
}
#else
#include <pthread.h>
static void* thread_entry(void* arg) { (void)arg; thread_body(); return NULL; }
static void run_in_fresh_thread(void (*body)(void)) {
  thread_body = body;
  pthread_t t;
  pthread_create(&t, NULL, &thread_entry, NULL);
  pthread_join(t, NULL);
}
#endif

// ---------------------------------------------------------------------------
// Shared state between the main thread and the worker.
// ---------------------------------------------------------------------------
static void* shared_block;     // allocated on the main thread, realloc'd on the worker
static void* worker_result;    // pointer returned by the worker's realloc
static bool  worker_content_ok;

// in-place-fitting realloc (48 in [32,64]): this is the branch that used to
// dereference the NULL theap on a fresh thread before the fix.
static void body_realloc_inplace_first(void) {
  worker_result = mi_realloc(shared_block, 48);   // <-- FIRST mimalloc call on this thread
  worker_content_ok = (worker_result != NULL && memcmp(worker_result, "1304", 4) == 0);
}

// growing realloc as the first call: always took the slow path, but exercise it too.
static void body_realloc_grow_first(void) {
  worker_result = mi_realloc(shared_block, 4096); // <-- FIRST mimalloc call on this thread
  worker_content_ok = (worker_result != NULL && memcmp(worker_result, "1304", 4) == 0);
}

int main(void) {
  mi_option_disable(mi_option_verbose);

  // in-place-fitting realloc as the first allocation on a fresh thread (issue #1304)
  shared_block = mi_malloc(64);
  memcpy(shared_block, "1304", 4);
  worker_result = NULL; worker_content_ok = false;
  run_in_fresh_thread(&body_realloc_inplace_first);
  CHECK("realloc-inplace-first-no-crash", worker_result != NULL);
  CHECK("realloc-inplace-first-content", worker_content_ok);
  mi_free(worker_result);

  // growing realloc as the first allocation on a fresh thread
  shared_block = mi_malloc(64);
  memcpy(shared_block, "1304", 4);
  worker_result = NULL; worker_content_ok = false;
  run_in_fresh_thread(&body_realloc_grow_first);
  CHECK("realloc-grow-first-no-crash", worker_result != NULL);
  CHECK("realloc-grow-first-content", worker_content_ok);
  mi_free(worker_result);

  return print_test_summary();
}
