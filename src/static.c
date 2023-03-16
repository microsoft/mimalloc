/* ----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#if defined(__sun)
// same remarks as os.c for the static's context.
#undef _XOPEN_SOURCE
#undef _POSIX_C_SOURCE
#endif

#include "mimalloc.h"
#include "mimalloc-internal.h"

// For a static override we create a single object file
// containing the whole library. If it is linked first
// it will override all the standard library allocation
// functions (on Unix's).
#include "alloc.c"
#include "alloc-aligned.c"
#if MI_OSX_ZONE
#include "alloc-override-osx.c"
#endif
#include "alloc-posix.c"
#include "arena.c"
#include "bitmap.c"
#include "heap.c"
#include "init.c"
#include "options.c"
#include "os.c"
#include "page.c"
#include "prim/prim.c"
#include "random.c"
#include "region.c"
#include "segment.c"
#include "stats.c"
