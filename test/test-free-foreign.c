/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/*
Regression test: `mi_free` must not crash when called on a pointer that was
not allocated by mimalloc. In LD_PRELOAD / override scenarios this is observed
in the wild when libraries with their own internal bookkeeping (e.g. LLVM via
Mesa's `gbm_create_device`) call `free()` on pointers that live on a thread
stack or on a static/TCB page whose pagemap submap entry is NULL.

Every other production allocator (glibc, jemalloc, tcmalloc) tolerates such
calls, and mimalloc's debug/MI_SECURE/macOS paths already did. This test
exercises the release-Linux fast path that historically bypassed the NULL
submap check in `_mi_ptr_page`.
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mimalloc.h"
#include "testhelper.h"

static uint8_t g_static_buffer[4096];

int main(void) {
  mi_option_disable(mi_option_verbose);

  CHECK_BODY("mi_free-NULL") {
    mi_free(NULL);
    result = true;
  };

  CHECK_BODY("mi_free-stack-pointer") {
    uint8_t stack_buffer[256];
    memset(stack_buffer, 0, sizeof(stack_buffer));
    mi_free(&stack_buffer[16]);
    result = true;
  };

  CHECK_BODY("mi_free-static-pointer") {
    memset(g_static_buffer, 0, sizeof(g_static_buffer));
    mi_free(&g_static_buffer[16]);
    result = true;
  };

  CHECK_BODY("mi_free-mmap-style-pointer") {
    uintptr_t fake = (uintptr_t)&g_static_buffer[0] ^ (uintptr_t)0x1000;
    mi_free((void*)fake);
    result = true;
  };

  CHECK_BODY("mi_free-roundtrip-after-foreign") {
    void* p = mi_malloc(128);
    result = (p != NULL);
    if (result) { mi_free(p); }
  };

  return print_test_summary();
}
