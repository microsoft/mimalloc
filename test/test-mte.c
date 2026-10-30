/* ----------------------------------------------------------------------------
Copyright (c) 2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Check that memory tagging (MTE) catches invalid accesses: each bad access runs in a
// forked child that should be terminated by a signal (SIGSEGV on Linux, SIGKILL on macOS).
// The test is skipped if tagging is not active at runtime, unless `MI_TEST_REQUIRE_MTE` is set.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <mimalloc.h>
#include "testhelper.h"

#if !defined(__aarch64__) || defined(_WIN32)
int main(void) {
  printf("mte test skipped on this platform\n");
  return 0;
}
#else
#include <unistd.h>
#include <sys/wait.h>

static size_t ptr_tag(const void* p) {
  return (((uintptr_t)p >> 56) & 0x0F);
}

// Run `fun` in a child process and return true if the child was terminated by a signal
static bool faults(void (*fun)(void)) {
  fflush(stdout); fflush(stderr);
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    fun();
    _exit(0);  // the syscall also delivers a pending asynchronous tag fault
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) return false;
  return WIFSIGNALED(status);
}

static void use_after_free(void) {
  volatile uint8_t* p = (uint8_t*)mi_malloc(64);
  p[0] = 1;
  mi_free((void*)p);
  p[0] = 2;
}

static void overflow(void) {
  volatile uint8_t* p = (uint8_t*)mi_malloc(64);
  for (size_t i = 0; i < 256; i++) { p[i] = 1; }  // overflows into the next block (beyond any debug padding)
}

static void* thread_use_after_free(void* arg) {
  (void)arg;
  use_after_free();
  return NULL;
}

static void thread_fault(void) {
  pthread_t t;
  if (pthread_create(&t, NULL, &thread_use_after_free, NULL) == 0) {
    pthread_join(t, NULL);
  }
}

static void valid_access(void) {
  uint8_t* p = (uint8_t*)mi_malloc(64);
  volatile uint8_t* q = p;
  for (size_t i = 0; i < mi_usable_size(p); i++) { q[i] = (uint8_t)i; }
  mi_free(p);
}

// adjacent blocks should always have a different tag
static bool adjacent_differ(void) {
  enum { N = 256 };
  void* ps[N];
  bool ok = true;
  for (int i = 0; i < N; i++) { ps[i] = mi_malloc(64); }
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      const uintptr_t a = (uintptr_t)ps[i] & ~((uintptr_t)0xFF << 56);
      const uintptr_t b = (uintptr_t)ps[j] & ~((uintptr_t)0xFF << 56);
      if (a + 64 == b && ptr_tag(ps[i]) == ptr_tag(ps[j])) { ok = false; }
    }
  }
  for (int i = 0; i < N; i++) { mi_free(ps[i]); }
  return ok;
}

int main(void) {
  void* p = mi_malloc(64);
  const bool active = (ptr_tag(p) != 0);
  mi_free(p);
  if (!active) {
    const bool required = (getenv("MI_TEST_REQUIRE_MTE") != NULL);
    fprintf(stderr, "memory tagging is not active%s\n", (required ? " (but required)" : "; test skipped"));
    return (required ? 1 : 0);
  }
  CHECK("valid-access", !faults(&valid_access));
  CHECK("adjacent-tags", adjacent_differ());
  CHECK("use-after-free", faults(&use_after_free));
  CHECK("overflow", faults(&overflow));
  CHECK("thread-use-after-free", faults(&thread_fault));
  return print_test_summary();
}
#endif
