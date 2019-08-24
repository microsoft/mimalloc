/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_TYPES_H
#define MIMALLOC_TYPES_H

#include <stddef.h>   // ptrdiff_t
#include <stdint.h>   // uintptr_t, uint16_t, etc

// ------------------------------------------------------
// Variants
// ------------------------------------------------------

// Define NDEBUG in the release version to disable assertions.
// #define NDEBUG

// Define MI_STAT as 1 to maintain statistics; set it to 2 to have detailed statistics (but costs some performance).
// #define MI_STAT 1

// Define MI_SECURE as 1 to encode free lists
// #define MI_SECURE 1

#if !defined(MI_SECURE)
#define MI_SECURE 0
#endif

// Define MI_DEBUG as 1 for basic assert checks and statistics
// set it to 2 to do internal asserts,
// and to 3 to do extensive invariant checking.
#if !defined(MI_DEBUG)
#if !defined(NDEBUG) || defined(_DEBUG)
#define MI_DEBUG 1
#else
#define MI_DEBUG 0
#endif
#endif


// ------------------------------------------------------
// Platform specific values
// ------------------------------------------------------


// ------------------------------------------------------
// Size of a pointer.
// We assume that `sizeof(void*)==sizeof(intptr_t)`
// and it holds for all platforms we know of.
//
// However, the C standard only requires that:
//  p == (void*)((intptr_t)p))
// but we also need:
//  i == (intptr_t)((void*)i)
// or otherwise one might define an intptr_t type that is larger than a pointer...
// ------------------------------------------------------

#if INTPTR_MAX == 9223372036854775807LL
# define MI_INTPTR_SHIFT (3)
#elif INTPTR_MAX == 2147483647LL
# define MI_INTPTR_SHIFT (2)
#else
#error platform must be 32 or 64 bits
#endif

#define MI_INTPTR_SIZE  (1<<MI_INTPTR_SHIFT)


// ------------------------------------------------------
// Main internal data-structures
// ------------------------------------------------------

// Main tuning parameters for segment and page sizes
// Sizes for 64-bit, divide by two for 32-bit
#define MI_SEGMENT_SLICE_SHIFT            (13 + MI_INTPTR_SHIFT)         // 64kb
#define MI_SEGMENT_SHIFT                  (10 + MI_SEGMENT_SLICE_SHIFT)  // 64mb

#define MI_SMALL_PAGE_SHIFT               (MI_SEGMENT_SLICE_SHIFT)       // 64kb
#define MI_MEDIUM_PAGE_SHIFT              ( 2 + MI_SEGMENT_SLICE_SHIFT)  // 512kb


// Derived constants
#define MI_SEGMENT_SIZE                   ((size_t)1<<MI_SEGMENT_SHIFT)
#define MI_SEGMENT_MASK                   (MI_SEGMENT_SIZE - 1)
#define MI_SEGMENT_SLICE_SIZE             ((size_t)1 << MI_SEGMENT_SLICE_SHIFT)
#define MI_SLICES_PER_SEGMENT             (MI_SEGMENT_SIZE / MI_SEGMENT_SLICE_SIZE) // 1024

#define MI_SMALL_PAGE_SIZE                (1<<MI_SMALL_PAGE_SHIFT)
#define MI_MEDIUM_PAGE_SIZE               (1<<MI_MEDIUM_PAGE_SHIFT)

#define MI_SMALL_OBJ_SIZE_MAX             (MI_SMALL_PAGE_SIZE/8)   // 8kb on 64-bit

#define MI_MEDIUM_OBJ_SIZE_MAX            (MI_MEDIUM_PAGE_SIZE/4)  // 128kb on 64-bit
#define MI_MEDIUM_OBJ_WSIZE_MAX           (MI_MEDIUM_OBJ_SIZE_MAX/MI_INTPTR_SIZE)   // 64kb on 64-bit

#define MI_LARGE_OBJ_SIZE_MAX             (MI_SEGMENT_SIZE/4)      // 16mb on 64-bit
#define MI_LARGE_OBJ_WSIZE_MAX            (MI_LARGE_OBJ_SIZE_MAX/MI_INTPTR_SIZE)

// Minimal alignment necessary. On most platforms 16 bytes are needed
// due to SSE registers for example. This must be at least `MI_INTPTR_SIZE`
#define MI_MAX_ALIGN_SIZE  16   // sizeof(max_align_t)

// Maximum number of size classes. (spaced exponentially in 12.5% increments)
#define MI_BIN_HUGE  (73U)

#if (MI_MEDIUM_OBJ_WSIZE_MAX >= 655360)
#error "define more bins"
#endif

typedef uintptr_t mi_encoded_t;

// free lists contain blocks
typedef struct mi_block_s {
  mi_encoded_t next;
} mi_block_t;


typedef enum mi_delayed_e {
  MI_NO_DELAYED_FREE = 0,
  MI_USE_DELAYED_FREE = 1,
  MI_DELAYED_FREEING = 2,
  MI_NEVER_DELAYED_FREE = 3
} mi_delayed_t;


// The `in_full` and `has_aligned` page flags are put in a union to efficiently 
// test if both are false (`value == 0`) in the `mi_free` routine.
typedef union mi_page_flags_u {
  uint16_t value;
  struct {
    bool in_full;
    bool has_aligned;
  };
} mi_page_flags_t;

// Thread free list.
// We use the bottom 2 bits of the pointer for mi_delayed_t flags
typedef uintptr_t mi_thread_free_t;

// A page contains blocks of one specific size (`block_size`).
// Each page has three list of free blocks:
// `free` for blocks that can be allocated,
// `local_free` for freed blocks that are not yet available to `mi_malloc`
// `thread_free` for freed blocks by other threads
// The `local_free` and `thread_free` lists are migrated to the `free` list
// when it is exhausted. The separate `local_free` list is necessary to
// implement a monotonic heartbeat. The `thread_free` list is needed for
// avoiding atomic operations in the common case.
//
// `used - thread_freed` == actual blocks that are in use (alive)
// `used - thread_freed + |free| + |local_free| == capacity`
//
// note: we don't count `freed` (as |free|) instead of `used` to reduce
//       the number of memory accesses in the `mi_page_all_free` function(s).
// note: the funny layout here is due to:
// - access is optimized for `mi_free` and `mi_page_alloc`
// - using `uint16_t` does not seem to slow things down
typedef struct mi_page_s {
  // "owned" by the segment
  uint32_t              slice_count;       // slices in this page (0 if not a page)
  uint32_t              slice_offset;      // distance from the actual page data slice (0 if a page)
  bool                  is_reset;          // `true` if the page memory was reset
  bool                  is_committed;      // `true` if the page virtual memory is committed

  // layout like this to optimize access in `mi_malloc` and `mi_free`
  uint16_t              capacity;          // number of blocks committed
  uint16_t              reserved;          // number of blocks reserved in memory
  mi_page_flags_t       flags;             // `in_full` and `has_aligned` flags (16 bits)

  mi_block_t*           free;              // list of available free blocks (`malloc` allocates from this list)
  #if MI_SECURE
  uintptr_t             cookie;            // random cookie to encode the free lists
  #endif
  size_t                used;              // number of blocks in use (including blocks in `local_free` and `thread_free`)

  mi_block_t*           local_free;        // list of deferred free blocks by this thread (migrates to `free`)
  volatile uintptr_t    thread_freed;      // at least this number of blocks are in `thread_free`
  volatile mi_thread_free_t thread_free;   // list of deferred free blocks freed by other threads

  // less accessed info
  size_t                block_size;        // size available in each block (always `>0`)
  mi_heap_t*            heap;              // the owning heap
  struct mi_page_s*     next;              // next page owned by this thread with the same `block_size`
  struct mi_page_s*     prev;              // previous page owned by this thread with the same `block_size`

  // improve page index calculation
  // without padding: 11 words on 64-bit, 13 on 32-bit. Secure adds one word
  #if (MI_SECURE==0)
  void*                 padding[1];        // 12 words on 64-bit, 14 words on 32-bit 
  #endif
} mi_page_t;



typedef enum mi_page_kind_e {
  MI_PAGE_SMALL,    // small blocks go into 64kb pages inside a segment
  MI_PAGE_MEDIUM,   // medium blocks go into 512kb pages inside a segment
  MI_PAGE_LARGE,    // larger blocks go into a page of just one block
  MI_PAGE_HUGE,     // huge blocks (>16mb) are put into a single page in a single segment.
} mi_page_kind_t;

typedef enum mi_segment_kind_e {
  MI_SEGMENT_NORMAL, // MI_SEGMENT_SIZE size with pages inside.
  MI_SEGMENT_HUGE,   // > MI_LARGE_SIZE_MAX segment with just one huge page inside.
} mi_segment_kind_t;

typedef mi_page_t mi_slice_t;

// Segments are large allocated memory blocks (2mb on 64 bit) from
// the OS. Inside segments we allocated fixed size _pages_ that
// contain blocks.
typedef struct mi_segment_s {
  struct mi_segment_s*          next;            // the list of freed segments in the cache
  volatile struct mi_segment_s* abandoned_next;  // the list of abandoned segments
  size_t          abandoned;          // abandoned pages (i.e. the original owning thread stopped) (`abandoned <= used`)
  size_t          used;               // count of pages in use
  size_t          segment_size;       // for huge pages this may be different from `MI_SEGMENT_SIZE`
  size_t          segment_info_size;  // space we are using from the first page for segment meta-data and possible guard pages.
  uintptr_t       cookie;             // verify addresses in debug mode: `mi_ptr_cookie(segment) == segment->cookie`
  size_t          memid;              // id for the os-level memory manager
  bool            all_committed;

  // layout like this to optimize access in `mi_free`
  mi_segment_kind_t kind;
  uintptr_t         thread_id;
  size_t            slice_count; // slices in this segment (at most MI_SLICES_PER_SEGMENT)
  mi_slice_t        slices[MI_SLICES_PER_SEGMENT];
} mi_segment_t;


// ------------------------------------------------------
// Heaps
// Provide first-class heaps to allocate from.
// A heap just owns a set of pages for allocation and
// can only be allocate/reallocate from the thread that created it.
// Freeing blocks can be done from any thread though.
// Per thread, the segments are shared among its heaps.
// Per thread, there is always a default heap that is
// used for allocation; it is initialized to statically
// point to an empty heap to avoid initialization checks
// in the fast path.
// ------------------------------------------------------

// Thread local data
typedef struct mi_tld_s mi_tld_t;

// Pages of a certain block size are held in a queue.
typedef struct mi_page_queue_s {
  mi_page_t* first;
  mi_page_t* last;
  size_t     block_size;
} mi_page_queue_t;

#define MI_BIN_FULL  (MI_BIN_HUGE+1)

// A heap owns a set of pages.
struct mi_heap_s {
  mi_tld_t*             tld;
  mi_page_t*            pages_free_direct[MI_SMALL_WSIZE_MAX + 2];   // optimize: array where every entry points a page with possibly free blocks in the corresponding queue for that size.
  mi_page_queue_t       pages[MI_BIN_FULL + 1];                      // queue of pages for each size class (or "bin")
  volatile mi_block_t*  thread_delayed_free;
  uintptr_t             thread_id;                                   // thread this heap belongs too
  uintptr_t             cookie;
  uintptr_t             random;                                      // random number used for secure allocation
  size_t                page_count;                                  // total number of pages in the `pages` queues.
  bool                  no_reclaim;                                  // `true` if this heap should not reclaim abandoned pages
};



// ------------------------------------------------------
// Debug
// ------------------------------------------------------

#define MI_DEBUG_UNINIT     (0xD0)
#define MI_DEBUG_FREED      (0xDF)


#if (MI_DEBUG)
// use our own assertion to print without memory allocation
void _mi_assert_fail(const char* assertion, const char* fname, unsigned int line, const char* func );
#define mi_assert(expr)     ((expr) ? (void)0 : _mi_assert_fail(#expr,__FILE__,__LINE__,__func__))
#else
#define mi_assert(x)
#endif

#if (MI_DEBUG>1)
#define mi_assert_internal    mi_assert
#else
#define mi_assert_internal(x)
#endif

#if (MI_DEBUG>2)
#define mi_assert_expensive   mi_assert
#else
#define mi_assert_expensive(x)
#endif

// ------------------------------------------------------
// Statistics
// ------------------------------------------------------

#ifndef MI_STAT
#if (MI_DEBUG>0)
#define MI_STAT 2
#else
#define MI_STAT 0
#endif
#endif

typedef struct mi_stat_count_s {
  int64_t allocated;
  int64_t freed;
  int64_t peak;
  int64_t current;
} mi_stat_count_t;

typedef struct mi_stat_counter_s {
  int64_t total;
  int64_t count;
} mi_stat_counter_t;

typedef struct mi_stats_s {
  mi_stat_count_t segments;
  mi_stat_count_t pages;
  mi_stat_count_t reserved;
  mi_stat_count_t committed;
  mi_stat_count_t reset;
  mi_stat_count_t page_committed;
  mi_stat_count_t segments_abandoned;
  mi_stat_count_t pages_abandoned;
  mi_stat_count_t pages_extended;
  mi_stat_count_t mmap_calls;
  mi_stat_count_t commit_calls;
  mi_stat_count_t threads;
  mi_stat_count_t huge;
  mi_stat_count_t large;
  mi_stat_count_t malloc;
  mi_stat_count_t segments_cache;
  mi_stat_counter_t page_no_retire;
  mi_stat_counter_t searches;
  mi_stat_counter_t huge_count;
  mi_stat_counter_t large_count;
#if MI_STAT>1
  mi_stat_count_t normal[MI_BIN_HUGE+1];
#endif
} mi_stats_t;


void _mi_stat_increase(mi_stat_count_t* stat, size_t amount);
void _mi_stat_decrease(mi_stat_count_t* stat, size_t amount);
void _mi_stat_counter_increase(mi_stat_counter_t* stat, size_t amount);

#if (MI_STAT)
#define mi_stat_increase(stat,amount)         _mi_stat_increase( &(stat), amount)
#define mi_stat_decrease(stat,amount)         _mi_stat_decrease( &(stat), amount)
#define mi_stat_counter_increase(stat,amount) _mi_stat_counter_increase( &(stat), amount)
#else
#define mi_stat_increase(stat,amount)         (void)0
#define mi_stat_decrease(stat,amount)         (void)0
#define mi_stat_counter_increase(stat,amount) (void)0
#endif

#define mi_heap_stat_increase(heap,stat,amount)  mi_stat_increase( (heap)->tld->stats.stat, amount)
#define mi_heap_stat_decrease(heap,stat,amount)  mi_stat_decrease( (heap)->tld->stats.stat, amount)


// ------------------------------------------------------
// Thread Local data
// ------------------------------------------------------

// Queue of segments
typedef struct mi_segment_queue_s {
  mi_segment_t* first;
  mi_segment_t* last;
} mi_segment_queue_t;

#define MI_SEGMENT_BIN_MAX (35)     // 35 == mi_segment_bin(MI_SLICES_PER_SEGMENT)

// Segments thread local data
typedef struct mi_segments_tld_s {
  mi_page_queue_t     pages[MI_SEGMENT_BIN_MAX+1];  // free pages inside segments
  size_t              count;        // current number of segments;
  size_t              peak_count;   // peak number of segments
  size_t              current_size; // current size of all segments
  size_t              peak_size;    // peak size of all segments
  size_t              cache_count;  // number of segments in the cache
  size_t              cache_size;   // total size of all segments in the cache
  mi_segment_t*       cache;        // (small) cache of segments
  mi_stats_t*         stats;        // points to tld stats
} mi_segments_tld_t;

// OS thread local data
typedef struct mi_os_tld_s {
  size_t              region_idx;   // start point for next allocation
  mi_stats_t*         stats;        // points to tld stats
} mi_os_tld_t;

// Thread local data
struct mi_tld_s {
  unsigned long long  heartbeat;     // monotonic heartbeat count
  mi_heap_t*          heap_backing;  // backing heap of this thread (cannot be deleted)
  mi_segments_tld_t   segments;      // segment tld
  mi_os_tld_t         os;            // os tld
  mi_stats_t          stats;         // statistics
};

#endif
