/* ----------------------------------------------------------------------------
Copyright (c) 2024-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_PROFILE_H
#define MIMALLOC_PROFILE_H

#include <mimalloc.h>
#include <stdbool.h>  // bool

typedef struct mi_profiler_data_s {
  size_t usable_size;
  size_t requested_size;
  void*  user_data[14];
} mi_profiler_data_t;

typedef size_t (mi_cdecl mi_profiler_on_alloc_fun  )(mi_profiler_data_t* profiler_data, void* ptr, size_t threshold, size_t bytes_since_last_sample, const mi_heap_t* heap, void* profiler_arg);
typedef size_t (mi_cdecl mi_profiler_on_realloc_inplace_fun)(mi_profiler_data_t* profiler_data, void* ptr, size_t old_requested_size, const mi_heap_t* heap, void* profiler_arg);
typedef void   (mi_cdecl mi_profiler_on_free_fun   )(mi_profiler_data_t* profiler_data, void* ptr, const mi_heap_t* heap, void* profiler_arg);

// A profiler
// All fields are considered immutable such that they can be copied and accessed concurrently.
// All fields can be NULL/0.
typedef struct mi_profiler_s {
  void*                       reserved1;          // opaque -- internal use for mimalloc
  void*                       reserved2;          
  void*                       reserved3;
  void*                       profiler_arg;       // opaque profiler state pointer -- passed to each callback
  size_t                      profiler_data_size; // size of required profiler data for each sampled allocation
  mi_profiler_on_alloc_fun*   on_alloc;           // called on a sampled allocation (may be called concurrently)  
  mi_profiler_on_free_fun*    on_free;            // called on when previous sampled allocation is freed (may be called concurrently)
  mi_profiler_on_realloc_inplace_fun* on_realloc_inplace;  // called on in-place reallocation of a previous sampled allocation (may be called concurrently)
} mi_profiler_t;

// Exported definitions
#ifdef __cplusplus
extern "C" {
#endif

mi_decl_export bool mi_heap_profile(mi_heap_t* heap, const mi_profiler_t* profiler);
mi_decl_export bool mi_subproc_profile(mi_subproc_id_t subproc_id, const mi_profiler_t* profiler);
mi_decl_export bool mi_profile(const mi_profiler_t* profiler);

mi_decl_export bool mi_profiler_start(const mi_profiler_t* profiler);
mi_decl_export bool mi_profiler_stop(const mi_profiler_t* profiler);

#ifdef __cplusplus
}
#endif

#endif // MIMALLOC_PROFILE_H
