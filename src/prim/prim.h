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
//  addr != NULL and page aligned
//  size > 0     and page aligned

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
void  _mi_prim_free(void* addr, size_t size );
  
// Allocate OS memory. Return NULL on error.
// The `try_alignment` is just a hint and the returned pointer does not have to be aligned.
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

// Clock ticks
mi_msecs_t _mi_prim_clock_now(void);

// Return process information (only for statistics)
void  _mi_prim_process_info(mi_msecs_t* utime, mi_msecs_t* stime, 
                             size_t* current_rss, size_t* peak_rss, 
                             size_t* current_commit, size_t* peak_commit, size_t* page_faults);

// Default stderr output. (only for warnings etc. with verbose enabled)
// msg != NULL && _mi_strlen(msg) > 0
void  _mi_prim_out_stderr( const char* msg );

// Get an environment variable. (only for options)
// name != NULL, result != NULL, result_size >= 64
bool _mi_prim_getenv(const char* name, char* result, size_t result_size);


//-------------------------------------------------------------------
// Thread id
// 
// Getting the thread id should be performant as it is called in the
// fast path of `_mi_free` and we specialize for various platforms as
// inlined definitions. Regular code should call `init.c:_mi_thread_id()`.
// We only require _mi_prim_thread_id() to return a unique id for each thread.
//-------------------------------------------------------------------

static inline mi_threadid_t _mi_prim_thread_id(void) mi_attr_noexcept;

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static inline mi_threadid_t _mi_prim_thread_id(void) mi_attr_noexcept {
  // Windows: works on Intel and ARM in both 32- and 64-bit
  return (uintptr_t)NtCurrentTeb();
}

// We use assembly for a fast thread id on the main platforms. The TLS layout depends on
// both the OS and libc implementation so we use specific tests for each main platform.
// If you test on another platform and it works please send a PR :-)
// see also https://akkadia.org/drepper/tls.pdf for more info on the TLS register.
#elif defined(__GNUC__) && ( \
           (defined(__GLIBC__)   && (defined(__x86_64__) || defined(__i386__) || defined(__arm__) || defined(__aarch64__))) \
        || (defined(__APPLE__)   && (defined(__x86_64__) || defined(__aarch64__))) \
        || (defined(__BIONIC__)  && (defined(__x86_64__) || defined(__i386__) || defined(__arm__) || defined(__aarch64__))) \
        || (defined(__FreeBSD__) && (defined(__x86_64__) || defined(__i386__) || defined(__aarch64__))) \
        || (defined(__OpenBSD__) && (defined(__x86_64__) || defined(__i386__) || defined(__aarch64__))) \
      )

static inline void* mi_tls_slot(size_t slot) mi_attr_noexcept {
  void* res;
  const size_t ofs = (slot*sizeof(void*));
  #if defined(__i386__)
    __asm__("movl %%gs:%1, %0" : "=r" (res) : "m" (*((void**)ofs)) : );  // x86 32-bit always uses GS
  #elif defined(__APPLE__) && defined(__x86_64__)
    __asm__("movq %%gs:%1, %0" : "=r" (res) : "m" (*((void**)ofs)) : );  // x86_64 macOSX uses GS
  #elif defined(__x86_64__) && (MI_INTPTR_SIZE==4)
    __asm__("movl %%fs:%1, %0" : "=r" (res) : "m" (*((void**)ofs)) : );  // x32 ABI
  #elif defined(__x86_64__)
    __asm__("movq %%fs:%1, %0" : "=r" (res) : "m" (*((void**)ofs)) : );  // x86_64 Linux, BSD uses FS
  #elif defined(__arm__)
    void** tcb; MI_UNUSED(ofs);
    __asm__ volatile ("mrc p15, 0, %0, c13, c0, 3\nbic %0, %0, #3" : "=r" (tcb));
    res = tcb[slot];
  #elif defined(__aarch64__)
    void** tcb; MI_UNUSED(ofs);
    #if defined(__APPLE__) // M1, issue #343
    __asm__ volatile ("mrs %0, tpidrro_el0\nbic %0, %0, #7" : "=r" (tcb));
    #else
    __asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tcb));
    #endif
    res = tcb[slot];
  #endif
  return res;
}

// setting a tls slot is only used on macOS for now
static inline void mi_tls_slot_set(size_t slot, void* value) mi_attr_noexcept {
  const size_t ofs = (slot*sizeof(void*));
  #if defined(__i386__)
    __asm__("movl %1,%%gs:%0" : "=m" (*((void**)ofs)) : "rn" (value) : );  // 32-bit always uses GS
  #elif defined(__APPLE__) && defined(__x86_64__)
    __asm__("movq %1,%%gs:%0" : "=m" (*((void**)ofs)) : "rn" (value) : );  // x86_64 macOS uses GS
  #elif defined(__x86_64__) && (MI_INTPTR_SIZE==4)
    __asm__("movl %1,%%fs:%0" : "=m" (*((void**)ofs)) : "rn" (value) : );  // x32 ABI
  #elif defined(__x86_64__)
    __asm__("movq %1,%%fs:%0" : "=m" (*((void**)ofs)) : "rn" (value) : );  // x86_64 Linux, BSD uses FS
  #elif defined(__arm__)
    void** tcb; MI_UNUSED(ofs);
    __asm__ volatile ("mrc p15, 0, %0, c13, c0, 3\nbic %0, %0, #3" : "=r" (tcb));
    tcb[slot] = value;
  #elif defined(__aarch64__)
    void** tcb; MI_UNUSED(ofs);
    #if defined(__APPLE__) // M1, issue #343
    __asm__ volatile ("mrs %0, tpidrro_el0\nbic %0, %0, #7" : "=r" (tcb));
    #else
    __asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tcb));
    #endif
    tcb[slot] = value;
  #endif
}

static inline mi_threadid_t _mi_prim_thread_id(void) mi_attr_noexcept {
  #if defined(__BIONIC__)
    // issue #384, #495: on the Bionic libc (Android), slot 1 is the thread id
    // see: https://github.com/aosp-mirror/platform_bionic/blob/c44b1d0676ded732df4b3b21c5f798eacae93228/libc/platform/bionic/tls_defines.h#L86
    return (uintptr_t)mi_tls_slot(1);
  #else
    // in all our other targets, slot 0 is the thread id
    // glibc: https://sourceware.org/git/?p=glibc.git;a=blob_plain;f=sysdeps/x86_64/nptl/tls.h
    // apple: https://github.com/apple/darwin-xnu/blob/main/libsyscall/os/tsd.h#L36
    return (uintptr_t)mi_tls_slot(0);
  #endif
}

#else

// otherwise use portable C, taking the address of a thread local variable (this is still very fast on most platforms).
static inline mi_threadid_t _mi_prim_thread_id(void) mi_attr_noexcept {
  return (uintptr_t)&_mi_heap_default;
}

#endif


#endif  // MIMALLOC_PRIM_H
