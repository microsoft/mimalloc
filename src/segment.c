/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-atomic.h"

#include <string.h>  // memset
#include <stdio.h>

#define MI_PAGE_HUGE_ALIGN  (256*1024)

static uint8_t* mi_segment_raw_page_start(const mi_segment_t* segment, const mi_page_t* page, size_t* page_size);

/* -----------------------------------------------------------
  Segment allocation
  We allocate pages inside big OS allocated "segments"
  (4mb on 64-bit). This is to avoid splitting VMA's on Linux
  and reduce fragmentation on other OS's. Each thread
  owns its own segments.

  Currently we have:
  - small pages (64kb), 64 in one segment
  - medium pages (512kb), 8 in one segment
  - large pages (4mb), 1 in one segment
  - huge blocks > MI_LARGE_OBJ_SIZE_MAX (512kb) are directly allocated by the OS

  In any case the memory for a segment is virtual and only
  committed on demand (i.e. we are careful to not touch the memory
  until we actually allocate a block there)

  If a  thread ends, it "abandons" pages with used blocks
  and there is an abandoned segment list whose segments can
  be reclaimed by still running threads, much like work-stealing.
----------------------------------------------------------- */


/* -----------------------------------------------------------
  Queue of segments containing free pages
----------------------------------------------------------- */

#if (MI_DEBUG>=3)
static bool mi_segment_queue_contains(const mi_segment_queue_t* queue, const mi_segment_t* segment) {
  mi_assert_internal(segment != NULL);
  mi_segment_t* list = queue->first;
  while (list != NULL) {
    if (list == segment) break;
    mi_assert_internal(list->next==NULL || list->next->prev == list);
    mi_assert_internal(list->prev==NULL || list->prev->next == list);
    list = list->next;
  }
  return (list == segment);
}
#endif

static bool mi_segment_queue_is_empty(const mi_segment_queue_t* queue) {
  return (queue->first == NULL);
}

static void mi_segment_queue_remove(mi_segment_queue_t* queue, mi_segment_t* segment) {
  mi_assert_expensive(mi_segment_queue_contains(queue, segment));
  if (segment->prev != NULL) segment->prev->next = segment->next;
  if (segment->next != NULL) segment->next->prev = segment->prev;
  if (segment == queue->first) queue->first = segment->next;
  if (segment == queue->last)  queue->last = segment->prev;
  segment->next = NULL;
  segment->prev = NULL;
}

static void mi_segment_enqueue(mi_segment_queue_t* queue, mi_segment_t* segment) {
  mi_assert_expensive(!mi_segment_queue_contains(queue, segment));
  segment->next = NULL;
  segment->prev = queue->last;
  if (queue->last != NULL) {
    mi_assert_internal(queue->last->next == NULL);
    queue->last->next = segment;
    queue->last = segment;
  }
  else {
    queue->last = queue->first = segment;
  }
}

static mi_segment_queue_t* mi_segment_free_queue_of_kind(mi_page_kind_t kind, mi_segments_tld_t* tld) {
  if (kind == MI_PAGE_SMALL) return &tld->small_free;
  else if (kind == MI_PAGE_MEDIUM) return &tld->medium_free;
  else return NULL;
}

static mi_segment_queue_t* mi_segment_free_queue(const mi_segment_t* segment, mi_segments_tld_t* tld) {
  return mi_segment_free_queue_of_kind(segment->page_kind, tld);
}

// remove from free queue if it is in one
static void mi_segment_remove_from_free_queue(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_segment_queue_t* queue = mi_segment_free_queue(segment, tld); // may be NULL
  bool in_queue = (queue!=NULL && (segment->next != NULL || segment->prev != NULL || queue->first == segment));
  if (in_queue) {
    mi_segment_queue_remove(queue, segment);
  }
}

static void mi_segment_insert_in_free_queue(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_segment_enqueue(mi_segment_free_queue(segment, tld), segment);
}


/* -----------------------------------------------------------
 Invariant checking
----------------------------------------------------------- */

#if (MI_DEBUG>=2)
static bool mi_segment_is_in_free_queue(const mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_segment_queue_t* queue = mi_segment_free_queue(segment, tld);
  bool in_queue = (queue!=NULL && (segment->next != NULL || segment->prev != NULL || queue->first == segment));
  if (in_queue) {
    mi_assert_expensive(mi_segment_queue_contains(queue, segment));
  }
  return in_queue;
}
#endif

static size_t mi_segment_page_size(const mi_segment_t* segment) {
  if (segment->capacity > 1) {
    mi_assert_internal(segment->page_kind <= MI_PAGE_MEDIUM);
    return ((size_t)1 << segment->page_shift);
  }
  else {
    mi_assert_internal(segment->page_kind >= MI_PAGE_LARGE);
    return segment->segment_size;
  }
}


#if (MI_DEBUG>=2)
static bool mi_pages_reset_contains(const mi_page_t* page, mi_segments_tld_t* tld) {
  mi_page_t* p = tld->pages_reset.first;
  while (p != NULL) {
    if (p == page) return true;
    p = p->next;
  }
  return false;
}
#endif

#if (MI_DEBUG>=3)
static bool mi_segment_is_valid(const mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(segment != NULL);
  mi_assert_internal(_mi_ptr_cookie(segment) == segment->cookie);
  mi_assert_internal(segment->used <= segment->capacity);
  mi_assert_internal(segment->abandoned <= segment->used);
  size_t nfree = 0;
  for (size_t i = 0; i < segment->capacity; i++) {
    const mi_page_t* const page = &segment->pages[i];
    if (!page->segment_in_use) {
      nfree++;      
    }
    if (page->segment_in_use || page->is_reset) {
      mi_assert_expensive(!mi_pages_reset_contains(page, tld));
    }
  }
  mi_assert_internal(nfree + segment->used == segment->capacity);
  mi_assert_internal(segment->thread_id == _mi_thread_id() || (segment->thread_id==0)); // or 0
  mi_assert_internal(segment->page_kind == MI_PAGE_HUGE ||
                     (mi_segment_page_size(segment) * segment->capacity == segment->segment_size));
  return true;
}
#endif

static bool mi_page_not_in_queue(const mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert_internal(page != NULL);
  if (page->next != NULL || page->prev != NULL) {
    mi_assert_internal(mi_pages_reset_contains(page, tld));
    return false;
  }
  else {
    // both next and prev are NULL, check for singleton list
    return (tld->pages_reset.first != page && tld->pages_reset.last != page);
  }
}


/* -----------------------------------------------------------
  Guard pages
----------------------------------------------------------- */

static void mi_segment_protect_range(void* p, size_t size, bool protect) {
  if (protect) {
    _mi_mem_protect(p, size);
  }
  else {
    _mi_mem_unprotect(p, size);
  }
}

static void mi_segment_protect(mi_segment_t* segment, bool protect, mi_os_tld_t* tld) {
  // add/remove guard pages
  if (MI_SECURE != 0) {
    // in secure mode, we set up a protected page in between the segment info and the page data
    const size_t os_page_size = _mi_os_page_size();
    mi_assert_internal((segment->segment_info_size - os_page_size) >= (sizeof(mi_segment_t) + ((segment->capacity - 1) * sizeof(mi_page_t))));
    mi_assert_internal(((uintptr_t)segment + segment->segment_info_size) % os_page_size == 0);
    mi_segment_protect_range((uint8_t*)segment + segment->segment_info_size - os_page_size, os_page_size, protect);
    if (MI_SECURE <= 1 || segment->capacity == 1) {
      // and protect the last (or only) page too
      mi_assert_internal(segment->page_kind >= MI_PAGE_LARGE);
      uint8_t* start = (uint8_t*)segment + segment->segment_size - os_page_size;
      if (protect && !mi_option_is_enabled(mi_option_eager_page_commit)) {
        // ensure secure page is committed
        _mi_mem_commit(start, os_page_size, NULL, tld);
      }
      mi_segment_protect_range(start, os_page_size, protect);
    }
    else {
      // or protect every page
      const size_t page_size = mi_segment_page_size(segment);
      for (size_t i = 0; i < segment->capacity; i++) {
        if (segment->pages[i].is_committed) {
          mi_segment_protect_range((uint8_t*)segment + (i+1)*page_size - os_page_size, os_page_size, protect);
        }
      }
    }
  }
}

/* -----------------------------------------------------------
  Page reset
----------------------------------------------------------- */

static void mi_page_reset(mi_segment_t* segment, mi_page_t* page, size_t size, mi_segments_tld_t* tld) {
  if (!mi_option_is_enabled(mi_option_page_reset)) return;
  if (segment->mem_is_fixed || page->segment_in_use || page->is_reset) return;
  size_t psize;
  void* start = mi_segment_raw_page_start(segment, page, &psize);
  page->is_reset = true;
  mi_assert_internal(size <= psize);
  size_t reset_size = (size == 0 || size > psize ? psize : size);
  if (size == 0 && segment->page_kind >= MI_PAGE_LARGE && !mi_option_is_enabled(mi_option_eager_page_commit)) {
    mi_assert_internal(page->xblock_size > 0);
    reset_size = page->capacity * mi_page_block_size(page);
  }
  _mi_mem_reset(start, reset_size, tld->os);
}

static void mi_page_unreset(mi_segment_t* segment, mi_page_t* page, size_t size, mi_segments_tld_t* tld)
{
  mi_assert_internal(page->is_reset);
  mi_assert_internal(!segment->mem_is_fixed);
  page->is_reset = false;
  size_t psize;
  uint8_t* start = mi_segment_raw_page_start(segment, page, &psize);
  size_t unreset_size = (size == 0 || size > psize ? psize : size);
  if (size == 0 && segment->page_kind >= MI_PAGE_LARGE && !mi_option_is_enabled(mi_option_eager_page_commit)) {
    mi_assert_internal(page->xblock_size > 0);
    unreset_size = page->capacity * mi_page_block_size(page);
  }
  bool is_zero = false;
  _mi_mem_unreset(start, unreset_size, &is_zero, tld->os);
  if (is_zero) page->is_zero_init = true;
}


/* -----------------------------------------------------------
  The free page queue
----------------------------------------------------------- */

// we re-use the `used` field for the expiration counter. Since this is a
// a 32-bit field while the clock is always 64-bit we need to guard
// against overflow, we use substraction to check for expiry which work
// as long as the reset delay is under (2^30 - 1) milliseconds (~12 days)
static void mi_page_reset_set_expire(mi_page_t* page) {
  uint32_t expire = (uint32_t)_mi_clock_now() + mi_option_get(mi_option_reset_delay);
  page->used = expire;
}

static bool mi_page_reset_is_expired(mi_page_t* page, mi_msecs_t now) {
  int32_t expire = (int32_t)(page->used);
  return (((int32_t)now - expire) >= 0);
}

static void mi_pages_reset_add(mi_segment_t* segment, mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert_internal(!page->segment_in_use);
  mi_assert_internal(mi_page_not_in_queue(page,tld));
  mi_assert_expensive(!mi_pages_reset_contains(page, tld));
  mi_assert_internal(_mi_page_segment(page)==segment);
  if (!mi_option_is_enabled(mi_option_page_reset)) return;
  if (segment->mem_is_fixed || page->segment_in_use || page->is_reset) return;

  if (mi_option_get(mi_option_reset_delay) == 0) {
    // reset immediately?
    mi_page_reset(segment, page, 0, tld);
  }
  else {
    // otherwise push on the delayed page reset queue
    mi_page_queue_t* pq = &tld->pages_reset;
    // push on top 
    mi_page_reset_set_expire(page);
    page->next = pq->first;
    page->prev = NULL;
    if (pq->first == NULL) {
      mi_assert_internal(pq->last == NULL);
      pq->first = pq->last = page;
    }
    else {
      pq->first->prev = page;
      pq->first = page;
    }
  }
}

static void mi_pages_reset_remove(mi_page_t* page, mi_segments_tld_t* tld) {
  if (mi_page_not_in_queue(page,tld)) return;

  mi_page_queue_t* pq = &tld->pages_reset;
  mi_assert_internal(pq!=NULL);
  mi_assert_internal(!page->segment_in_use);
  mi_assert_internal(mi_pages_reset_contains(page, tld));  
  if (page->prev != NULL) page->prev->next = page->next;
  if (page->next != NULL) page->next->prev = page->prev;
  if (page == pq->last)  pq->last = page->prev;
  if (page == pq->first) pq->first = page->next;
  page->next = page->prev = NULL;
  page->used = 0;
}

static void mi_pages_reset_remove_all_in_segment(mi_segment_t* segment, bool force_reset, mi_segments_tld_t* tld) {
  if (segment->mem_is_fixed) return; // never reset in huge OS pages
  for (size_t i = 0; i < segment->capacity; i++) {
    mi_page_t* page = &segment->pages[i];
    if (!page->segment_in_use && !page->is_reset) {
      mi_pages_reset_remove(page, tld);
      if (force_reset) {
        mi_page_reset(segment, page, 0, tld); 
      }
    }
    else {
      mi_assert_internal(mi_page_not_in_queue(page,tld));
    }    
  }
}

static void mi_reset_delayed(mi_segments_tld_t* tld) {
  if (!mi_option_is_enabled(mi_option_page_reset)) return;
  mi_msecs_t now = _mi_clock_now();
  mi_page_queue_t* pq = &tld->pages_reset;  
  // from oldest up to the first that has not expired yet
  mi_page_t* page = pq->last;
  while (page != NULL && mi_page_reset_is_expired(page,now)) {
    mi_page_t* const prev = page->prev; // save previous field
    mi_page_reset(_mi_page_segment(page), page, 0, tld);
    page->used = 0;
    page->prev = page->next = NULL;
    page = prev;
  }
  // discard the reset pages from the queue
  pq->last = page;
  if (page != NULL){
    page->next = NULL;
  } 
  else {
    pq->first = NULL;
  }
}





/* -----------------------------------------------------------
 Segment size calculations
----------------------------------------------------------- */

// Raw start of the page available memory; can be used on uninitialized pages (only `segment_idx` must be set)
// The raw start is not taking aligned block allocation into consideration.
static uint8_t* mi_segment_raw_page_start(const mi_segment_t* segment, const mi_page_t* page, size_t* page_size) {
  size_t   psize = (segment->page_kind == MI_PAGE_HUGE ? segment->segment_size : (size_t)1 << segment->page_shift);
  uint8_t* p = (uint8_t*)segment + page->segment_idx * psize;

  if (page->segment_idx == 0) {
    // the first page starts after the segment info (and possible guard page)
    p += segment->segment_info_size;
    psize -= segment->segment_info_size;
  }

  if (MI_SECURE > 1 || (MI_SECURE == 1 && page->segment_idx == segment->capacity - 1)) {
    // secure == 1: the last page has an os guard page at the end
    // secure >  1: every page has an os guard page
    psize -= _mi_os_page_size();
  }

  if (page_size != NULL) *page_size = psize;
  mi_assert_internal(page->xblock_size == 0 || _mi_ptr_page(p) == page);
  mi_assert_internal(_mi_ptr_segment(p) == segment);
  return p;
}

// Start of the page available memory; can be used on uninitialized pages (only `segment_idx` must be set)
uint8_t* _mi_segment_page_start(const mi_segment_t* segment, const mi_page_t* page, size_t block_size, size_t* page_size, size_t* pre_size)
{
  size_t   psize;
  uint8_t* p = mi_segment_raw_page_start(segment, page, &psize);
  if (pre_size != NULL) *pre_size = 0;
  if (page->segment_idx == 0 && block_size > 0 && segment->page_kind <= MI_PAGE_MEDIUM) {
    // for small and medium objects, ensure the page start is aligned with the block size (PR#66 by kickunderscore)
    size_t adjust = block_size - ((uintptr_t)p % block_size);
    if (adjust < block_size) {
      p += adjust;
      psize -= adjust;
      if (pre_size != NULL) *pre_size = adjust;
    }
    mi_assert_internal((uintptr_t)p % block_size == 0);
  }

  if (page_size != NULL) *page_size = psize;
  mi_assert_internal(page->xblock_size==0 || _mi_ptr_page(p) == page);
  mi_assert_internal(_mi_ptr_segment(p) == segment);
  return p;
}

static size_t mi_segment_size(size_t capacity, size_t required, size_t* pre_size, size_t* info_size)
{
  const size_t minsize   = sizeof(mi_segment_t) + ((capacity - 1) * sizeof(mi_page_t)) + 16 /* padding */;
  size_t guardsize = 0;
  size_t isize     = 0;

  if (MI_SECURE == 0) {
    // normally no guard pages
    isize = _mi_align_up(minsize, 16 * MI_MAX_ALIGN_SIZE);
  }
  else {
    // in secure mode, we set up a protected page in between the segment info
    // and the page data (and one at the end of the segment)
    const size_t page_size = _mi_os_page_size();
    isize = _mi_align_up(minsize, page_size);
    guardsize = page_size;
    required = _mi_align_up(required, page_size);
  }
;
  if (info_size != NULL) *info_size = isize;
  if (pre_size != NULL)  *pre_size  = isize + guardsize;
  return (required==0 ? MI_SEGMENT_SIZE : _mi_align_up( required + isize + 2*guardsize, MI_PAGE_HUGE_ALIGN) );
}


/* ----------------------------------------------------------------------------
Segment caches
We keep a small segment cache per thread to increase local
reuse and avoid setting/clearing guard pages in secure mode.
------------------------------------------------------------------------------- */

static void mi_segments_track_size(long segment_size, mi_segments_tld_t* tld) {
  if (segment_size>=0) _mi_stat_increase(&tld->stats->segments,1);
                  else _mi_stat_decrease(&tld->stats->segments,1);
  tld->count += (segment_size >= 0 ? 1 : -1);
  if (tld->count > tld->peak_count) tld->peak_count = tld->count;
  tld->current_size += segment_size;
  if (tld->current_size > tld->peak_size) tld->peak_size = tld->current_size;
}


static void mi_segment_os_free(mi_segment_t* segment, size_t segment_size, mi_segments_tld_t* tld) {
  segment->thread_id = 0;
  mi_segments_track_size(-((long)segment_size),tld);
  if (MI_SECURE != 0) {
    mi_assert_internal(!segment->mem_is_fixed);
    mi_segment_protect(segment, false, tld->os); // ensure no more guard pages are set
  }

  bool any_reset = false;
  bool fully_committed = true;
  for (size_t i = 0; i < segment->capacity; i++) {
    mi_page_t* page = &segment->pages[i];
    if (!page->is_committed) { fully_committed = false; }
    if (page->is_reset)      { any_reset = true; }
  }
  if (any_reset && mi_option_is_enabled(mi_option_reset_decommits)) {
    fully_committed = false;
  }
  if (segment->page_kind >= MI_PAGE_LARGE && !mi_option_is_enabled(mi_option_eager_page_commit)) {
    fully_committed = false;
  }

  _mi_mem_free(segment, segment_size, segment->memid, fully_committed, any_reset, tld->os);
}


// The thread local segment cache is limited to be at most 1/8 of the peak size of segments in use,
#define MI_SEGMENT_CACHE_FRACTION (8)

// note: returned segment may be partially reset
static mi_segment_t* mi_segment_cache_pop(size_t segment_size, mi_segments_tld_t* tld) {
  if (segment_size != 0 && segment_size != MI_SEGMENT_SIZE) return NULL;
  mi_segment_t* segment = tld->cache;
  if (segment == NULL) return NULL;
  tld->cache_count--;
  tld->cache = segment->next;
  segment->next = NULL;
  mi_assert_internal(segment->segment_size == MI_SEGMENT_SIZE);
  _mi_stat_decrease(&tld->stats->segments_cache, 1);
  return segment;
}

static bool mi_segment_cache_full(mi_segments_tld_t* tld)
{
  // if (tld->count == 1 && tld->cache_count==0) return false; // always cache at least the final segment of a thread
  size_t max_cache = mi_option_get(mi_option_segment_cache);
  if (tld->cache_count < max_cache
       && tld->cache_count < (1 + (tld->peak_count / MI_SEGMENT_CACHE_FRACTION)) // at least allow a 1 element cache
     ) {
    return false;
  }
  // take the opportunity to reduce the segment cache if it is too large (now)
  // TODO: this never happens as we check against peak usage, should we use current usage instead?
  while (tld->cache_count > max_cache) { //(1 + (tld->peak_count / MI_SEGMENT_CACHE_FRACTION))) {
    mi_segment_t* segment = mi_segment_cache_pop(0,tld);
    mi_assert_internal(segment != NULL);
    if (segment != NULL) mi_segment_os_free(segment, segment->segment_size, tld);
  }
  return true;
}

static bool mi_segment_cache_push(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(!mi_segment_is_in_free_queue(segment, tld));
  mi_assert_internal(segment->next == NULL);
  if (segment->segment_size != MI_SEGMENT_SIZE || mi_segment_cache_full(tld)) {
    return false;
  }
  mi_assert_internal(segment->segment_size == MI_SEGMENT_SIZE);
  segment->next = tld->cache;
  tld->cache = segment;
  tld->cache_count++;
  _mi_stat_increase(&tld->stats->segments_cache,1);
  return true;
}

// called by threads that are terminating to free cached segments
void _mi_segment_thread_collect(mi_segments_tld_t* tld) {
  mi_segment_t* segment;
  while ((segment = mi_segment_cache_pop(0,tld)) != NULL) {
    mi_segment_os_free(segment, segment->segment_size, tld);
  }
  mi_assert_internal(tld->cache_count == 0);
  mi_assert_internal(tld->cache == NULL);
  mi_assert_internal(tld->pages_reset.first == NULL);  
  mi_assert_internal(tld->pages_reset.last == NULL);
}


/* -----------------------------------------------------------
   Segment allocation
----------------------------------------------------------- */

// Allocate a segment from the OS aligned to `MI_SEGMENT_SIZE` .
static mi_segment_t* mi_segment_alloc(size_t required, mi_page_kind_t page_kind, size_t page_shift, mi_segments_tld_t* tld, mi_os_tld_t* os_tld)
{
  // calculate needed sizes first
  size_t capacity;
  if (page_kind == MI_PAGE_HUGE) {
    mi_assert_internal(page_shift == MI_SEGMENT_SHIFT && required > 0);
    capacity = 1;
  }
  else {
    mi_assert_internal(required == 0);
    size_t page_size = (size_t)1 << page_shift;
    capacity = MI_SEGMENT_SIZE / page_size;
    mi_assert_internal(MI_SEGMENT_SIZE % page_size == 0);
    mi_assert_internal(capacity >= 1 && capacity <= MI_SMALL_PAGES_PER_SEGMENT);
  }
  size_t info_size;
  size_t pre_size;
  size_t segment_size = mi_segment_size(capacity, required, &pre_size, &info_size);
  mi_assert_internal(segment_size >= required);

  // Initialize parameters
  const bool eager_delayed = (page_kind <= MI_PAGE_MEDIUM && tld->count < (size_t)mi_option_get(mi_option_eager_commit_delay));
  const bool eager  = !eager_delayed && mi_option_is_enabled(mi_option_eager_commit);
  bool commit = eager; // || (page_kind >= MI_PAGE_LARGE);
  bool pages_still_good = false;
  bool is_zero = false;

  // Try to get it from our thread local cache first
  mi_segment_t* segment = mi_segment_cache_pop(segment_size, tld);
  if (segment != NULL) {
    if (page_kind <= MI_PAGE_MEDIUM && segment->page_kind == page_kind && segment->segment_size == segment_size) {
      pages_still_good = true;
    }
    else
    {
      if (MI_SECURE!=0) {
        mi_assert_internal(!segment->mem_is_fixed);
        mi_segment_protect(segment, false, tld->os); // reset protection if the page kind differs
      }
      // different page kinds; unreset any reset pages, and unprotect
      // TODO: optimize cache pop to return fitting pages if possible?
      for (size_t i = 0; i < segment->capacity; i++) {
        mi_page_t* page = &segment->pages[i];
        if (page->is_reset) {
          if (!commit && mi_option_is_enabled(mi_option_reset_decommits)) {
            page->is_reset = false;
          }
          else {
            mi_page_unreset(segment, page, 0, tld);  // todo: only unreset the part that was reset? (instead of the full page)
          }
        }
      }
      // ensure the initial info is committed
      if (segment->capacity < capacity) {
        bool commit_zero = false;
        _mi_mem_commit(segment, pre_size, &commit_zero, tld->os);
        if (commit_zero) is_zero = true;
      }
    }
  }
  else {
    // Allocate the segment from the OS
    size_t memid;
    bool   mem_large = (!eager_delayed && (MI_SECURE==0)); // only allow large OS pages once we are no longer lazy
    segment = (mi_segment_t*)_mi_mem_alloc_aligned(segment_size, MI_SEGMENT_SIZE, &commit, &mem_large, &is_zero, &memid, os_tld);
    if (segment == NULL) return NULL;  // failed to allocate
    if (!commit) {
      // ensure the initial info is committed
      bool commit_zero = false;
      _mi_mem_commit(segment, pre_size, &commit_zero, tld->os);
      if (commit_zero) is_zero = true;
    }
    segment->memid = memid;
    segment->mem_is_fixed = mem_large;
    segment->mem_is_committed = commit;
    mi_segments_track_size((long)segment_size, tld);
  }
  mi_assert_internal(segment != NULL && (uintptr_t)segment % MI_SEGMENT_SIZE == 0);

  if (!pages_still_good) {
    // zero the segment info (but not the `mem` fields)
    ptrdiff_t ofs = offsetof(mi_segment_t, next);
    memset((uint8_t*)segment + ofs, 0, info_size - ofs);

    // initialize pages info
    for (uint8_t i = 0; i < capacity; i++) {
      segment->pages[i].segment_idx = i;
      segment->pages[i].is_reset = false;
      segment->pages[i].is_committed = commit;
      segment->pages[i].is_zero_init = is_zero;
    }
  }
  else {
    // zero the segment info but not the pages info (and mem fields)
    ptrdiff_t ofs = offsetof(mi_segment_t, next);
    memset((uint8_t*)segment + ofs, 0, offsetof(mi_segment_t,pages) - ofs);
  }

  // initialize
  segment->page_kind  = page_kind;
  segment->capacity   = capacity;
  segment->page_shift = page_shift;
  segment->segment_size = segment_size;
  segment->segment_info_size = pre_size;
  segment->thread_id  = _mi_thread_id();
  segment->cookie = _mi_ptr_cookie(segment);
  // _mi_stat_increase(&tld->stats->page_committed, segment->segment_info_size);

  // set protection
  mi_segment_protect(segment, true, tld->os);

  //fprintf(stderr,"mimalloc: alloc segment at %p\n", (void*)segment);
  return segment;
}


static void mi_segment_free(mi_segment_t* segment, bool force, mi_segments_tld_t* tld) {
  UNUSED(force);
  mi_assert(segment != NULL);  
  // note: don't reset pages even on abandon as the whole segment is freed? (and ready for reuse)
  bool force_reset = (force && mi_option_is_enabled(mi_option_abandoned_page_reset));
  mi_pages_reset_remove_all_in_segment(segment, force_reset, tld);
  mi_segment_remove_from_free_queue(segment,tld);

  mi_assert_expensive(!mi_segment_queue_contains(&tld->small_free, segment));
  mi_assert_expensive(!mi_segment_queue_contains(&tld->medium_free, segment));
  mi_assert(segment->next == NULL);
  mi_assert(segment->prev == NULL);
  _mi_stat_decrease(&tld->stats->page_committed, segment->segment_info_size);

  if (!force && mi_segment_cache_push(segment, tld)) {
    // it is put in our cache
  }
  else {
    // otherwise return it to the OS
    mi_segment_os_free(segment, segment->segment_size, tld);
  }
}

/* -----------------------------------------------------------
  Free page management inside a segment
----------------------------------------------------------- */


static bool mi_segment_has_free(const mi_segment_t* segment) {
  return (segment->used < segment->capacity);
}

static void mi_segment_page_claim(mi_segment_t* segment, mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert_internal(_mi_page_segment(page) == segment);
  mi_assert_internal(!page->segment_in_use);    
  // set in-use before doing unreset to prevent delayed reset
  mi_pages_reset_remove(page, tld);
  page->segment_in_use = true;
  segment->used++;
  if (!page->is_committed) {
    mi_assert_internal(!segment->mem_is_fixed);
    mi_assert_internal(!page->is_reset);
    page->is_committed = true;
    if (segment->page_kind < MI_PAGE_LARGE
      || !mi_option_is_enabled(mi_option_eager_page_commit)) {
      size_t psize;
      uint8_t* start = mi_segment_raw_page_start(segment, page, &psize);
      bool is_zero = false;
      const size_t gsize = (MI_SECURE >= 2 ? _mi_os_page_size() : 0);
      _mi_mem_commit(start, psize + gsize, &is_zero, tld->os);
      if (gsize > 0) { mi_segment_protect_range(start + psize, gsize, true); }
      if (is_zero) { page->is_zero_init = true; }
    }
  }
  if (page->is_reset) {
    mi_page_unreset(segment, page, 0, tld); // todo: only unreset the part that was reset?
  }
  mi_assert_internal(page->segment_in_use);
  mi_assert_internal(segment->used <= segment->capacity);
  if (segment->used == segment->capacity && segment->page_kind <= MI_PAGE_MEDIUM) {
    // if no more free pages, remove from the queue
    mi_assert_internal(!mi_segment_has_free(segment));
    mi_segment_remove_from_free_queue(segment, tld);
  }
}


/* -----------------------------------------------------------
   Free
----------------------------------------------------------- */

static void mi_segment_abandon(mi_segment_t* segment, mi_segments_tld_t* tld);

static void mi_segment_page_clear(mi_segment_t* segment, mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert_internal(page->segment_in_use);
  mi_assert_internal(mi_page_all_free(page));
  mi_assert_internal(page->is_committed);
  mi_assert_internal(mi_page_not_in_queue(page, tld));

  size_t inuse = page->capacity * mi_page_block_size(page);
  _mi_stat_decrease(&tld->stats->page_committed, inuse);
  _mi_stat_decrease(&tld->stats->pages, 1);

  // calculate the used size from the raw (non-aligned) start of the page
  //size_t pre_size;
  //_mi_segment_page_start(segment, page, page->block_size, NULL, &pre_size);
  //size_t used_size = pre_size + (page->capacity * page->block_size);

  page->is_zero_init = false;
  page->segment_in_use = false;

  // reset the page memory to reduce memory pressure?
  // note: must come after setting `segment_in_use` to false but before block_size becomes 0
  //mi_page_reset(segment, page, 0 /*used_size*/, tld);

  // zero the page data, but not the segment fields and block_size (for page size calculations)
  uint32_t block_size = page->xblock_size;
  ptrdiff_t ofs = offsetof(mi_page_t,capacity);
  memset((uint8_t*)page + ofs, 0, sizeof(*page) - ofs);
  page->xblock_size = block_size;
  segment->used--;

  // add to the free page list for reuse/reset
  if (segment->page_kind <= MI_PAGE_MEDIUM) {
    mi_pages_reset_add(segment, page, tld);
  }
}

void _mi_segment_page_free(mi_page_t* page, bool force, mi_segments_tld_t* tld)
{
  mi_assert(page != NULL);
  mi_segment_t* segment = _mi_page_segment(page);
  mi_assert_expensive(mi_segment_is_valid(segment,tld));
  mi_reset_delayed(tld);

  // mark it as free now
  mi_segment_page_clear(segment, page, tld);

  if (segment->used == 0) {
    // no more used pages; remove from the free list and free the segment
    mi_segment_free(segment, force, tld);
  }
  else {
    if (segment->used == segment->abandoned) {
      // only abandoned pages; remove from free list and abandon
      mi_segment_abandon(segment,tld);
    }
    else if (segment->used + 1 == segment->capacity) {
      mi_assert_internal(segment->page_kind <= MI_PAGE_MEDIUM); // for now we only support small and medium pages
      // move back to segments  free list
      mi_segment_insert_in_free_queue(segment,tld);
    }
  }
}


/* -----------------------------------------------------------
   Abandonment
----------------------------------------------------------- */

// When threads terminate, they can leave segments with
// live blocks (reached through other threads). Such segments
// are "abandoned" and will be reclaimed by other threads to
// reuse their pages and/or free them eventually
static volatile _Atomic(mi_segment_t*) abandoned; // = NULL;
static volatile _Atomic(uintptr_t)     abandoned_count; // = 0; approximate count of abandoned segments

// prepend a list of abandoned segments atomically to the global abandoned list; O(n)
static void mi_segments_prepend_abandoned(mi_segment_t* first) {
  if (first == NULL) return;

  // first try if the abandoned list happens to be NULL
  if (mi_atomic_cas_ptr_weak(mi_atomic_cast(void*, &abandoned), first, NULL)) return;

  // if not, find the end of the list
  mi_segment_t* last = first;
  while (last->abandoned_next != NULL) {
    last = last->abandoned_next;
  }

  // and atomically prepend
  mi_segment_t* next;
  do {
    next = (mi_segment_t*)mi_atomic_read_ptr_relaxed(mi_atomic_cast(void*, &abandoned));
    last->abandoned_next = next;
  } while (!mi_atomic_cas_ptr_weak(mi_atomic_cast(void*, &abandoned), first, next));
}

static void mi_segment_abandon(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(segment->used == segment->abandoned);
  mi_assert_internal(segment->used > 0);
  mi_assert_internal(segment->abandoned_next == NULL);
  mi_assert_expensive(mi_segment_is_valid(segment,tld));

  // remove the segment from the free page queue if needed
  mi_reset_delayed(tld); 
  mi_pages_reset_remove_all_in_segment(segment, mi_option_is_enabled(mi_option_abandoned_page_reset), tld); 
  mi_segment_remove_from_free_queue(segment, tld);
  mi_assert_internal(segment->next == NULL && segment->prev == NULL);

  // all pages in the segment are abandoned; add it to the abandoned list
  _mi_stat_increase(&tld->stats->segments_abandoned, 1);
  mi_segments_track_size(-((long)segment->segment_size), tld);
  segment->thread_id = 0;
  segment->abandoned_next = NULL;
  mi_segments_prepend_abandoned(segment); // prepend one-element list
  mi_atomic_increment(&abandoned_count);  // keep approximate count
}

void _mi_segment_page_abandon(mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert(page != NULL);
  mi_assert_internal(mi_page_thread_free_flag(page)==MI_NEVER_DELAYED_FREE);
  mi_assert_internal(mi_page_heap(page) == NULL);
  mi_segment_t* segment = _mi_page_segment(page);
  mi_assert_expensive(!mi_pages_reset_contains(page, tld));
  mi_assert_expensive(mi_segment_is_valid(segment,tld));
  segment->abandoned++;
  _mi_stat_increase(&tld->stats->pages_abandoned, 1);
  mi_assert_internal(segment->abandoned <= segment->used);
  if (segment->used == segment->abandoned) {
    // all pages are abandoned, abandon the entire segment
    mi_segment_abandon(segment,tld);
  }
}

bool _mi_segment_try_reclaim_abandoned( mi_heap_t* heap, bool try_all, mi_segments_tld_t* tld) {
  // To avoid the A-B-A problem, grab the entire list atomically
  mi_segment_t* segment = (mi_segment_t*)mi_atomic_read_ptr_relaxed(mi_atomic_cast(void*, &abandoned));  // pre-read to avoid expensive atomic operations
  if (segment == NULL) return false;
  segment = (mi_segment_t*)mi_atomic_exchange_ptr(mi_atomic_cast(void*, &abandoned), NULL);
  if (segment == NULL) return false;

  // we got a non-empty list
  if (!try_all) {
    // take at most 1/8th of the list and append the rest back to the abandoned list again
    // this is O(n) but simplifies the code a lot (as we don't have an A-B-A problem)
    // and probably ok since the length will tend to be not too large.
    uintptr_t atmost = mi_atomic_read(&abandoned_count)/8;  // at most 1/8th of all outstanding (estimated)
    if (atmost < 8) atmost = 8;    // but at least 8

    // find the split point
    mi_segment_t* last = segment;
    while (last->abandoned_next != NULL && atmost > 0) {
      last = last->abandoned_next;  
      atmost--;
    }
    // split the list and push back the remaining segments
    mi_segment_t* next = last->abandoned_next;
    last->abandoned_next = NULL;
    mi_segments_prepend_abandoned(next);
  }

  // reclaim all segments that we kept
  while(segment != NULL) {
    mi_segment_t* const next = segment->abandoned_next; // save the next segment

    // got it.
    mi_atomic_decrement(&abandoned_count);
    segment->thread_id = _mi_thread_id();
    segment->abandoned_next = NULL;
    mi_segments_track_size((long)segment->segment_size,tld);
    mi_assert_internal(segment->next == NULL && segment->prev == NULL);
    mi_assert_expensive(mi_segment_is_valid(segment,tld));
    _mi_stat_decrease(&tld->stats->segments_abandoned,1);

    // add its abandoned pages to the current thread
    mi_assert(segment->abandoned == segment->used);
    for (size_t i = 0; i < segment->capacity; i++) {
      mi_page_t* page = &segment->pages[i];
      if (page->segment_in_use) {
        mi_assert_internal(!page->is_reset);
        mi_assert_internal(page->is_committed);
        mi_assert_internal(mi_page_not_in_queue(page, tld));
        mi_assert_internal(mi_page_thread_free_flag(page)==MI_NEVER_DELAYED_FREE);
        mi_assert_internal(mi_page_heap(page) == NULL);
        segment->abandoned--;
        mi_assert(page->next == NULL);
        _mi_stat_decrease(&tld->stats->pages_abandoned, 1);
        // set the heap again and allow delayed free again
        mi_page_set_heap(page, heap);
        _mi_page_use_delayed_free(page, MI_USE_DELAYED_FREE, true); // override never (after heap is set)
        _mi_page_free_collect(page, false); // ensure used count is up to date
        if (mi_page_all_free(page)) {
          // if everything free already, clear the page directly
          mi_segment_page_clear(segment,page,tld);
        }
        else {
          // otherwise reclaim it into the heap
          _mi_page_reclaim(heap,page);
        }
      }
    }
    mi_assert(segment->abandoned == 0);
    if (segment->used == 0) {  // due to page_clear
      mi_segment_free(segment,false,tld);
    }
    else {
      // add its free pages to the the current thread free small segment queue
      if (segment->page_kind <= MI_PAGE_MEDIUM && mi_segment_has_free(segment)) {
        mi_segment_insert_in_free_queue(segment,tld);
      }
    }

    // go on
    segment = next; 
  }

  return true;
}


/* -----------------------------------------------------------
   Small page allocation
----------------------------------------------------------- */


static mi_page_t* mi_segment_find_free(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(mi_segment_has_free(segment));
  mi_assert_expensive(mi_segment_is_valid(segment, tld));
  for (size_t i = 0; i < segment->capacity; i++) {  // TODO: use a bitmap instead of search?
    mi_page_t* page = &segment->pages[i];
    if (!page->segment_in_use) {
      mi_segment_page_claim(segment, page, tld);
      return page;
    }
  }
  mi_assert(false);
  return NULL;
}

// Allocate a page inside a segment. Requires that the page has free pages
static mi_page_t* mi_segment_page_alloc_in(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(mi_segment_has_free(segment));
  return mi_segment_find_free(segment, tld);  
}

static mi_page_t* mi_segment_page_alloc(mi_page_kind_t kind, size_t page_shift, mi_segments_tld_t* tld, mi_os_tld_t* os_tld) {  
  // find an available segment the segment free queue 
  mi_segment_queue_t* const free_queue = mi_segment_free_queue_of_kind(kind, tld);
  if (mi_segment_queue_is_empty(free_queue)) {
    // possibly allocate a fresh segment
    mi_segment_t* segment = mi_segment_alloc(0, kind, page_shift, tld, os_tld);
    if (segment == NULL) return NULL;  // return NULL if out-of-memory
    mi_segment_enqueue(free_queue, segment);
  }
  mi_assert_internal(free_queue->first != NULL);
  mi_page_t* const page = mi_segment_page_alloc_in(free_queue->first, tld);
  mi_assert_internal(page != NULL);
#if MI_DEBUG>=2
  // verify it is committed
  _mi_segment_page_start(_mi_page_segment(page), page, sizeof(void*), NULL, NULL)[0] = 0;
#endif
  return page;
}

static mi_page_t* mi_segment_small_page_alloc(mi_segments_tld_t* tld, mi_os_tld_t* os_tld) {
  return mi_segment_page_alloc(MI_PAGE_SMALL,MI_SMALL_PAGE_SHIFT,tld,os_tld);
}

static mi_page_t* mi_segment_medium_page_alloc(mi_segments_tld_t* tld, mi_os_tld_t* os_tld) {
  return mi_segment_page_alloc(MI_PAGE_MEDIUM, MI_MEDIUM_PAGE_SHIFT, tld, os_tld);
}

/* -----------------------------------------------------------
   large page allocation
----------------------------------------------------------- */

static mi_page_t* mi_segment_large_page_alloc(mi_segments_tld_t* tld, mi_os_tld_t* os_tld) {
  mi_segment_t* segment = mi_segment_alloc(0,MI_PAGE_LARGE,MI_LARGE_PAGE_SHIFT,tld,os_tld);
  if (segment == NULL) return NULL;
  mi_page_t* page = mi_segment_find_free(segment, tld);
  mi_assert_internal(page != NULL);
#if MI_DEBUG>=2
  _mi_segment_page_start(segment, page, sizeof(void*), NULL, NULL)[0] = 0;
#endif
  return page;
}

static mi_page_t* mi_segment_huge_page_alloc(size_t size, mi_segments_tld_t* tld, mi_os_tld_t* os_tld)
{
  mi_segment_t* segment = mi_segment_alloc(size, MI_PAGE_HUGE, MI_SEGMENT_SHIFT,tld,os_tld);
  if (segment == NULL) return NULL;
  mi_assert_internal(mi_segment_page_size(segment) - segment->segment_info_size - (2*(MI_SECURE == 0 ? 0 : _mi_os_page_size())) >= size);
  segment->thread_id = 0; // huge pages are immediately abandoned
  mi_page_t* page = mi_segment_find_free(segment, tld);
  mi_assert_internal(page != NULL);
  return page;
}

/* -----------------------------------------------------------
   Page allocation and free
----------------------------------------------------------- */

mi_page_t* _mi_segment_page_alloc(size_t block_size, mi_segments_tld_t* tld, mi_os_tld_t* os_tld) {
  mi_page_t* page;
  if (block_size <= MI_SMALL_OBJ_SIZE_MAX) {
    page = mi_segment_small_page_alloc(tld,os_tld);
  }
  else if (block_size <= MI_MEDIUM_OBJ_SIZE_MAX) {
    page = mi_segment_medium_page_alloc(tld, os_tld);
  }
  else if (block_size <= MI_LARGE_OBJ_SIZE_MAX) {
    page = mi_segment_large_page_alloc(tld, os_tld);
  }
  else {
    page = mi_segment_huge_page_alloc(block_size,tld,os_tld);
  }
  mi_assert_expensive(page == NULL || mi_segment_is_valid(_mi_page_segment(page),tld));
  mi_assert_internal(page == NULL || (mi_segment_page_size(_mi_page_segment(page)) - (MI_SECURE == 0 ? 0 : _mi_os_page_size())) >= block_size);
  mi_reset_delayed(tld);
  mi_assert_internal(page == NULL || mi_page_not_in_queue(page, tld));
  return page;
}
