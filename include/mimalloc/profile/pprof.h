/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_PPROF_H
#define MI_INTERNAL_PPROF_H

#include "../types.h"

void  mi_profiler_init(void);      // in `src/profile/pprof.c`: starts a `MIMALLOC_PROFILE`-driven profiler if that environment variable is set
void  mi_profile_done(void);       // in `src/profile/pprof.c`: stops/dumps/deletes the profiler started by `mi_profiler_init` (if any)
void  _mi_pprof_profiler_init(void);
void  _mi_pprof_profiler_done(void);

#endif
