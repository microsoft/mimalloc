/* ----------------------------------------------------------------------------
Copyright (c) 2019-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdio.h>

#include "mimalloc.h"

// Test for large arenas registered with mmap on 64-bit Linux systems
#if defined(__linux__) && UINTPTR_MAX > UINT32_MAX
#include <sys/mman.h>

#define GIB ((size_t)1024 * 1024 * 1024)
#define MIB ((size_t)1024 * 1024)

int main(void) {
  // With the following setup, the arena size is 24 GiB and the total
  // allocation requires over 16 GiB. mimalloc arena internally creates
  // child arenas once the arena size is over 16 GiB so this test will
  // trigger the creation and access to child arenas to ensure the arena
  // traversal and space accounting works as expected.
  const size_t arena_size = 24 * GIB;
  const size_t allocation_size = 63 * MIB;
  const size_t allocation_count = 272; 
  const size_t alignment = mi_arena_min_alignment();
  const size_t mapping_size = arena_size + alignment;

  void* mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (mapping == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  const uintptr_t aligned =
      ((uintptr_t)mapping + alignment - 1) & ~((uintptr_t)alignment - 1);
  mi_arena_id_t arena_id = NULL;
  if (!mi_manage_os_memory_ex((void*)aligned, arena_size,
                              true,   /* committed */
                              false,  /* pinned */
                              false,  /* not known to be initially zero */
                              -1,     /* no NUMA preference */
                              true,   /* exclusive */
                              &arena_id)) {
    fprintf(stderr, "failed to register the 24 GiB arena\n");
    return 1;
  }

  mi_heap_t* heap = mi_heap_new_in_arena(arena_id);
  if (heap == NULL) {
    fprintf(stderr, "failed to create the arena heap\n");
    return 1;
  }

  for (size_t i = 0; i < allocation_count; i++) {
    void* p = mi_heap_malloc(heap, allocation_size);
    if (p == NULL) {
      fprintf(stderr,
              "allocation %zu failed after %zu MiB; child arena was not used\n",
              i, i * allocation_size / MIB);
      return 1;
    }
    if (!mi_arena_contains(arena_id, p)) {
      fprintf(stderr, "allocation %zu came from outside the requested arena\n", i);
      return 1;
    }
  }

  printf("allocated %zu MiB from the parent arena and its children\n",
         allocation_count * allocation_size / MIB);
  return 0;
}

#else

int main(void) {
  printf("test-arena-large requires 64-bit Linux\n");
  return 0;
}

#endif
