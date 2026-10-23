/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license.
-----------------------------------------------------------------------------*/

// Run the regular stress test while the pprof heap profiler (in the modern
// protobuf format) is attached and takes a dump after each iteration.
#define TEST_STRESS_PPROF    1

#include "test-stress.c"
