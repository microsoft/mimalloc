/* ----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_H
#define MIMALLOC_H

#define MI_MALLOC_VERSION 164   // major + 2 digits minor

// ------------------------------------------------------
// Compiler specific attributes
// ------------------------------------------------------

#ifdef __cplusplus
  #if (__cplusplus >= 201103L) || (_MSC_VER > 1900)  // C++11
    #define mi_attr_noexcept   noexcept
  #else
    #define mi_attr_noexcept   throw()
  #endif
#else
  #define mi_attr_noexcept
#endif

#if (__cplusplus >= 201703L)   // C++17
  #define mi_decl_nodiscard    [[nodiscard]]
#elif (__GNUC__ >= 4) || defined(__clang__)  // includes clang, icc, and clang-cl
  #define mi_decl_nodiscard    __attribute__((warn_unused_result))
#elif (_MSC_VER >= 1700)
  #define mi_decl_nodiscard    _Check_return_
#else
  #define mi_decl_nodiscard
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
  #if !defined(MI_SHARED_LIB)
    #define mi_decl_export
  #elif defined(MI_SHARED_LIB_EXPORT)
    #define mi_decl_export              __declspec(dllexport)
  #else
    #define mi_decl_export              __declspec(dllimport)
  #endif
  #if defined(__MINGW32__)
    #define mi_decl_restrict
    #define mi_attr_malloc              __attribute__((malloc))
  #else
    #if (_MSC_VER >= 1900) && !defined(__EDG__)
      #define mi_decl_restrict          __declspec(allocator) __declspec(restrict)
    #else
      #define mi_decl_restrict          __declspec(restrict)
    #endif
    #define mi_attr_malloc
  #endif
  #define mi_cdecl                      __cdecl
  #define mi_attr_alloc_size(s)
  #define mi_attr_alloc_size2(s1,s2)
  #define mi_attr_alloc_align(p)
#elif defined(__GNUC__)                 // includes clang and icc
  #define mi_cdecl                      // leads to warnings... __attribute__((cdecl))
  #define mi_decl_export                __attribute__((visibility("default")))
  #define mi_decl_restrict
  #define mi_attr_malloc                __attribute__((malloc))
  #if (defined(__clang_major__) && (__clang_major__ < 4)) || (__GNUC__ < 5)
    #define mi_attr_alloc_size(s)
    #define mi_attr_alloc_size2(s1,s2)
    #define mi_attr_alloc_align(p)
  #elif defined(__INTEL_COMPILER)
    #define mi_attr_alloc_size(s)       __attribute__((alloc_size(s)))
    #define mi_attr_alloc_size2(s1,s2)  __attribute__((alloc_size(s1,s2)))
    #define mi_attr_alloc_align(p)
  #else
    #define mi_attr_alloc_size(s)       __attribute__((alloc_size(s)))
    #define mi_attr_alloc_size2(s1,s2)  __attribute__((alloc_size(s1,s2)))
    #define mi_attr_alloc_align(p)      __attribute__((alloc_align(p)))
  #endif
#else
  #define mi_cdecl
  #define mi_decl_export
  #define mi_decl_restrict
  #define mi_attr_malloc
  #define mi_attr_alloc_size(s)
  #define mi_attr_alloc_size2(s1,s2)
  #define mi_attr_alloc_align(p)
#endif

// ------------------------------------------------------
// Includes
// ------------------------------------------------------

#include <stddef.h>     // size_t
#include <stdbool.h>    // bool
#include <stdlib.h>     // wchar_t

#ifdef __cplusplus
extern "C" {
#endif


// -----------------------------------------------------------------------------------
// Debugging
// We declare two entry points for each allocation function:
// the normal one (`mi_malloc`) and one that takes a source argument (`dbg_mi_malloc`). 
// The source argument is stored in heap blocks to track allocations in debug mode.
// The following macros make it easier to specify this.
// Note: these are even defined in release mode (where the source argument is ignored)
// so one can still build a debug program that links with the release build of mimalloc.
// -----------------------------------------------------------------------------------

typedef struct mi_source_s {
  long long src; // packed encoding of the source location.
} mi_source_t;

mi_decl_export mi_source_t  mi_source_ret(void* return_address);
mi_decl_export mi_source_t  mi_source_loc(const char* fname, int lineno);
mi_decl_export void*        mi_source_unpack(mi_source_t source, const char** fname, int* lineno);

#define mi_export_alloc(decls,tp,name,attrs,...) \
  decls mi_decl_export tp dbg_##name( __VA_ARGS__, mi_source_t dbg_source) attrs; \
  decls mi_decl_export tp name(__VA_ARGS__) attrs

#define mi_export_malloc(tp,name,...)  mi_export_alloc(mi_decl_nodiscard, mi_decl_restrict tp, name, mi_attr_noexcept mi_attr_malloc, __VA_ARGS__)
#define mi_export_realloc(tp,name,...) mi_export_alloc(mi_decl_nodiscard, tp, name, mi_attr_noexcept, __VA_ARGS__)
#define mi_export_new(tp,name,...)     mi_export_alloc(mi_decl_nodiscard, mi_decl_restrict tp, name, mi_attr_malloc, __VA_ARGS__)
#define mi_export_noexcpt(tp,name,...) mi_export_alloc( , tp, name, mi_attr_noexcept, __VA_ARGS__)


// ------------------------------------------------------
// Standard malloc interface
// ------------------------------------------------------

mi_decl_export void mi_free(void* p) mi_attr_noexcept;

mi_export_malloc( void*, mi_malloc,  size_t size)               mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_calloc,  size_t count, size_t size) mi_attr_alloc_size2(1, 2);
mi_export_realloc(void*, mi_realloc, void* p, size_t newsize)   mi_attr_alloc_size(2);
mi_export_noexcpt(void*, mi_expand,  void* p, size_t newsize)   mi_attr_alloc_size(2);

mi_export_malloc( char*, mi_strdup,  const char* s);
mi_export_malloc( char*, mi_strndup, const char* s, size_t n);
mi_export_malloc( char*, mi_realpath,const char* fname, char* resolved_name);


// ------------------------------------------------------
// Extended functionality
// ------------------------------------------------------
#define MI_SMALL_WSIZE_MAX  (128)
#define MI_SMALL_SIZE_MAX   (MI_SMALL_WSIZE_MAX*sizeof(void*))

mi_export_malloc( void*, mi_malloc_small, size_t size)  mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_zalloc_small, size_t size)  mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_zalloc, size_t size)        mi_attr_alloc_size(1);

mi_export_malloc( void*, mi_mallocn, size_t count, size_t size)            mi_attr_alloc_size2(1,2);
mi_export_realloc(void*, mi_reallocn, void* p, size_t count, size_t size)  mi_attr_alloc_size2(2,3);
mi_export_realloc(void*, mi_reallocf, void* p, size_t newsize)             mi_attr_alloc_size(2);

mi_decl_nodiscard mi_decl_export size_t mi_usable_size(const void* p) mi_attr_noexcept;
mi_decl_nodiscard mi_decl_export size_t mi_good_size(size_t size)     mi_attr_noexcept;


// ------------------------------------------------------
// Internals
// ------------------------------------------------------

typedef void (mi_cdecl mi_deferred_free_fun)(bool force, unsigned long long heartbeat, void* arg);
mi_decl_export void mi_register_deferred_free(mi_deferred_free_fun* deferred_free, void* arg) mi_attr_noexcept;

typedef void (mi_cdecl mi_output_fun)(const char* msg, void* arg);
mi_decl_export void mi_register_output(mi_output_fun* out, void* arg) mi_attr_noexcept;

typedef void (mi_cdecl mi_error_fun)(int err, void* arg);
mi_decl_export void mi_register_error(mi_error_fun* fun, void* arg);

mi_decl_export void mi_collect(bool force)    mi_attr_noexcept;
mi_decl_export int  mi_version(void)          mi_attr_noexcept;
mi_decl_export void mi_stats_reset(void)      mi_attr_noexcept;
mi_decl_export void mi_stats_merge(void)      mi_attr_noexcept;
mi_decl_export void mi_stats_print(void* out) mi_attr_noexcept;  // backward compatibility: `out` is ignored and should be NULL
mi_decl_export void mi_stats_print_out(mi_output_fun* out, void* arg) mi_attr_noexcept;

mi_decl_export void mi_process_init(void)     mi_attr_noexcept;
mi_decl_export void mi_thread_init(void)      mi_attr_noexcept;
mi_decl_export void mi_thread_done(void)      mi_attr_noexcept;
mi_decl_export void mi_thread_stats_print_out(mi_output_fun* out, void* arg) mi_attr_noexcept;


// -------------------------------------------------------------------------------------
// Aligned allocation
// Note that `alignment` always follows `size` for consistency with unaligned
// allocation, but unfortunately this differs from `posix_memalign` and `aligned_alloc`.
// -------------------------------------------------------------------------------------

mi_export_malloc( void*, mi_malloc_aligned,  size_t size, size_t alignment)                mi_attr_alloc_size(1) mi_attr_alloc_align(2);
mi_export_malloc( void*, mi_zalloc_aligned,  size_t size, size_t alignment)                mi_attr_alloc_size(1) mi_attr_alloc_align(2);
mi_export_malloc( void*, mi_calloc_aligned,  size_t count, size_t size, size_t alignment)  mi_attr_alloc_size2(1,2) mi_attr_alloc_align(3);
mi_export_realloc(void*, mi_realloc_aligned, void* p, size_t newsize, size_t alignment)    mi_attr_alloc_size(2) mi_attr_alloc_align(3);

mi_export_malloc( void*, mi_malloc_aligned_at,  size_t size, size_t alignment, size_t offset)  mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_zalloc_aligned_at,  size_t size, size_t alignment, size_t offset)  mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_calloc_aligned_at,  size_t count, size_t size, size_t alignment, size_t offset)  mi_attr_alloc_size2(1, 2);
mi_export_realloc(void*, mi_realloc_aligned_at, void* p, size_t newsize, size_t alignment, size_t offset)    mi_attr_alloc_size(2);


// -------------------------------------------------------------------------------------
// Heaps: first-class, but can only allocate from the same thread that created it.
// -------------------------------------------------------------------------------------

struct mi_heap_s;
typedef struct mi_heap_s mi_heap_t;

mi_decl_nodiscard mi_decl_export mi_heap_t* mi_heap_new(void);
mi_decl_export void       mi_heap_delete(mi_heap_t* heap);
mi_decl_export void       mi_heap_destroy(mi_heap_t* heap);
mi_decl_export mi_heap_t* mi_heap_set_default(mi_heap_t* heap);
mi_decl_export mi_heap_t* mi_heap_get_default(void);
mi_decl_export mi_heap_t* mi_heap_get_backing(void);
mi_decl_export void       mi_heap_collect(mi_heap_t* heap, bool force) mi_attr_noexcept;

mi_export_malloc( void*, mi_heap_malloc,   mi_heap_t* heap, size_t size)               mi_attr_alloc_size(2);
mi_export_malloc( void*, mi_heap_zalloc,   mi_heap_t* heap, size_t size)               mi_attr_alloc_size(2);
mi_export_malloc( void*, mi_heap_calloc,   mi_heap_t* heap, size_t count, size_t size) mi_attr_alloc_size2(2, 3);
mi_export_malloc( void*, mi_heap_mallocn,  mi_heap_t* heap, size_t count, size_t size) mi_attr_alloc_size2(2, 3);
mi_export_malloc( void*, mi_heap_malloc_small, mi_heap_t* heap, size_t size)           mi_attr_alloc_size(2);
mi_export_malloc( void*, mi_heap_zalloc_small, mi_heap_t* heap, size_t size)           mi_attr_alloc_size(2);

mi_export_realloc(void*, mi_heap_realloc,  mi_heap_t* heap, void* p, size_t newsize)   mi_attr_alloc_size(3);
mi_export_realloc(void*, mi_heap_reallocn, mi_heap_t* heap, void* p, size_t count, size_t size);
mi_export_realloc(void*, mi_heap_reallocf, mi_heap_t* heap, void* p, size_t newsize)   mi_attr_alloc_size(3);

mi_export_malloc( char*, mi_heap_strdup,   mi_heap_t* heap, const char* s);
mi_export_malloc( char*, mi_heap_strndup,  mi_heap_t* heap, const char* s, size_t n);
mi_export_malloc( char*, mi_heap_realpath, mi_heap_t* heap, const char* fname, char* resolved_name);

mi_export_malloc( void*, mi_heap_malloc_aligned, mi_heap_t* heap, size_t size, size_t alignment)               mi_attr_alloc_size(2) mi_attr_alloc_align(3);
mi_export_malloc( void*, mi_heap_zalloc_aligned, mi_heap_t* heap, size_t size, size_t alignment)               mi_attr_alloc_size(2) mi_attr_alloc_align(3);
mi_export_malloc( void*, mi_heap_calloc_aligned, mi_heap_t* heap, size_t count, size_t size, size_t alignment) mi_attr_alloc_size2(2, 3) mi_attr_alloc_align(4);
mi_export_realloc(void*, mi_heap_realloc_aligned,mi_heap_t* heap, void* p, size_t newsize, size_t alignment)   mi_attr_alloc_size(3) mi_attr_alloc_align(4);

mi_export_malloc( void*, mi_heap_malloc_aligned_at, mi_heap_t* heap, size_t size, size_t alignment, size_t offset)  mi_attr_alloc_size(2);
mi_export_malloc( void*, mi_heap_zalloc_aligned_at, mi_heap_t* heap, size_t size, size_t alignment, size_t offset)  mi_attr_alloc_size(2);
mi_export_malloc( void*, mi_heap_calloc_aligned_at, mi_heap_t* heap, size_t count, size_t size, size_t alignment, size_t offset) mi_attr_alloc_size2(2, 3);
mi_export_realloc(void*, mi_heap_realloc_aligned_at,mi_heap_t* heap, void* p, size_t newsize, size_t alignment, size_t offset)   mi_attr_alloc_size(3);


// --------------------------------------------------------------------------------
// Zero initialized re-allocation.
// Only valid on memory that was originally allocated with zero initialization too.
// e.g. `mi_calloc`, `mi_zalloc`, `mi_zalloc_aligned` etc.
// see <https://github.com/microsoft/mimalloc/issues/63#issuecomment-508272992>
// --------------------------------------------------------------------------------

mi_export_realloc(void*, mi_rezalloc, void* p, size_t newsize)                mi_attr_alloc_size(2);
mi_export_realloc(void*, mi_recalloc, void* p, size_t newcount, size_t size)  mi_attr_alloc_size2(2,3);
mi_export_realloc(void*, mi_rezalloc_aligned,    void* p, size_t newsize, size_t alignment)                mi_attr_alloc_size(2) mi_attr_alloc_align(3);
mi_export_realloc(void*, mi_recalloc_aligned,    void* p, size_t newcount, size_t size, size_t alignment)  mi_attr_alloc_size2(2,3) mi_attr_alloc_align(4);
mi_export_realloc(void*, mi_rezalloc_aligned_at, void* p, size_t newsize, size_t alignment, size_t offset) mi_attr_alloc_size(2);
mi_export_realloc(void*, mi_recalloc_aligned_at, void* p, size_t newcount, size_t size, size_t alignment, size_t offset)  mi_attr_alloc_size2(2,3);

mi_export_realloc(void*, mi_heap_rezalloc, mi_heap_t* heap, void* p, size_t newsize)                mi_attr_alloc_size(3);
mi_export_realloc(void*, mi_heap_recalloc, mi_heap_t* heap, void* p, size_t newcount, size_t size)  mi_attr_alloc_size2(3,4);
mi_export_realloc(void*, mi_heap_rezalloc_aligned,    mi_heap_t* heap, void* p, size_t newsize, size_t alignment)                 mi_attr_alloc_size(3) mi_attr_alloc_align(4);
mi_export_realloc(void*, mi_heap_recalloc_aligned,    mi_heap_t* heap, void* p, size_t newcount, size_t size, size_t alignment)   mi_attr_alloc_size2(3,4) mi_attr_alloc_align(5);
mi_export_realloc(void*, mi_heap_rezalloc_aligned_at, mi_heap_t* heap, void* p, size_t newsize, size_t alignment, size_t offset)  mi_attr_alloc_size(3);
mi_export_realloc(void*, mi_heap_recalloc_aligned_at, mi_heap_t* heap, void* p, size_t newcount, size_t size, size_t alignment, size_t offset)  mi_attr_alloc_size2(3, 4);


// ------------------------------------------------------
// Analysis
// ------------------------------------------------------

mi_decl_nodiscard mi_decl_export bool mi_heap_contains_block(mi_heap_t* heap, const void* p);
mi_decl_nodiscard mi_decl_export bool mi_heap_check_owned(mi_heap_t* heap, const void* p);
mi_decl_nodiscard mi_decl_export bool mi_check_owned(const void* p);

// An area of heap space contains blocks of a single size.
typedef struct mi_heap_area_s {
  void*  blocks;      // start of the area containing heap blocks
  size_t reserved;    // bytes reserved for this area (virtual)
  size_t committed;   // current available bytes for this area
  size_t used;        // bytes in use by allocated blocks
  size_t block_size;  // size in bytes of each block
} mi_heap_area_t;

// Information about an allocated block. 
typedef struct mi_block_info_s {
  void*       block;          // start of the block
  size_t      size;           // full size including padding etc.
  size_t      usable_size;    // usable size (available for in-place realloc)
  size_t      allocated_size; // actual allocated size (only precise in debug mode with padding enabled)
  bool        valid;          // is the block valid? (only detects heap overflow with padding enabled)
  mi_source_t source;         // the source location that allocated this block (only valid in debug mode with padding)
} mi_block_info_t;

typedef bool (mi_cdecl mi_block_visit_fun)(const mi_heap_t* heap, const mi_heap_area_t* area, const mi_block_info_t* block_info, void* arg);

mi_decl_export bool mi_heap_visit_blocks(const mi_heap_t* heap, bool visit_all_blocks, mi_block_visit_fun* visitor, void* arg);

// Experimental
mi_decl_export void mi_heap_print_json(mi_heap_t* heap, mi_output_fun* out, void* arg);
mi_decl_export bool mi_heap_is_empty(mi_heap_t* heap);
mi_decl_export void mi_heap_check_leak(mi_heap_t* heap, mi_output_fun* out, void* arg);
mi_decl_export void mi_heap_set_extra_padding(mi_heap_t* heap, size_t extra_padding);

mi_decl_nodiscard mi_decl_export bool mi_is_in_heap_region(const void* p) mi_attr_noexcept;
mi_decl_nodiscard mi_decl_export bool mi_is_redirected() mi_attr_noexcept;

mi_decl_export int mi_reserve_huge_os_pages_interleave(size_t pages, size_t numa_nodes, size_t timeout_msecs) mi_attr_noexcept;
mi_decl_export int mi_reserve_huge_os_pages_at(size_t pages, int numa_node, size_t timeout_msecs) mi_attr_noexcept;

// deprecated
mi_decl_export int  mi_reserve_huge_os_pages(size_t pages, double max_secs, size_t* pages_reserved) mi_attr_noexcept;


// ------------------------------------------------------
// Convenience
// ------------------------------------------------------

#define mi_malloc_tp(tp)                ((tp*)mi_malloc(sizeof(tp)))
#define mi_zalloc_tp(tp)                ((tp*)mi_zalloc(sizeof(tp)))
#define mi_calloc_tp(tp,n)              ((tp*)mi_calloc(n,sizeof(tp)))
#define mi_mallocn_tp(tp,n)             ((tp*)mi_mallocn(n,sizeof(tp)))
#define mi_reallocn_tp(p,tp,n)          ((tp*)mi_reallocn(p,n,sizeof(tp)))
#define mi_recalloc_tp(p,tp,n)          ((tp*)mi_recalloc(p,n,sizeof(tp)))

#define mi_heap_malloc_tp(hp,tp)        ((tp*)mi_heap_malloc(hp,sizeof(tp)))
#define mi_heap_zalloc_tp(hp,tp)        ((tp*)mi_heap_zalloc(hp,sizeof(tp)))
#define mi_heap_calloc_tp(hp,tp,n)      ((tp*)mi_heap_calloc(hp,n,sizeof(tp)))
#define mi_heap_mallocn_tp(hp,tp,n)     ((tp*)mi_heap_mallocn(hp,n,sizeof(tp)))
#define mi_heap_reallocn_tp(hp,p,tp,n)  ((tp*)mi_heap_reallocn(hp,p,n,sizeof(tp)))
#define mi_heap_recalloc_tp(hp,p,tp,n)  ((tp*)mi_heap_recalloc(hp,p,n,sizeof(tp)))


// ------------------------------------------------------
// Options, all `false` by default
// ------------------------------------------------------

typedef enum mi_option_e {
  // stable options
  mi_option_show_errors,
  mi_option_show_stats,
  mi_option_verbose,
  // the following options are experimental
  mi_option_eager_commit,
  mi_option_eager_region_commit,
  mi_option_reset_decommits,
  mi_option_large_os_pages,         // implies eager commit
  mi_option_reserve_huge_os_pages,
  mi_option_segment_cache,
  mi_option_page_reset,
  mi_option_abandoned_page_reset,
  mi_option_segment_reset,
  mi_option_eager_commit_delay,
  mi_option_reset_delay,
  mi_option_use_numa_nodes,
  mi_option_debug_extra_padding,
  mi_option_os_tag,
  mi_option_max_errors,
  _mi_option_last
} mi_option_t;


mi_decl_nodiscard mi_decl_export bool mi_option_is_enabled(mi_option_t option);
mi_decl_export void mi_option_enable(mi_option_t option);
mi_decl_export void mi_option_disable(mi_option_t option);
mi_decl_export void mi_option_set_enabled(mi_option_t option, bool enable);
mi_decl_export void mi_option_set_enabled_default(mi_option_t option, bool enable);

mi_decl_nodiscard mi_decl_export long mi_option_get(mi_option_t option);
mi_decl_export void mi_option_set(mi_option_t option, long value);
mi_decl_export void mi_option_set_default(mi_option_t option, long value);


// -------------------------------------------------------------------------------------------------------
// "mi" prefixed implementations of various posix, Unix, Windows, and C++ allocation functions.
// (This can be convenient when providing overrides of these functions as done in `mimalloc-override.h`.)
// note: we use `mi_cfree` as "checked free" and it checks if the pointer is in our heap before free-ing.
// -------------------------------------------------------------------------------------------------------

mi_decl_nodiscard mi_decl_export size_t mi_malloc_size(const void* p)        mi_attr_noexcept;
mi_decl_nodiscard mi_decl_export size_t mi_malloc_usable_size(const void *p) mi_attr_noexcept;

mi_decl_export void mi_cfree(void* p) mi_attr_noexcept;
mi_decl_export void mi_free_size(void* p, size_t size) mi_attr_noexcept;
mi_decl_export void mi_free_size_aligned(void* p, size_t size, size_t alignment) mi_attr_noexcept;
mi_decl_export void mi_free_aligned(void* p, size_t alignment) mi_attr_noexcept;

mi_export_noexcpt(int,   mi_posix_memalign, void** p, size_t alignment, size_t size) mi_attr_alloc_size(3) mi_attr_alloc_align(2);
mi_export_malloc( void*, mi_memalign, size_t alignment, size_t size)           mi_attr_alloc_size(2) mi_attr_alloc_align(1);
mi_export_malloc( void*, mi_valloc, size_t size)                               mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_pvalloc, size_t size)                              mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_aligned_alloc, size_t alignment, size_t size)      mi_attr_alloc_size(2) mi_attr_alloc_align(1);
mi_export_realloc(void*, mi_reallocarray, void* p, size_t count, size_t size)  mi_attr_alloc_size2(2,3);
mi_export_realloc(void*, mi_aligned_recalloc, void* p, size_t newcount, size_t size, size_t alignment)  mi_attr_alloc_size2(2, 3) mi_attr_alloc_align(4);
mi_export_realloc(void*, mi_aligned_offset_recalloc, void* p, size_t newcount, size_t size, size_t alignment, size_t offset) mi_attr_alloc_size2(2, 3);

mi_export_malloc( wchar_t*,  mi_wcsdup, const wchar_t* s);
mi_export_malloc( unsigned char*, mi_mbsdup, const unsigned char* s);
mi_export_malloc( char*,     mi_getcwd, char* buf, size_t buf_len);

mi_export_noexcpt(int,      mi__dupenv_s, char** buf, size_t* size, const char* name);
mi_export_noexcpt(int,      mi__wdupenv_s, wchar_t** buf, size_t* size, const wchar_t* name);
mi_export_noexcpt(void*,    mi__expand, void* p, size_t newsize);
mi_export_malloc( char*,    mi__fullpath, char* buf, const char* path, size_t buf_len);
mi_export_malloc( wchar_t*, mi__wfullpath, wchar_t* buf, const wchar_t* path, size_t buf_len);
mi_export_malloc( wchar_t*, mi__wgetcwd, wchar_t* buf, size_t buf_len);
// todo: tempnam, _wgetdcwd, and _wgetdcwd_nolock

// The `mi_new` wrappers implement C++ semantics on out-of-memory instead of directly returning `NULL`.
// (and call `std::get_new_handler` and potentially raise a `std::bad_alloc` exception).
mi_export_new(    void*, mi_new, size_t size)                                   mi_attr_alloc_size(1) ;
mi_export_new(    void*, mi_new_aligned, size_t size, size_t alignment)         mi_attr_alloc_size(1) mi_attr_alloc_align(2) ;
mi_export_new(    void*, mi_new_n, size_t count, size_t size)                   mi_attr_alloc_size2(1, 2);
mi_export_malloc( void*, mi_new_nothrow, size_t size)                           mi_attr_alloc_size(1);
mi_export_malloc( void*, mi_new_aligned_nothrow, size_t size, size_t alignment) mi_attr_alloc_size(1) mi_attr_alloc_align(2);
mi_export_alloc(mi_decl_nodiscard, void*, mi_new_realloc, , void* p, size_t newsize) mi_attr_alloc_size(2);
mi_export_alloc(mi_decl_nodiscard, void*, mi_new_reallocn, , void* p, size_t newcount, size_t size) mi_attr_alloc_size2(2, 3);


// ----------------------------------------------------------------------
// end of extern "C"
// ----------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------------------------
// Implement the C++ std::allocator interface for use in STL containers.
// (note: see `mimalloc-new-delete.h` for overriding the new/delete operators globally)
// ---------------------------------------------------------------------------------------------
#ifdef __cplusplus

#include <cstdint>     // PTRDIFF_MAX
#if (__cplusplus >= 201103L) || (_MSC_VER > 1900)  // C++11
#include <type_traits> // std::true_type
#include <utility>     // std::forward
#endif

template<class T> struct mi_stl_allocator {
  typedef T                 value_type;
  typedef std::size_t       size_type;
  typedef std::ptrdiff_t    difference_type;
  typedef value_type& reference;
  typedef value_type const& const_reference;
  typedef value_type* pointer;
  typedef value_type const* const_pointer;
  template <class U> struct rebind { typedef mi_stl_allocator<U> other; };

  mi_stl_allocator()                                             mi_attr_noexcept = default;
  mi_stl_allocator(const mi_stl_allocator&)                      mi_attr_noexcept = default;
  template<class U> mi_stl_allocator(const mi_stl_allocator<U>&) mi_attr_noexcept { }
  mi_stl_allocator  select_on_container_copy_construction() const { return *this; }
  void              deallocate(T* p, size_type) { mi_free(p); }

#if (__cplusplus >= 201703L)  // C++17
  mi_decl_nodiscard T* allocate(size_type count) { return static_cast<T*>(mi_new_n(count, sizeof(T))); }
  mi_decl_nodiscard T* allocate(size_type count, const void*) { return allocate(count); }
#else
  mi_decl_nodiscard pointer allocate(size_type count, const void* = 0) { return static_cast<pointer>(mi_new_n(count, sizeof(value_type))); }
#endif

#if ((__cplusplus >= 201103L) || (_MSC_VER > 1900))  // C++11
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::true_type;
  template <class U, class ...Args> void construct(U* p, Args&& ...args) { ::new(p) U(std::forward<Args>(args)...); }
  template <class U> void destroy(U* p) mi_attr_noexcept { p->~U(); }
#else
  void construct(pointer p, value_type const& val) { ::new(p) value_type(val); }
  void destroy(pointer p) { p->~value_type(); }
#endif

  size_type     max_size() const mi_attr_noexcept { return (PTRDIFF_MAX/sizeof(value_type)); }
  pointer       address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }
};

template<class T1, class T2> bool operator==(const mi_stl_allocator<T1>&, const mi_stl_allocator<T2>&) mi_attr_noexcept { return true; }
template<class T1, class T2> bool operator!=(const mi_stl_allocator<T1>&, const mi_stl_allocator<T2>&) mi_attr_noexcept { return false; }
#endif // __cplusplus


// -------------------------------------------------------------------------------
// Debugging API that tracks the source location of an allocation
// In debug build all api entries are redefined to use the `dbg_` entry 
// and pass the allocation location as a last argument.
// The various `MI_SOURCE` macros enable defining custom allocation functions
// that pass through the source locations properly (see `mimalloc-new-delete.h`)
// -------------------------------------------------------------------------------

#if defined(NDEBUG) 

#define MI_SOURCE_XPARAM
#define MI_SOURCE_XARG
#define MI_SOURCE_XRET()
#define MI_SOURCE_XLOC()
#define MI_SOURCE_ARG(fun,...)  fun(__VA_ARGS__)
#define MI_SOURCE_RET(fun,...)  fun(__VA_ARGS__)
#define MI_SOURCE_LOC(fun,...)  fun(__VA_ARGS__)

#else

#if defined(_MSC_VER) && !defined(__clang__)  // exclude clang-cl
#include <intrin.h>
#define mi_return_address()     _ReturnAddress()
#elif (defined(__GNUC__) && (__GNUC__>=3))    // includes clang and icc
#define mi_return_address()     __builtin_return_address(0)
#else
#define mi_return_address()     NULL
#endif
#define MI_SOURCE_XPARAM        , mi_source_t __mi_source                  // declare the extra source parameter
#define MI_SOURCE_XARG          , __mi_source                              // pass the extra source parameter as a source argument
#define MI_SOURCE_XRET()        , mi_source_ret(mi_return_address())       // pass the return address as a source argument
#define MI_SOURCE_XLOC()        , mi_source_loc(__FILE__,__LINE__)         // pass the current source location as a source argument
#define MI_SOURCE_ARG(fun,...)  dbg_##fun(__VA_ARGS__  MI_SOURCE_XARG)     // call the debug entry with the given source argument
#define MI_SOURCE_RET(fun,...)  dbg_##fun(__VA_ARGS__  MI_SOURCE_XRET())   // call the debug entry with the return address as the source
#define MI_SOURCE_LOC(fun,...)  dbg_##fun(__VA_ARGS__  MI_SOURCE_XLOC())   // call the debug entry with the current source location as the source 
#endif

#if !defined(NDEBUG) && !defined(MI_DEBUG_NO_SOURCE_LOC)

#define mi_malloc(n)                             MI_SOURCE_LOC(mi_malloc,n)
#define mi_calloc(c,n)                           MI_SOURCE_LOC(mi_calloc,c,n)
#define mi_realloc(p,n)                          MI_SOURCE_LOC(mi_realloc,p,n)
#define mi_expand(p,n)                           MI_SOURCE_LOC(mi_expand,p,n)
#define mi_strdup(s)                             MI_SOURCE_LOC(mi_strdup,s)
#define mi_strndup(s,n)                          MI_SOURCE_LOC(mi_strndup,s,n)
#define mi_realpath(f,n)                         MI_SOURCE_LOC(mi_realpath,f,n)
#define mi_malloc_small(n)                       MI_SOURCE_LOC(mi_malloc_small,n)
#define mi_zalloc_small(n)                       MI_SOURCE_LOC(mi_zalloc_small,n)
#define mi_zalloc(n)                             MI_SOURCE_LOC(mi_zalloc,n)
#define mi_mallocn(c,n)                          MI_SOURCE_LOC(mi_mallocn,c,n)
#define mi_reallocn(p,c,n)                       MI_SOURCE_LOC(mi_reallocn,p,c,n)
#define mi_reallocf(p,n)                         MI_SOURCE_LOC(mi_reallocf,p,n)

#define mi_malloc_aligned(n,a)                   MI_SOURCE_LOC(mi_malloc_aligned,n,a)
#define mi_zalloc_aligned(n,a)                   MI_SOURCE_LOC(mi_zalloc_aligned,n,a)
#define mi_calloc_aligned(c,n,a)                 MI_SOURCE_LOC(mi_calloc_aligned,c,n,a)
#define mi_realloc_aligned(p,n,a)                MI_SOURCE_LOC(mi_realloc_aligned,p,n,a)
#define mi_malloc_aligned_at(n,a,o)              MI_SOURCE_LOC(mi_malloc_aligned_at,n,a,o)
#define mi_zalloc_aligned_at(n,a,o)              MI_SOURCE_LOC(mi_zalloc_aligned_at,n,a,o)
#define mi_calloc_aligned_at(c,n,a,o)            MI_SOURCE_LOC(mi_calloc_aligned_at,c,n,a,o)
#define mi_realloc_aligned_at(p,n,a,o)           MI_SOURCE_LOC(mi_realloc_aligned_at,p,n,a,o)

#define mi_rezalloc(p,n)                         MI_SOURCE_LOC(mi_rezalloc,p,n)
#define mi_recalloc(p,c,n)                       MI_SOURCE_LOC(mi_recalloc,p,c,n)
#define mi_rezalloc_aligned(p,n,a)               MI_SOURCE_LOC(mi_rezalloc_aligned,p,n,a)
#define mi_recalloc_aligned(p,c,n,a)             MI_SOURCE_LOC(mi_recalloc_aligned,p,c,n,a)
#define mi_rezalloc_aligned_at(p,n,a,o)          MI_SOURCE_LOC(mi_rezalloc_aligned_at,p,n,a,o)
#define mi_recalloc_aligned_at(p,c,n,a,o)        MI_SOURCE_LOC(mi_recalloc_aligned_at,p,c,n,a,o)

#define mi_heap_malloc(h,n)                      MI_SOURCE_LOC(mi_heap_malloc,h,n)
#define mi_heap_calloc(h,c,n)                    MI_SOURCE_LOC(mi_heap_calloc,h,c,n)
#define mi_heap_realloc(h,p,n)                   MI_SOURCE_LOC(mi_heap_realloc,h,p,n)
#define mi_heap_strdup(h,s)                      MI_SOURCE_LOC(mi_heap_strdup,h,s)
#define mi_heap_strndup(h,s,n)                   MI_SOURCE_LOC(mi_heap_strndup,h,s,n)
#define mi_heap_realpath(h,f,n)                  MI_SOURCE_LOC(mi_heap_realpath,h,f,n)
#define mi_heap_malloc_small(h,n)                MI_SOURCE_LOC(mi_heap_malloc_small,h,n)
#define mi_heap_zalloc_small(h,n)                MI_SOURCE_LOC(mi_heap_zalloc_small,h,n)
#define mi_heap_zalloc(h,n)                      MI_SOURCE_LOC(mi_heap_zalloc,h,n)
#define mi_heap_mallocn(h,c,n)                   MI_SOURCE_LOC(mi_heap_mallocn,h,c,n)
#define mi_heap_reallocn(h,p,c,n)                MI_SOURCE_LOC(mi_heap_reallocn,h,p,c,n)
#define mi_heap_reallocf(h,p,n)                  MI_SOURCE_LOC(mi_heap_reallocf,h,p,n)

#define mi_heap_malloc_aligned(h,n,a)            MI_SOURCE_LOC(mi_heap_malloc_aligned,h,n,a)
#define mi_heap_zalloc_aligned(h,n,a)            MI_SOURCE_LOC(mi_heap_zalloc_aligned,h,n,a)
#define mi_heap_calloc_aligned(h,c,n,a)          MI_SOURCE_LOC(mi_heap_calloc_aligned,h,c,n,a)
#define mi_heap_realloc_aligned(h,p,n,a)         MI_SOURCE_LOC(mi_heap_realloc_aligned,h,p,n,a)
#define mi_heap_malloc_aligned_at(h,n,a,o)       MI_SOURCE_LOC(mi_heap_malloc_aligned_at,h,n,a,o)
#define mi_heap_zalloc_aligned_at(h,n,a,o)       MI_SOURCE_LOC(mi_heap_zalloc_aligned_at,h,n,a,o)
#define mi_heap_calloc_aligned_at(h,c,n,a,o)     MI_SOURCE_LOC(mi_heap_calloc_aligned_at,h,c,n,a,o)
#define mi_heap_realloc_aligned_at(h,p,n,a,o)    MI_SOURCE_LOC(mi_heap_realloc_aligned_at,h,p,n,a,o)

#define mi_heap_rezalloc(h,p,n)                  MI_SOURCE_LOC(mi_heap_rezalloc,h,p,n)
#define mi_heap_recalloc(h,p,c,n)                MI_SOURCE_LOC(mi_heap_recalloc,h,p,c,n)
#define mi_heap_rezalloc_aligned(h,p,n,a)        MI_SOURCE_LOC(mi_heap_rezalloc_aligned,h,p,n,a)
#define mi_heap_recalloc_aligned(h,p,c,n,a)      MI_SOURCE_LOC(mi_heap_recalloc_aligned,h,p,c,n,a)
#define mi_heap_rezalloc_aligned_at(h,p,n,a,o)   MI_SOURCE_LOC(mi_heap_rezalloc_aligned_at,h,p,n,a,o)
#define mi_heap_recalloc_aligned_at(h,p,c,n,a,o) MI_SOURCE_LOC(mi_heap_recalloc_aligned_at,h,p,c,n,a,o)

#define mi_wcsdup(s)                             MI_SOURCE_LOC(mi_wcsdup,s)
#define mi_mbsdup(s)                             MI_SOURCE_LOC(mi_mbsdup,s)
#define mi_getcwd(b,n)                           MI_SOURCE_LOC(mi_getcwd(b,n)
#define mi__dupenv_s(b,s,n)                      MI_SOURCE_LOC(mi__dupenv_s,b,s,n)
#define mi__wdupenv_s(b,s,n)                     MI_SOURCE_LOC(mi__wdupenv_s,b,s,n)
#define mi__expand(p,n)                          MI_SOURCE_LOC(mi__expand,p,n)
#define mi__fullpath(b,p,n)                      MI_SOURCE_LOC(mi__fullpath,b,p,n)
#define mi__wfullpath(b,p,n)                     MI_SOURCE_LOC(mi__wfullpath,b,p,n)
#define mi__wgetcwd(b,n)                         MI_SOURCE_LOC(mi__wgetcwd,b,n)

#define mi_posix_memalign(p,a,s)                 MI_SOURCE_LOC(mi_posix_memalign,p,a,s)
#define mi_memalign(a,s)                         MI_SOURCE_LOC(mi_memalign,a,s)
#define mi_valloc(s)                             MI_SOURCE_LOC(mi_valloc,s)
#define mi_pvalloc(s)                            MI_SOURCE_LOC(mi_pvalloc,s)
#define mi_aligned_alloc(a,s)                    MI_SOURCE_LOC(mi_aligned_alloc,a,s)
#define mi_reallocarray(p,c,s)                   MI_SOURCE_LOC(mi_reallocarray,p,c,s)

#define mi_aligned_recalloc(p,c,s,a)             MI_SOURCE_LOC(mi_aligned_recalloc,p,c,s,a)
#define mi_aligned_offset_recalloc(p,c,s,a,o)    MI_SOURCE_LOC(mi_aligned_offset_recalloc,p,c,s,a,o)

#endif


#endif // MIMALLOC_H
