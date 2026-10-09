/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_H
#define MI_INTERNAL_H

// --------------------------------------------------------------------------
// This file contains the internal API's of mimalloc and various utility
// functions and macros.
// --------------------------------------------------------------------------

#include "types.h"
#include "track.h"

// --------------------------------------------------------------------------
// Internal functions
// --------------------------------------------------------------------------

#include "internal/libc.h"
#include "internal/options.h"
#include "internal/random.h"
#include "internal/prim.h"
#include "internal/prim-tls.h"
#include "internal/subproc.h"
#include "internal/init.h"
#include "internal/os.h"
#include "internal/threadlocal.h"
#include "internal/arena.h"
#include "internal/arena-alloc.h"
#include "internal/arena-page.h"
#include "internal/page-map.h"
#include "internal/page.h"
#include "internal/theap.h"
#include "internal/heap.h"
#include "internal/stats.h"
#include "internal/alloc.h"
#include "internal/free.h"
#include "internal/sample-profile.h"
#include "internal/sample-guarded.h"
#include "internal/pprof.h"
#include "internal/memid.h"

#endif  // MI_INTERNAL_H
