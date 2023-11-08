/* ----------------------------------------------------------------------------
Copyright (c) 2018-2023, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// This file is included in `src/prim/prim.c`

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "mimalloc/atomic.h"
#include "mimalloc/prim.h"

//---------------------------------------------
// init
//---------------------------------------------

void _mi_prim_mem_init( mi_os_mem_config_t* config ) {
  config->page_size = 64*MI_KiB; // WebAssembly has a fixed page size: 64KiB
  config->alloc_granularity = 16;
  config->has_overcommit = false;
  config->must_free_whole = true;
  config->has_virtual_reserve = false;
}

//---------------------------------------------
// Free, Allocation: We use emmalloc as the
// "system allocator". That is a small
// allocator that still has the ability to
// properly return memory to the OS.
//---------------------------------------------

extern void emmalloc_free(void*);

int _mi_prim_free(void* addr, size_t size ) {
  MI_UNUSED(size);
  emmalloc_free(addr);
  return 0;
}


//---------------------------------------------
// Allocation
//---------------------------------------------

extern void* emmalloc_memalign(size_t, size_t);

// Note: the `try_alignment` is just a hint and the returned pointer is not guaranteed to be aligned.
int _mi_prim_alloc(size_t size, size_t try_alignment, bool commit, bool allow_large, bool* is_large, bool* is_zero, void** addr) {
  MI_UNUSED(try_alignment); MI_UNUSED(allow_large); MI_UNUSED(commit);
  *is_large = false;
  // TODO: Track the highest address ever seen; first uses of it are zeroes.
  //       That assumes no one else uses sbrk but us (they could go up,
  //       scribble, and then down), but we could assert on that perhaps.
  *is_zero = false;
  // emmalloc has some limitations on alignment size.
  // TODO: why does emmalloc ask for an align of 4MB? that ends up allocating
  //       8, which wastes quite a lot for us in wasm.
  #define MIN_EMMALLOC_ALIGN           8
  #define MAX_EMMALLOC_ALIGN (1024*1024)
  if (try_alignment < MIN_EMMALLOC_ALIGN) {
    try_alignment = MIN_EMMALLOC_ALIGN;
  } else if (try_alignment > MAX_EMMALLOC_ALIGN) {
    try_alignment = MAX_EMMALLOC_ALIGN;
  }
  void* p = emmalloc_memalign(try_alignment, size);
  *addr = p;
  if (p == 0) {
    return ENOMEM;
  }
  return 0;
}


//---------------------------------------------
// Commit/Reset
//---------------------------------------------

int _mi_prim_commit(void* addr, size_t size, bool* is_zero) {
  MI_UNUSED(addr); MI_UNUSED(size);
  // See TODO above.
  *is_zero = false;
  return 0;
}

int _mi_prim_decommit(void* addr, size_t size, bool* needs_recommit) {
  MI_UNUSED(addr); MI_UNUSED(size);
  *needs_recommit = false;
  return 0;
}

int _mi_prim_reset(void* addr, size_t size) {
  MI_UNUSED(addr); MI_UNUSED(size);
  return 0;
}

int _mi_prim_protect(void* addr, size_t size, bool protect) {
  MI_UNUSED(addr); MI_UNUSED(size); MI_UNUSED(protect);
  return 0;
}


//---------------------------------------------
// Huge pages and NUMA nodes
//---------------------------------------------

int _mi_prim_alloc_huge_os_pages(void* hint_addr, size_t size, int numa_node, bool* is_zero, void** addr) {
  MI_UNUSED(hint_addr); MI_UNUSED(size); MI_UNUSED(numa_node);
  *is_zero = true;
  *addr = NULL;
  return ENOSYS;
}

size_t _mi_prim_numa_node(void) {
  return 0;
}

size_t _mi_prim_numa_node_count(void) {
  return 1;
}


//----------------------------------------------------------------
// Clock
//----------------------------------------------------------------

#include <emscripten/html5.h>

mi_msecs_t _mi_prim_clock_now(void) {
  return emscripten_date_now();
}


//----------------------------------------------------------------
// Process info
//----------------------------------------------------------------

void _mi_prim_process_info(mi_process_info_t* pinfo)
{
  // use defaults
  MI_UNUSED(pinfo);
}


//----------------------------------------------------------------
// Output
//----------------------------------------------------------------

#include <emscripten/console.h>

void _mi_prim_out_stderr( const char* msg ) {
  emscripten_console_error(msg);
}


//----------------------------------------------------------------
// Environment
//----------------------------------------------------------------

bool _mi_prim_getenv(const char* name, char* result, size_t result_size) {
  // For code size reasons, do not support environ customization for now.
  MI_UNUSED(name);
  MI_UNUSED(result);
  MI_UNUSED(result_size);
  return false;
}


//----------------------------------------------------------------
// Random
//----------------------------------------------------------------

bool _mi_prim_random_buf(void* buf, size_t buf_len) {
  MI_UNUSED(buf);
  MI_UNUSED(buf_len);
  return false;
}


//----------------------------------------------------------------
// Thread init/done
//----------------------------------------------------------------

#ifdef __EMSCRIPTEN_SHARED_MEMORY__

// use pthread local storage keys to detect thread ending
// (and used with MI_TLS_PTHREADS for the default heap)
pthread_key_t _mi_heap_default_key = (pthread_key_t)(-1);

static void mi_pthread_done(void* value) {
  if (value!=NULL) {
    _mi_thread_done((mi_heap_t*)value);
  }
}

void _mi_prim_thread_init_auto_done(void) {
  mi_assert_internal(_mi_heap_default_key == (pthread_key_t)(-1));
  pthread_key_create(&_mi_heap_default_key, &mi_pthread_done);
}

void _mi_prim_thread_done_auto_done(void) {
  // nothing to do
}

void _mi_prim_thread_associate_default_heap(mi_heap_t* heap) {
  if (_mi_heap_default_key != (pthread_key_t)(-1)) {  // can happen during recursive invocation on freeBSD
    pthread_setspecific(_mi_heap_default_key, heap);
  }
}

#else

void _mi_prim_thread_init_auto_done(void) {
  // nothing
}

void _mi_prim_thread_done_auto_done(void) {
  // nothing
}

void _mi_prim_thread_associate_default_heap(mi_heap_t* heap) {
  MI_UNUSED(heap);

}
#endif

