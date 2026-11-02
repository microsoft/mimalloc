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
#include "../prim/prim-pac.h"

mi_arena_id_t _mi_arena_id_none(void);
mi_arena_t*   _mi_arena_from_id(mi_arena_id_t id);
bool          _mi_arena_memid_is_suitable(mi_memid_t memid, mi_arena_t* request_arena);

void          _mi_arenas_unsafe_destroy_all(mi_subproc_t* subproc);

mi_heap_t*    _mi_arena_heap_main(const mi_arena_t* arena);
size_t        _mi_slice_count_of_size(size_t size);
size_t        _mi_size_of_slices(size_t bcount);

// The arena of an arena memid (signed with the slice index as the salt since a memid is copied by value)
static inline mi_addr_t mi_memid_arena_salt(const mi_memid_arena_info_t* info) {
  return mi_salt_of(info->slice_index);
}

static inline mi_arena_t* mi_memid_arena(mi_memid_t memid) {
  return (memid.memkind == MI_MEM_ARENA ? (mi_arena_t*)mi_ptr_auth(memid.mem.arena.arena, mi_memid_arena_salt(&memid.mem.arena)) : NULL);
}

// Start of the arena memory area
static inline uint8_t* mi_arena_start(mi_arena_t* arena) {
  return (uint8_t*)mi_ptr_auth_at(void, &arena->start);
}

// The parent of a sub-arena (or NULL)
static inline mi_arena_t* mi_arena_parent(mi_arena_t* arena) {
  return mi_ptr_auth_at(mi_arena_t, &arena->parent);
}

// The sub-process of an arena
static inline mi_subproc_t* mi_arena_subproc(const mi_arena_t* arena) {
  return mi_ptr_auth_at(mi_subproc_t, &arena->subproc);
}

// The arena bitmaps
static inline mi_bbitmap_t* mi_arena_slices_free(const mi_arena_t* arena) {
  return mi_ptr_auth_at(mi_bbitmap_t, &arena->slices_free);
}

static inline mi_bitmap_t* mi_arena_slices_committed(const mi_arena_t* arena) {
  return mi_ptr_auth_at(mi_bitmap_t, &arena->slices_committed);
}

static inline mi_bitmap_t* mi_arena_slices_dirty(const mi_arena_t* arena) {
  return mi_ptr_auth_at(mi_bitmap_t, &arena->slices_dirty);
}

static inline mi_bitmap_t* mi_arena_slices_purge(const mi_arena_t* arena) {
  return mi_ptr_auth_at(mi_bitmap_t, &arena->slices_purge);
}

// The separated page meta info of an arena (or NULL)
static inline mi_page_t* mi_arena_pages_meta(const mi_arena_t* arena) {
  return mi_ptr_auth_at(mi_page_t, &arena->pages_meta);
}

// The bitmap of all registered pages of a heap in an arena
static inline mi_bitmap_t* mi_arena_pages_registered(const mi_arena_pages_t* arena_pages) {
  return mi_ptr_auth_at(mi_bitmap_t, &arena_pages->pages);
}

// The bitmap of abandoned pages of a heap in an arena for a size bin
static inline mi_bitmap_t* mi_arena_pages_abandoned(const mi_arena_pages_t* arena_pages, size_t bin) {
  return mi_ptr_auth_at(mi_bitmap_t, &arena_pages->pages_abandoned[bin]);
}

#endif
