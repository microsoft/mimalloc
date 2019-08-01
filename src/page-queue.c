/*----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* -----------------------------------------------------------
  Definition of page queues for each block size
----------------------------------------------------------- */

#ifndef MI_IN_PAGE_C
#error "this file should be included from 'page.c'"
#endif

/* -----------------------------------------------------------
  Minimal alignment in machine words (i.e. `sizeof(void*)`)
----------------------------------------------------------- */

#if (MI_MAX_ALIGN_SIZE > 4*MI_INTPTR_SIZE)
  #error "define alignment for more than 4x word size for this platform"
#elif (MI_MAX_ALIGN_SIZE > 2*MI_INTPTR_SIZE)
  #define MI_ALIGN4W   // 4 machine words minimal alignment
#elif (MI_MAX_ALIGN_SIZE > MI_INTPTR_SIZE)
  #define MI_ALIGN2W   // 2 machine words minimal alignment
#else
  // ok, default alignment is 1 word
#endif


/* -----------------------------------------------------------
  Queue query
----------------------------------------------------------- */


static inline bool mi_page_queue_is_huge(const mi_page_queue_t* pq) {
  return (pq->block_size == (MI_LARGE_SIZE_MAX+sizeof(uintptr_t)));
}

static inline bool mi_page_queue_is_full(const mi_page_queue_t* pq) {
  return (pq->block_size == (MI_LARGE_SIZE_MAX+(2*sizeof(uintptr_t))));
}

static inline bool mi_page_queue_is_special(const mi_page_queue_t* pq) {
  return (pq->block_size > MI_LARGE_SIZE_MAX);
}

/* -----------------------------------------------------------
  Bins
----------------------------------------------------------- */

// Bit scan reverse: return the index of the highest bit.
static inline uint8_t mi_bsr32(uint32_t x);

#if defined(_MSC_VER)
#include <intrin.h>
static inline uint8_t mi_bsr32(uint32_t x) {
  uint32_t idx;
  _BitScanReverse((DWORD*)&idx, x);
  return idx;
}
#elif defined(__GNUC__) || defined(__clang__)
static inline uint8_t mi_bsr32(uint32_t x) {
  return (31 - __builtin_clz(x));
}
#else
static inline uint8_t mi_bsr32(uint32_t x) {
  // de Bruijn multiplication, see <http://supertech.csail.mit.edu/papers/debruijn.pdf>
  static const uint8_t debruijn[32] = {
     31,  0, 22,  1, 28, 23, 18,  2, 29, 26, 24, 10, 19,  7,  3, 12,
     30, 21, 27, 17, 25,  9,  6, 11, 20, 16,  8,  5, 15,  4, 14, 13,
  };
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x++;
  return debruijn[(x*0x076be629) >> 27];
}
#endif

// Bit scan reverse: return the index of the highest bit.
uint8_t _mi_bsr(uintptr_t x) {
  if (x == 0) return 0;
#if MI_INTPTR_SIZE==8
  uint32_t hi = (x >> 32);
  return (hi == 0 ? mi_bsr32((uint32_t)x) : 32 + mi_bsr32(hi));
#elif MI_INTPTR_SIZE==4
  return mi_bsr32(x);
#else
# error "define bsr for non-32 or 64-bit platforms"
#endif
}

// Return the bin for a given field size.
// Returns MI_BIN_HUGE if the size is too large.
// We use `wsize` for the size in "machine word sizes",
// i.e. byte size == `wsize*sizeof(void*)`.
inline uint8_t _mi_bin(size_t size) {
  size_t wsize = _mi_wsize_from_size(size);
  uint8_t bin;
  if (wsize <= 1) {
    bin = 1;
  }
  #if defined(MI_ALIGN4W)
  else if (wsize <= 4) {
    bin = (uint8_t)((wsize+1)&~1); // round to double word sizes
  }
  #elif defined(MI_ALIGN2W)
  else if (wsize <= 8) {
    bin = (uint8_t)((wsize+1)&~1); // round to double word sizes
  }
  #else
  else if (wsize <= 8) {
    bin = (uint8_t)wsize;
  }
  #endif
  else if (wsize > MI_LARGE_WSIZE_MAX) {
    bin = MI_BIN_HUGE;
  }
  else {
    #if defined(MI_ALIGN4W)
    if (wsize <= 16) { wsize = (wsize+3)&~3; } // round to 4x word sizes
    #endif
    wsize--;
    // find the highest bit
    uint8_t b = mi_bsr32((uint32_t)wsize);
    // and use the top 3 bits to determine the bin (~16% worst internal fragmentation).
    // - adjust with 3 because we use do not round the first 8 sizes
    //   which each get an exact bin
    bin = ((b << 2) + (uint8_t)((wsize >> (b - 2)) & 0x03)) - 3;
  }
  mi_assert_internal(bin > 0 && bin <= MI_BIN_HUGE);
  return bin;
}



/* -----------------------------------------------------------
  Queue of pages with free blocks
----------------------------------------------------------- */

size_t _mi_bin_size(uint8_t bin) {
  return _mi_heap_empty.pages[bin].block_size;
}

// Good size for allocation
size_t mi_good_size(size_t size) mi_attr_noexcept {
  if (size <= MI_LARGE_SIZE_MAX) {
    return _mi_bin_size(_mi_bin(size));
  }
  else {
    return _mi_align_up(size,_mi_os_page_size());
  }
}

#if (MI_DEBUG>1)
static bool mi_page_queue_contains(mi_page_queue_t* queue, const mi_page_t* page) {
  mi_assert_internal(page != NULL);
  mi_page_t* list = queue->first;
  while (list != NULL) {
    mi_assert_internal(list->next == NULL || list->next->prev == list);
    mi_assert_internal(list->prev == NULL || list->prev->next == list);
    if (list == page) break;
    list = list->next;
  }
  return (list == page);
}

#endif

#if (MI_DEBUG>1)
static bool mi_heap_contains_queue(const mi_heap_t* heap, const mi_page_queue_t* pq) {
  return (pq >= &heap->pages[0] && pq <= &heap->pages[MI_BIN_FULL]);
}
#endif

static mi_page_queue_t* mi_page_queue_of(const mi_page_t* page) {
  uint8_t bin = (page->flags.in_full ? MI_BIN_FULL : _mi_bin(page->block_size));
  mi_heap_t* heap = page->heap;
  mi_assert_internal(heap != NULL && bin <= MI_BIN_FULL);
  mi_page_queue_t* pq = &heap->pages[bin];
  mi_assert_internal(bin >= MI_BIN_HUGE || page->block_size == pq->block_size);
  mi_assert_expensive(mi_page_queue_contains(pq, page));
  return pq;
}

static mi_page_queue_t* mi_heap_page_queue_of(mi_heap_t* heap, const mi_page_t* page) {
  uint8_t bin = (page->flags.in_full ? MI_BIN_FULL : _mi_bin(page->block_size));
  mi_assert_internal(bin <= MI_BIN_FULL);
  mi_page_queue_t* pq = &heap->pages[bin];
  mi_assert_internal(page->flags.in_full || page->block_size == pq->block_size);
  return pq;
}

// The current small page array is for efficiency and for each
// small size (up to 256) it points directly to the page for that
// size without having to compute the bin. This means when the
// current free page queue is updated for a small bin, we need to update a
// range of entries in `_mi_page_small_free`.
static inline void mi_heap_queue_first_update(mi_heap_t* heap, const mi_page_queue_t* pq) {
  mi_assert_internal(mi_heap_contains_queue(heap,pq));
  size_t size = pq->block_size;
  if (size > MI_SMALL_SIZE_MAX) return;

  mi_page_t* page = pq->first;
  if (pq->first == NULL) page = (mi_page_t*)&_mi_page_empty;

  // find index in the right direct page array
  size_t start;
  size_t idx = _mi_wsize_from_size(size);
  mi_page_t** pages_free = heap->pages_free_direct;

  if (pages_free[idx] == page) return;  // already set

  // find start slot
  if (idx<=1) {
    start = 0;
  }
  else {
    // find previous size; due to minimal alignment upto 3 previous bins may need to be skipped
    uint8_t bin = _mi_bin(size);
    const mi_page_queue_t* prev = pq - 1;
    while( bin == _mi_bin(prev->block_size) && prev > &heap->pages[0]) {
      prev--;
    }
    start = 1 + _mi_wsize_from_size(prev->block_size);
    if (start > idx) start = idx;
  }

  // set size range to the right page
  mi_assert(start <= idx);
  for (size_t sz = start; sz <= idx; sz++) {
    pages_free[sz] = page;
  }
}

/*
static bool mi_page_queue_is_empty(mi_page_queue_t* queue) {
  return (queue->first == NULL);
}
*/
static void _mi_page_queue_remove(mi_page_queue_t* queue, mi_page_t* page) {
  if (page->prev != NULL) page->prev->next = page->next;
  if (page->next != NULL) page->next->prev = page->prev;
  if (page == queue->last)  queue->last = page->prev;
  if (page == queue->first) {
    queue->first = page->next;
    // update first
    mi_heap_t* heap = page->heap;
    mi_assert_internal(mi_heap_contains_queue(heap, queue));
    mi_heap_queue_first_update(heap,queue);
  }
  page->heap->page_count--;
}

static void _mi_page_clear(mi_page_t* page) {
  page->next = NULL;
  page->prev = NULL;
  page->heap = NULL;
  page->flags.in_full = false;
}

static void mi_page_queue_remove_clear(mi_page_queue_t* queue, mi_page_t* page) {
  mi_assert_internal(page != NULL);
  mi_assert_expensive(mi_page_queue_contains(queue, page));
  mi_assert_internal(page->block_size == queue->block_size || (page->block_size > MI_LARGE_SIZE_MAX && mi_page_queue_is_huge(queue))  || (page->flags.in_full && mi_page_queue_is_full(queue)));
  _mi_page_queue_remove(queue, page);
  _mi_page_clear(page);
}


static void mi_page_queue_push(mi_heap_t* heap, mi_page_queue_t* queue, mi_page_t* page) {
  mi_assert_internal(page->heap == NULL);
  mi_assert_internal(!mi_page_queue_contains(queue, page));
  mi_assert_internal(page->block_size == queue->block_size || (page->block_size > MI_LARGE_SIZE_MAX && mi_page_queue_is_huge(queue)) || (page->flags.in_full && mi_page_queue_is_full(queue)));

  page->flags.in_full = mi_page_queue_is_full(queue);
  page->heap = heap;
  page->next = queue->first;
  page->prev = NULL;
  if (queue->first != NULL) {
    mi_assert_internal(queue->first->prev == NULL);
    queue->first->prev = page;
    queue->first = page;
  }
  else {
    queue->first = queue->last = page;
  }

  // update direct
  mi_heap_queue_first_update(heap, queue);
  heap->page_count++;
}


static void mi_page_queue_enqueue_from(mi_page_queue_t* to, mi_page_queue_t* from, mi_page_t* page) {
  mi_assert_internal(page != NULL);
  mi_assert_expensive(mi_page_queue_contains(from, page));
  mi_assert_expensive(!mi_page_queue_contains(to, page));
  mi_assert_internal(page->block_size == to->block_size ||
                     (page->block_size > MI_LARGE_SIZE_MAX && (mi_page_queue_is_huge(to) || mi_page_queue_is_full(to))) ||
                      (page->block_size == from->block_size && mi_page_queue_is_full(to)));

  _mi_page_queue_remove(from, page);
  
  page->prev = to->last;
  page->next = NULL;
  if (to->last != NULL) {
    mi_assert_internal(page->heap == to->last->heap);
    to->last->next = page;
    to->last = page;
  }
  else {
    to->first = page;
    to->last = page;
    mi_heap_queue_first_update(page->heap, to);
  }
  page->flags.in_full = mi_page_queue_is_full(to);
  page->heap->page_count++;
}

size_t _mi_page_queue_append(mi_heap_t* heap, mi_page_queue_t* pq, mi_page_queue_t* append) {
  mi_assert_internal(mi_heap_contains_queue(heap,pq));
  mi_assert_internal(pq->block_size == append->block_size);

  if (append->first==NULL) return 0;

  // set append pages to new heap and count
  size_t count = 0;
  for (mi_page_t* page = append->first; page != NULL; page = page->next) {
    page->heap = heap;
    count++;
  }

  if (pq->last==NULL) {
    // take over afresh
    mi_assert_internal(pq->first==NULL);
    pq->first = append->first;
    pq->last = append->last;
    mi_heap_queue_first_update(heap, pq);
  }
  else {
    // append to end
    mi_assert_internal(pq->last!=NULL);
    mi_assert_internal(append->first!=NULL);
    pq->last->next = append->first;
    append->first->prev = pq->last;
    pq->last = append->last;
  }
  return count;
}
