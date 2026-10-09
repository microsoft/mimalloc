/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_ARENA_H
#define MI_INTERNAL_ARENA_H

#include "../types.h"
#include "init.h"
#include "libc.h"

mi_arena_id_t _mi_arena_id_none(void);
mi_arena_t*   _mi_arena_from_id(mi_arena_id_t id);
bool          _mi_arena_memid_is_suitable(mi_memid_t memid, mi_arena_t* request_arena);

void          _mi_arenas_unsafe_destroy_all(mi_subproc_t* subproc);

static inline mi_heap_t* mi_arena_heap_main(const mi_arena_t* arena) {
  return _mi_subproc_heap_main(arena->subproc);
}

/* -----------------------------------------------------------
  arena blocks
----------------------------------------------------------- */

// Blocks needed for a given byte size
static inline size_t mi_slice_count_of_size(size_t size) {
  return mi_divide_up(size, MI_ARENA_SLICE_SIZE);
}

// Byte size of a number of blocks
static inline size_t mi_size_of_slices(size_t bcount) {
  return (bcount * MI_ARENA_SLICE_SIZE);
}

#endif
