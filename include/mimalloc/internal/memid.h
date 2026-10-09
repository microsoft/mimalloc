/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_MEMID_H
#define MI_INTERNAL_MEMID_H

#include "../types.h"
#include "arena.h"

static inline mi_memid_t mi_memid_create(mi_memkind_t memkind) {
  mi_memid_t memid;
  _mi_memzero_var(memid);
  memid.memkind = memkind;
  return memid;
}

static inline mi_memid_t mi_memid_none(void) {
  return mi_memid_create(MI_MEM_NONE);
}

static inline mi_memid_t mi_memid_create_os(void* base, size_t size, bool committed, bool is_zero, bool is_large) {
  mi_memid_t memid = mi_memid_create(MI_MEM_OS);
  memid.mem.os.base = base;
  memid.mem.os.size = size;
  memid.initially_committed = committed;
  memid.initially_zero = is_zero;
  memid.is_pinned = is_large;
  return memid;
}

static inline mi_memid_t mi_memid_create_static(void* p, size_t size) {
  mi_memid_t memid = mi_memid_create(MI_MEM_STATIC);
  memid.mem.malloc.base = p;
  memid.mem.malloc.size = size;
  memid.initially_committed = true;
  memid.is_pinned = true;
  return memid;
}

static inline mi_memid_t mi_memid_create_malloc(void* p, size_t size, bool iszero) {
  mi_memid_t memid = mi_memid_create(MI_MEM_MALLOC);
  memid.mem.malloc.base = p;
  memid.mem.malloc.size = size;
  memid.initially_committed = true;
  memid.initially_zero = iszero;
  memid.is_pinned = true;
  return memid;
}

static inline size_t mi_memid_size(mi_memid_t memid) {
  if (mi_memid_is_os(memid)) {
    return memid.mem.os.size;
  }
  else if (memid.memkind == MI_MEM_ARENA) {
    return mi_size_of_slices(memid.mem.arena.slice_count);
  }
  else if (memid.memkind == MI_MEM_MALLOC) {
    return memid.mem.malloc.size;
  }
  else {
    mi_assert_internal(mi_memid_needs_no_free(memid));
    return 0;
  }
}

#endif
