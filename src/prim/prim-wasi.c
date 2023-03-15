/* ----------------------------------------------------------------------------
Copyright (c) 2018-2023, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-atomic.h"
#include "prim.h"

//---------------------------------------------
// Initialize
//---------------------------------------------

void _mi_prim_mem_init( mi_os_mem_config_t* config ) {
  config->page_size = 64*MI_KiB; // WebAssembly has a fixed page size: 64KiB
  config->alloc_granularity = 16;
  config->has_overcommit = false;  
  config->must_free_whole = true;
}

//---------------------------------------------
// Free
//---------------------------------------------

void _mi_prim_free(void* addr, size_t size ) {
  MI_UNUSED(addr); MI_UNUSED(size);
  // wasi heap cannot be shrunk
}


//---------------------------------------------
// Allocation: sbrk or memory_grow
//---------------------------------------------

#if defined(MI_USE_SBRK)
  static void* mi_memory_grow( size_t size ) {
    void* p = sbrk(size);
    if (p == (void*)(-1)) return NULL;
    #if !defined(__wasi__) // on wasi this is always zero initialized already (?)
    memset(p,0,size);
    #endif
    return p;
  }
#elif defined(__wasi__)
  static void* mi_memory_grow( size_t size ) {
    size_t base = (size > 0 ? __builtin_wasm_memory_grow(0,_mi_divide_up(size, _mi_os_page_size()))
                            : __builtin_wasm_memory_size(0));
    if (base == SIZE_MAX) return NULL;
    return (void*)(base * _mi_os_page_size());
  }
#endif

#if defined(MI_USE_PTHREADS)
static pthread_mutex_t mi_heap_grow_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void* mi_prim_mem_grow(size_t size, size_t try_alignment) {
  void* p = NULL;
  if (try_alignment <= 1) {
    // `sbrk` is not thread safe in general so try to protect it (we could skip this on WASM but leave it in for now)
    #if defined(MI_USE_PTHREADS)
    pthread_mutex_lock(&mi_heap_grow_mutex);
    #endif
    p = mi_memory_grow(size);
    #if defined(MI_USE_PTHREADS)
    pthread_mutex_unlock(&mi_heap_grow_mutex);
    #endif
  }
  else {
    void* base = NULL;
    size_t alloc_size = 0;
    // to allocate aligned use a lock to try to avoid thread interaction
    // between getting the current size and actual allocation
    // (also, `sbrk` is not thread safe in general)
    #if defined(MI_USE_PTHREADS)
    pthread_mutex_lock(&mi_heap_grow_mutex);
    #endif
    {
      void* current = mi_memory_grow(0);  // get current size
      if (current != NULL) {
        void* aligned_current = mi_align_up_ptr(current, try_alignment);  // and align from there to minimize wasted space
        alloc_size = _mi_align_up( ((uint8_t*)aligned_current - (uint8_t*)current) + size, _mi_os_page_size());
        base = mi_memory_grow(alloc_size);
      }
    }
    #if defined(MI_USE_PTHREADS)
    pthread_mutex_unlock(&mi_heap_grow_mutex);
    #endif
    if (base != NULL) {
      p = mi_align_up_ptr(base, try_alignment);
      if ((uint8_t*)p + size > (uint8_t*)base + alloc_size) {
        // another thread used wasm_memory_grow/sbrk in-between and we do not have enough
        // space after alignment. Give up (and waste the space as we cannot shrink :-( )
        // (in `mi_os_mem_alloc_aligned` this will fall back to overallocation to align)
        p = NULL;
      }
    }
  }
  if (p == NULL) {
    _mi_warning_message("unable to allocate sbrk/wasm_memory_grow OS memory (%zu bytes, %zu alignment)\n", size, try_alignment);
    errno = ENOMEM;
    return NULL;
  }
  mi_assert_internal( try_alignment == 0 || (uintptr_t)p % try_alignment == 0 );
  return p;
}

// Note: the `try_alignment` is just a hint and the returned pointer is not guaranteed to be aligned.
void* _mi_prim_alloc(size_t size, size_t try_alignment, bool commit, bool allow_large, bool* is_large) {
  MI_UNUSED(allow_large);
  *is_large = false;
  return mi_prim_mem_grow(size, try_alignment);
}


//---------------------------------------------
// Commit/Reset/Protect
//---------------------------------------------

int _mi_prim_commit(void* addr, size_t size, bool commit) {
  MI_UNUSED(addr); MI_UNUSED(size); MI_UNUSED(commit);
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

void* _mi_prim_alloc_huge_os_pages(void* addr, size_t size, int numa_node) {
  MI_UNUSED(addr); MI_UNUSED(size); MI_UNUSED(numa_node);
  return NULL;
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

#include <time.h>

#if defined(CLOCK_REALTIME) || defined(CLOCK_MONOTONIC)

mi_msecs_t _mi_prim_clock_now(void) {
  struct timespec t;
  #ifdef CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &t);
  #else
  clock_gettime(CLOCK_REALTIME, &t);
  #endif
  return ((mi_msecs_t)t.tv_sec * 1000) + ((mi_msecs_t)t.tv_nsec / 1000000);
}

#else

// low resolution timer
mi_msecs_t _mi_prim_clock_now(void) {
  return ((mi_msecs_t)clock() / ((mi_msecs_t)CLOCKS_PER_SEC / 1000));
}

#endif


//----------------------------------------------------------------
// Process info
//----------------------------------------------------------------

void _mi_prim_process_info(mi_msecs_t* utime, mi_msecs_t* stime, size_t* current_rss, size_t* peak_rss, size_t* current_commit, size_t* peak_commit, size_t* page_faults)
{
  *peak_commit    = (size_t)(mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&_mi_stats_main.committed.peak));
  *current_commit = (size_t)(mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&_mi_stats_main.committed.current));
  *peak_rss    = *peak_commit;
  *current_rss = *current_commit;
  *page_faults = 0;
  *utime = 0;
  *stime = 0;
}

//----------------------------------------------------------------
// Output
//----------------------------------------------------------------

void _mi_prim_out_stderr( const char* msg ) {
  fputs(msg,stderr);
}


//----------------------------------------------------------------
// Environment
//----------------------------------------------------------------

bool _mi_prim_getenv(const char* name, char* result, size_t result_size) {
  // cannot call getenv() when still initializing the C runtime.
  if (_mi_preloading()) return false;
  const char* s = getenv(name);
  if (s == NULL) {
    // we check the upper case name too.
    char buf[64+1];
    size_t len = _mi_strnlen(name,sizeof(buf)-1);
    for (size_t i = 0; i < len; i++) {
      buf[i] = _mi_toupper(name[i]);
    }
    buf[len] = 0;
    s = getenv(buf);
  }
  if (s == NULL || _mi_strnlen(s,result_size) >= result_size)  return false;
  _mi_strlcpy(result, s, result_size);
  return true;
}