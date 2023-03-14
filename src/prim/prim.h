/* ----------------------------------------------------------------------------
Copyright (c) 2018-2023, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_PRIM_H
#define MIMALLOC_PRIM_H

// note: on all primitive functions, we always get:
// addr != NULL and page aligned
// size > 0     and page aligned
// 

// OS memory configuration
typedef struct mi_os_mem_config_s {
  size_t  page_size;          // 4KiB
  size_t  large_page_size;    // 2MiB
  size_t  alloc_granularity;  // smallest allocation size (on Windows 64KiB)
  bool    has_overcommit;     // can we reserve more memory than can be actually committed?
  bool    must_free_whole;    // must allocated blocks free as a whole (false for mmap, true for VirtualAlloc)
} mi_os_mem_config_t;

// Initialize
void  _mi_prim_mem_init( mi_os_mem_config_t* config );

// Free OS memory
// pre: addr != NULL, size > 0
void  _mi_prim_free(void* addr, size_t size );
  
// Allocate OS memory.
// The `try_alignment` is just a hint and the returned pointer does not have to be aligned.
// return NULL on error.
// pre: !commit => !allow_large
//      try_alignment >= _mi_os_page_size() and a power of 2
void* _mi_prim_alloc(size_t size, size_t try_alignment, bool commit, bool allow_large, bool* is_large);

// Commit memory. Returns error code or 0 on success.
int   _mi_prim_commit(void* addr, size_t size, bool commit);

// Reset memory. The range keeps being accessible but the content might be reset.
// Returns error code or 0 on success.
int   _mi_prim_reset(void* addr, size_t size);

// Protect memory. Returns error code or 0 on success.
int   _mi_prim_protect(void* addr, size_t size, bool protect);

// Allocate huge (1GiB) pages possibly associated with a NUMA node.
// pre: size > 0  and a multiple of 1GiB.
//      addr is either NULL or an address hint.
//      numa_node is either negative (don't care), or a numa node number.
void* _mi_prim_alloc_huge_os_pages(void* addr, size_t size, int numa_node);

// Return the current NUMA node
size_t _mi_prim_numa_node(void);

// Return the number of logical NUMA nodes
size_t _mi_prim_numa_node_count(void);


#endif  // MIMALLOC_PRIM_H


