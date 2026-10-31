/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_PROCESS_H
#define MI_INTERNAL_PROCESS_H

#include "../types.h"
#include "../prim.h"          // mi_os_mem_config_t
#include "../util/options.h"
#include "../util/libc.h"     // mi_align_down_ptr

mi_heap_t*    _mi_subproc_heap_main(mi_subproc_t* subproc);
mi_theap_t*   _mi_theap_main(void);  // current thread's backing theap, without initialization
mi_page_t*    _mi_page_empty_get(void);
void          _mi_auto_process_init(void);
void mi_cdecl _mi_auto_process_done(void) mi_attr_noexcept;
bool          _mi_preloading(void);           // true while the C runtime is not initialized yet
void          _mi_thread_done(mi_theap_t* theap);
mi_theap_t*   _mi_thread_init(void);
mi_theap_t*   _mi_thread_init_with_heap(mi_heap_t* heap);
bool          _mi_is_empty_theap(const mi_theap_t* theap);

extern mi_decl_hidden const mi_theap_t _mi_theap_empty; // read-only empty theap, initial value of the thread local default theap (in the MI_TLS_MODEL_LOCAL)

// Fork state
typedef struct mi_fork_state_s {
  size_t              epoch;            // incremented in the child at every fork
  _Atomic(size_t)     pending;          // set in the child until the dead theaps are abandoned
  bool                torn;             // set in a child that may have a torn snapshot of memory (on macOS)
  mi_heap_t*          suspended_heap;   // heap with suspended abandoned reclaim (on macOS)
  size_t              suspended_count[MI_BIN_COUNT];  // and its abandoned counts
} mi_fork_state_t;

// OS state (see `prim/os.c` and the `prim/<os>/prim.c` files)
typedef struct mi_os_state_s {
  _Atomic(size_t)     numa_node_count;         // cached NUMA node count (0 until first use)
  _Atomic(mi_addr_t)  aligned_base;            // next aligned address hint (see `_mi_os_get_aligned_hint`)
  _Atomic(mi_addr_t)  huge_start;              // next address for huge OS pages (see `mi_os_claim_huge_pages`)
  _Atomic(size_t)     large_page_access;       // 0: unknown, 1: granted, 2: denied (e.g. needs `SeLockMemoryPrivilege` on Windows)
  _Atomic(size_t)     large_page_backoff;      // skip large OS page allocation while > 0 (set after a failed attempt)
  _Atomic(size_t)     huge_page_unavailable;   // set once a huge (1GiB) OS page allocation failed
  _Atomic(size_t)     lazy_reset_unavailable;  // set if a lazy reset (`MADV_FREE`) is not supported (and we use `MADV_DONTNEED`)
} mi_os_state_t;


// Process meta data: this is allocated from the OS at process initialization (see `heap/process.c:mi_process_metadata_init`)
// instead of being static so it is not at a fixed offset from the program image, is surrounded by guard pages in secure mode,
// and can be memory tagged.
typedef struct mi_process_metadata_s {
  mi_decl_cache_align mi_subproc_t  subproc_main;           // initial subprocess
  mi_decl_cache_align mi_heap_t     heap_main;              // initial heap for the process
  mi_decl_cache_align mi_theap_t    theap_main;             // initial theap for the main thread
  mi_decl_cache_align mi_tld_t      tld_main;               // initial tld for the main thread
  mi_decl_cache_align mi_theap_t    theap_meta;             // detached theap for meta-data allocation
  mi_decl_cache_align mi_tld_t      tld_meta;               // detached tld for `theap_meta`
  mi_decl_cache_align mi_theap_t    theap_empty_wrong;      // empty theap used to signal that a theap for a heap could not be allocated
  mi_memid_t                        memid;                  // OS provenance of the meta data (never freed)
} mi_process_metadata_t;

// Sealed process data that is read-only after process initialization in secure mode
// (see `heap/process.c:_mi_process_unseal` for writing).
typedef struct mi_process_sealed_s {
  // page map
  _Atomic(struct mi_page_map_s*) page_map;  // the page map (`NULL` before initialization) (see `heap/page-map.h`); must be the first entry for better code-gen in secure mode
  _Atomic(size_t)     page_map_committed;   // committed entries in the page map

  // user callbacks
  _Atomic(void*)      deferred_free;    // is `mi_deferred_free_fun*` (but some platforms don't support atomic function pointers)
  _Atomic(void*)      deferred_arg;
  _Atomic(void*)      out_default;      // is `mi_output_fun*`
  _Atomic(void*)      out_arg;
  _Atomic(void*)      error_handler;    // is `mi_error_fun*`
  _Atomic(void*)      error_arg;

  mi_process_metadata_t* metadata;      // process meta data (`NULL` before initialization) (see `heap/process.c:mi_process_metadata_init`)
  mi_os_mem_config_t  os_mem_config;    // OS memory configuration (see `prim/os.c`)
  bool                is_protected;     // `true` if the sealed page is read-only (see `_mi_process_unseal`)
} mi_process_sealed_t;


// In secure mode, guard static process data such that guard pages fit in secure mode (twice the maximal OS page size)
// the front guard area also has a read-only page (preceded by a guard page) that contains mi_process_sealed_t.
#if MI_SECURE && MI_OS_HAS_GUARD_PAGES
  #define MI_SECURE_STATIC_GUARD    (2*MI_OS_PROBABLE_PAGE_SIZE)  // checked at runtime
  #define MI_SECURE_SEALED_PAGE     (1)
  #define MI_SECURE_SEALED_GUARD    (MI_SECURE_STATIC_GUARD + MI_OS_PROBABLE_PAGE_SIZE)
#else
  #define MI_SECURE_STATIC_GUARD    (MI_CACHE_LINE_SIZE)          // no guard pages; just a cache line
  #define MI_SECURE_SEALED_PAGE     (0)
  #define MI_SECURE_SEALED_GUARD    (MI_SECURE_STATIC_GUARD)
#endif

// Output before an output function is registered (or the C runtime is initialized) is buffered (see `util/options.c`)
#ifndef MI_MAX_DELAY_OUTPUT
#define MI_MAX_DELAY_OUTPUT         (16*MI_KiB)
#endif

// Statically allocated process data. This is zero initialized and padded such that
// in secure mode it is surrounded by guard pages (see `heap/process.c:mi_process_guard_init`)
typedef struct mi_process_s {
  uint8_t                           guard_front[MI_SECURE_SEALED_GUARD];  // in secure mode, contains the sealed page (see `mi_process_get_sealed`)
  #if !MI_SECURE_SEALED_PAGE
  mi_process_sealed_t               sealed;                 // sealed data (unprotected)
  #endif

  bool                              process_is_initialized; // set to `true` in `mi_process_init`
  bool                              libc_is_initialized;    // `true` once the C runtime is initialized (and `false` again after process done)
  bool                              malloc_is_redirected;   // `true` if malloc is redirected to mimalloc (on Windows with `mimalloc-redirect.dll`)
  _Atomic(size_t)                   process_done_called;    // set once `mi_process_done` is called
  _Atomic(size_t)                   subprocs_created;       // total number of sub-processes created (used for `subproc_seq`)
  mi_atomic_guard_t                 arena_purge_guard;      // allow only one thread at a time to purge arenas

  mi_msecs_t                        clock_process_start;    // clock at process initialization (see `util/stats.c`)
  mi_msecs_t                        clock_overhead;         // measured overhead of reading the clock (subtracted in `_mi_clock_end`)
  int64_t                           clock_ticks_per_msec;   // performance counter ticks per millisecond (on Windows) (0 until first use)

  _Atomic(size_t)                   output_buf_len;         // used length of `output_buf`
  _Atomic(size_t)                   error_count;            // stop emitting errors when >= `mi_option_max_errors`
  _Atomic(size_t)                   warning_count;          // stop emitting warnings when >= `mi_option_max_warnings`

  mi_os_state_t                     os;                     // OS state

  mi_lock_t                         thread_locals_lock;     // initialized in `_mi_thread_locals_init` (see `util/threadlocal.c`)
  mi_bitmap_t*                      thread_locals_free;     // bitmap of free thread local slots (1=free, 0=in-use)
  mi_memid_t                        thread_locals_memid;    // provenance of `thread_locals_free`
  size_t                            thread_locals_version;  // version to be able to reuse slots safely

  mi_subproc_t*                     subprocs;               // list of all sub-processes
  mi_lock_t                         subprocs_lock;          // initialized in `_mi_subproc_main_init` (as a zero lock is invalid on some platforms)
  mi_fork_state_t                   fork;                   // fork state (see `heap/subproc.c`)

  mi_profiler_t*                    profile_env_profiler;   // the pprof profiler if `MIMALLOC_PROFILE` was set (see `profile/pprof.c`)
  mi_atomic_guard_t                 profile_snapshot_guard; // allow only one thread at a time to take a profile snapshot

  // arrays last
  mi_option_state_t                 options[_mi_option_last]; // current option values (see `util/options.c`)
  char                              output_buf[MI_MAX_DELAY_OUTPUT+1]; // delayed output buffer (see `util/options.c`)

  // end guard
  mi_decl_cache_align uint8_t       guard_end[MI_SECURE_STATIC_GUARD];
} mi_process_t;

extern mi_decl_hidden mi_process_t _mi_process;

// Get the sealed process data (for reading only).
// In secure mode, this is the last full OS page within `guard_front` (just before the page where the process data starts).
static inline mi_process_sealed_t* mi_process_get_sealed(void) {
  #if MI_SECURE_SEALED_PAGE
  return (mi_process_sealed_t*)mi_align_down_ptr((uint8_t*)&_mi_process + MI_SECURE_SEALED_GUARD - MI_OS_PROBABLE_PAGE_SIZE, MI_OS_PROBABLE_PAGE_SIZE);
  #else
  return &_mi_process.sealed;
  #endif
}

mi_process_metadata_t* _mi_process_metadata_init(void);

// Get the process meta data (allocated at process initialization, or lazily on first access).
static inline mi_process_metadata_t* mi_process_metadata(void) {
  mi_process_metadata_t* const metadata = mi_process_get_sealed()->metadata;
  return (mi_likely(metadata != NULL) ? metadata : _mi_process_metadata_init());
}

// Make the sealed process data writable (and take a lock); must be followed by `_mi_process_seal`.
mi_process_sealed_t* _mi_process_unseal(void);
void                 _mi_process_seal(void);

#endif
