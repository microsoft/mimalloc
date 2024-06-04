/* ----------------------------------------------------------------------------
Copyright (c) 2019-2024, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_ARENA_H
#define MI_ARENA_H

/* ----------------------------------------------------------------------------
"Arenas" are fixed area's of OS memory from which we can allocate
large blocks (>= MI_ARENA_MIN_BLOCK_SIZE, 4MiB).
In contrast to the rest of mimalloc, the arenas are shared between
threads and need to be accessed using atomic operations.

Arenas are also used to for huge OS page (1GiB) reservations or for reserving
OS memory upfront which can be improve performance or is sometimes needed
on embedded devices. We can also employ this with WASI or `sbrk` systems
to reserve large arenas upfront and be able to reuse the memory more effectively.

The arena allocation needs to be thread safe and we use an atomic bitmap to allocate.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "bitmap.h"

/* -----------------------------------------------------------
  Arena allocation
----------------------------------------------------------- */

// A memory arena descriptor
typedef struct mi_arena_s {
  mi_arena_id_t       id;                   // arena id; 0 for non-specific
  mi_memid_t          memid;                // memid of the memory area
  _Atomic(uint8_t*)   start;                // the start of the memory area
  size_t              block_count;          // size of the area in arena blocks (of `MI_ARENA_BLOCK_SIZE`)
  size_t              field_count;          // number of bitmap fields (where `field_count * MI_BITMAP_FIELD_BITS >= block_count`)
  size_t              meta_size;            // size of the arena structure itself (including its bitmaps)
  mi_memid_t          meta_memid;           // memid of the arena structure itself (OS or static allocation)
  int                 numa_node;            // associated NUMA node
  bool                exclusive;            // only allow allocations if specifically for this arena
  bool                is_large;             // memory area consists of large- or huge OS pages (always committed)
  mi_lock_t           abandoned_visit_lock; // lock is only used when abandoned segments are being visited
  _Atomic(size_t)     search_idx;           // optimization to start the search for free blocks
  _Atomic(mi_msecs_t) purge_expire;         // expiration time when blocks should be decommitted from `blocks_decommit`.
  mi_bitmap_field_t*  blocks_dirty;         // are the blocks potentially non-zero?
  mi_bitmap_field_t*  blocks_committed;     // are the blocks committed? (can be NULL for memory that cannot be decommitted)
  mi_bitmap_field_t*  blocks_purge;         // blocks that can be (reset) decommitted. (can be NULL for memory that cannot be (reset) decommitted)
  mi_bitmap_field_t*  blocks_abandoned;     // blocks that start with an abandoned segment. (This crosses API's but it is convenient to have here)
  mi_bitmap_field_t   blocks_inuse[1];      // in-place bitmap of in-use blocks (of size `field_count`)
  // do not add further fields here as the dirty, committed, purged, and abandoned bitmaps follow the inuse bitmap fields.
} mi_arena_t;


// Minimal exports for arena-abandoned.
size_t      mi_arena_id_index(mi_arena_id_t id);
mi_arena_t* mi_arena_from_index(size_t idx);
size_t      mi_arena_get_count(void);
void*       mi_arena_block_start(mi_arena_t* arena, mi_bitmap_index_t bindex);
bool        mi_arena_memid_indices(mi_memid_t memid, size_t* arena_index, mi_bitmap_index_t* bitmap_index);

#endif