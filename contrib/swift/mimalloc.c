/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Maps the package traits (MI_SWIFT_*, see Package.swift) to mimalloc defines.
// A define passed through MIMALLOC_DEFINES takes precedence.

#if defined(MI_SWIFT_DEBUG)
  #if !defined(MI_DEBUG)
    #define MI_DEBUG 2
  #endif
#elif !defined(MI_DEBUG) && !defined(NDEBUG)
  #define NDEBUG 1  // include/mimalloc/decl.h then sets MI_DEBUG 0
#endif

// Traits are additive, so the highest enabled level wins.
#if !defined(MI_SECURE)
  #if defined(MI_SWIFT_SECURE)
    #define MI_SECURE 4
  #elif defined(MI_SWIFT_SECURE_CHECKED)
    #define MI_SECURE 2
  #elif defined(MI_SWIFT_SECURE_BASIC)
    #define MI_SECURE 1
  #endif
#endif

#if defined(__linux__) && !defined(__ANDROID__)
  // Before <limits.h> below, which latches the glibc feature set.
  #if !defined(_DEFAULT_SOURCE)
    #define _DEFAULT_SOURCE
  #endif
  #include <limits.h>  // defines __GLIBC__ on glibc
  #if !defined(__GLIBC__) && !defined(MI_LIBC_MUSL)
    #define MI_LIBC_MUSL 1
  #endif
#endif

// Sanitizers replace malloc themselves.
#if defined(__has_feature)
  #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    #define MI_SKIP_OVERRIDE 1
  #endif
#endif

#if defined(MI_SWIFT_OVERRIDE) && !defined(MI_SKIP_OVERRIDE) && !defined(_WIN32)
  // Unix exports malloc/free from the executable; Apple platforms replace the default malloc zone.
  #if !defined(MI_MALLOC_OVERRIDE)
    #define MI_MALLOC_OVERRIDE 1
  #endif
  #if defined(__APPLE__) && !defined(MI_OSX_ZONE)
    #define MI_OSX_ZONE 1
  #endif
#endif

#if defined(__has_feature)
  #if __has_feature(thread_sanitizer)
    // mimalloc's lock-free internals and exit teardown are reported as races (mi_process_done_once).
    #pragma clang attribute push (__attribute__((no_sanitize("thread"))), apply_to = function)
    #define MI_TSAN_POP 1
  #endif
#endif

#include "../../src/static.c"

#if defined(MI_TSAN_POP)
  #pragma clang attribute pop
#endif
