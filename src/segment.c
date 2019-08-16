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

static void mi_segment_map_allocated_at(const mi_segment_t* segment);
static void mi_segment_map_freed_at(const mi_segment_t* segment);




/* -----------------------------------------------------------
  Segment allocation
  

  In any case the memory for a segment is virtual and only
  committed on demand (i.e. we are careful to not touch the memory
  until we actually allocate a block there)

  If a  thread ends, it "abandons" pages with used blocks
  and there is an abandoned segment list whose segments can
  be reclaimed by still running threads, much like work-stealing.
----------------------------------------------------------- */

/* -----------------------------------------------------------
   Bins
----------------------------------------------------------- */
// Use bit scan forward to quickly find the first zero bit if it is available
#if defined(_MSC_VER)
#include <intrin.h>
static inline size_t mi_bsr(uintptr_t x) {
  if (x==0) return 8*MI_INTPTR_SIZE;
  DWORD idx;
  #if (MI_INTPTR_SIZE==8)
  _BitScanReverse64(&idx, x);
  #else
  _BitScanReverse(&idx, x);
  #endif
  return idx;
}
#elif defined(__GNUC__) || defined(__clang__)
static inline size_t mi_bsr(uintptr_t x) {
  return (x==0 ? 8*MI_INTPTR_SIZE : (8*MI_INTPTR_SIZE - 1) - __builtin_clzl(x));
}
#else
#error "define bsr for your platform"
#endif

static size_t mi_slice_bin8(size_t slice_count) {
  if (slice_count<=1) return slice_count;
  mi_assert_internal(slice_count <= MI_SLICES_PER_SEGMENT);
  slice_count--;
  size_t s = mi_bsr(slice_count);
  if (s <= 2) return slice_count + 1;
  size_t bin = ((s << 2) | ((slice_count >> (s - 2))&0x03)) - 4;
  return bin;
}

static size_t mi_slice_bin(size_t slice_count) {
  mi_assert_internal(slice_count*MI_SEGMENT_SLICE_SIZE <= MI_SEGMENT_SIZE);
  mi_assert_internal(mi_slice_bin8(MI_SLICES_PER_SEGMENT) == MI_SEGMENT_BIN_MAX);
  size_t bin = (slice_count==0 ? 0 : mi_slice_bin8(slice_count));
  mi_assert_internal(bin <= MI_SEGMENT_BIN_MAX);
  return bin;
}


/* -----------------------------------------------------------
   Page Queues
----------------------------------------------------------- */
/*
static bool mi_page_queue_is_empty(mi_page_queue_t* pq) {
  return (pq->first == NULL);
}

static mi_page_t* mi_page_queue_pop(mi_page_queue_t* pq)
{
  mi_page_t* page = pq->first;
  if (page==NULL) return NULL;
  mi_assert_internal(page->prev==NULL);
  pq->first = page->next;
  if (page->next == NULL) pq->last = NULL;
  else page->next->prev = NULL;
  page->next = NULL;
  page->prev = NULL;    // paranoia
  page->block_size = 1; // no more free
  return page;
}
*/

static void mi_page_queue_push(mi_page_queue_t* pq, mi_page_t* page) {
  // todo: or push to the end?
  mi_assert_internal(page->prev == NULL && page->next==NULL);
  page->prev = NULL; // paranoia
  page->next = pq->first;
  pq->first = page;
  if (page->next != NULL) page->next->prev = page;
                     else pq->last = page;
  page->block_size = 0; // free                     
}

static mi_page_queue_t* mi_page_queue_for(size_t slice_count, mi_segments_tld_t* tld) {
  size_t bin = mi_slice_bin(slice_count);
  mi_page_queue_t* pq = &tld->pages[bin];
  // mi_assert_internal(pq->block_size >= slice_count);
  return pq;
}

static void mi_page_queue_delete(mi_page_queue_t* pq, mi_page_t* page) {
  mi_assert_internal(page->block_size==0 && page->slice_count>0 && page->slice_offset==0);
  // should work too if the queue does not contain page (which can happen during reclaim)
  if (page->prev != NULL) page->prev->next = page->next;
  if (page == pq->first) pq->first = page->next;
  if (page->next != NULL) page->next->prev = page->prev;
  if (page == pq->last) pq->last = page->prev;
  page->prev = NULL;
  page->next = NULL;
  page->block_size = 1; // no more free
}


/* -----------------------------------------------------------
 Invariant checking
----------------------------------------------------------- */

#if (MI_DEBUG > 1)
static bool mi_page_queue_contains(mi_page_queue_t* pq, mi_page_t* page) {
  for (mi_page_t* p = pq->first; p != NULL; p = p->next) {
    if (p==page) return true;
  }
  return false;
}

static bool mi_segment_is_valid(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(segment != NULL);
  mi_assert_internal(_mi_ptr_cookie(segment) == segment->cookie);
  mi_assert_internal(segment->abandoned <= segment->used);
  mi_assert_internal(segment->thread_id == 0 || segment->thread_id == _mi_thread_id());
  //mi_assert_internal(segment->segment_info_size % MI_SEGMENT_SLICE_SIZE == 0);
  mi_slice_t* slice = &segment->slices[0];
  size_t used_count = 0;
  mi_page_queue_t* pq;
  while(slice < &segment->slices[segment->slice_count]) {
    mi_assert_internal(slice->slice_count > 0);
    mi_assert_internal(slice->slice_offset == 0);    
    size_t index = mi_slice_index(slice);
    size_t maxindex = (index + slice->slice_count >= segment->slice_count ? segment->slice_count : index + slice->slice_count) - 1;
    if (slice->block_size > 0) { // a page in use, all slices need their back offset set
      used_count++;
      for (size_t i = index; i <= maxindex; i++) {
        mi_assert_internal(segment->slices[i].slice_offset == i - index);
        mi_assert_internal(i==index || segment->slices[i].slice_count == 0);
        mi_assert_internal(i==index || segment->slices[i].block_size == 1);
      }
    }
    else {  // free range of slices; only last slice needs a valid back offset
      mi_slice_t* end = &segment->slices[maxindex];
      mi_assert_internal(slice == end - end->slice_offset);
      mi_assert_internal(slice == end || end->slice_count == 0 );
      mi_assert_internal(end->block_size == 0);
      if (segment->kind == MI_SEGMENT_NORMAL && segment->thread_id != 0) {
        pq = mi_page_queue_for(slice->slice_count,tld);
        mi_assert_internal(mi_page_queue_contains(pq,mi_slice_to_page(slice)));
      }
    }    
    slice = &segment->slices[maxindex+1];
  }
  mi_assert_internal(slice == &segment->slices[segment->slice_count]);
  mi_assert_internal(used_count == segment->used + 1);
  return true;
}
#endif

/* -----------------------------------------------------------
 Segment size calculations
----------------------------------------------------------- */

// Start of the page available memory; can be used on uninitialized pages
uint8_t* _mi_segment_page_start(const mi_segment_t* segment, const mi_page_t* page, size_t* page_size) 
{
  mi_slice_t* slice = mi_page_to_slice((mi_page_t*)page);
  ptrdiff_t idx     = slice - segment->slices;
  size_t psize      = slice->slice_count*MI_SEGMENT_SLICE_SIZE;
  uint8_t* p = (uint8_t*)segment + (idx*MI_SEGMENT_SLICE_SIZE);
  /*
  if (idx == 0) {
    // the first page starts after the segment info (and possible guard page)
    p     += segment->segment_info_size;
    psize -= segment->segment_info_size;
    // for small and medium objects, ensure the page start is aligned with the block size (PR#66 by kickunderscore)
    // to ensure this, we over-estimate and align with the OS page size
    const size_t asize = _mi_os_page_size();
    uint8_t* q = (uint8_t*)_mi_align_up((uintptr_t)p, _mi_os_page_size());
    if (p < q) {
      psize -= (q - p);
      p      = q;
    }
    mi_assert_internal((uintptr_t)p % _mi_os_page_size() == 0);
  }
  */

  long secure = mi_option_get(mi_option_secure);
  if (secure > 1 || (secure == 1 && slice == &segment->slices[segment->slice_count - 1])) {
    // secure == 1: the last page has an os guard page at the end
    // secure >  1: every page has an os guard page
    psize -= _mi_os_page_size();
  }

  if (page_size != NULL) *page_size = psize;
  mi_assert_internal(_mi_ptr_page(p) == page);
  mi_assert_internal(_mi_ptr_segment(p) == segment);
  return p;
}

static size_t mi_segment_size(size_t required, size_t* pre_size, size_t* info_size) {
  size_t page_size = _mi_os_page_size();
  size_t isize     = _mi_align_up(sizeof(mi_segment_t), page_size);
  size_t guardsize = 0;
  
  if (mi_option_is_enabled(mi_option_secure)) {
    // in secure mode, we set up a protected page in between the segment info
    // and the page data (and one at the end of the segment)
    guardsize =  page_size;
    required  = _mi_align_up(required, page_size);
  }
;
  if (info_size != NULL) *info_size = isize;
  if (pre_size != NULL)  *pre_size = isize + guardsize;
  isize = _mi_align_up(isize + guardsize, MI_SEGMENT_SLICE_SIZE);
  size_t segment_size = (required==0 ? MI_SEGMENT_SIZE : _mi_align_up( required + isize + guardsize, MI_SEGMENT_SLICE_SIZE) );
  mi_assert_internal(segment_size % MI_SEGMENT_SLICE_SIZE == 0);
  return segment_size;
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


static void mi_segment_os_free(mi_segment_t* segment, mi_segments_tld_t* tld) {
  segment->thread_id = 0;
  mi_segment_map_freed_at(segment);
  mi_segments_track_size(-((long)segment->segment_size),tld);
  if (mi_option_is_enabled(mi_option_secure)) {
    _mi_os_unprotect(segment, segment->segment_size); // ensure no more guard pages are set
  }
  _mi_os_free(segment, segment->segment_size, /*segment->memid,*/ tld->stats);
}


// The thread local segment cache is limited to be at most 1/8 of the peak size of segments in use,
// and no more than 1.
#define MI_SEGMENT_CACHE_MAX      (2)
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

static bool mi_segment_cache_full(mi_segments_tld_t* tld) {
  if (tld->cache_count <  MI_SEGMENT_CACHE_MAX 
      && tld->cache_count < (1 + (tld->peak_count / MI_SEGMENT_CACHE_FRACTION))
     ) { // always allow 1 element cache
    return false;
  }
  // take the opportunity to reduce the segment cache if it is too large (now)
  // TODO: this never happens as we check against peak usage, should we use current usage instead?
  while (tld->cache_count > MI_SEGMENT_CACHE_MAX ) { //(1 + (tld->peak_count / MI_SEGMENT_CACHE_FRACTION))) {
    mi_segment_t* segment = mi_segment_cache_pop(0,tld);
    mi_assert_internal(segment != NULL);
    if (segment != NULL) mi_segment_os_free(segment, tld);
  }
  return true;
}

static bool mi_segment_cache_push(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(segment->next == NULL);
  if (segment->segment_size != MI_SEGMENT_SIZE || mi_segment_cache_full(tld)) {
    return false;
  }
  mi_assert_internal(segment->segment_size == MI_SEGMENT_SIZE);
  if (mi_option_is_enabled(mi_option_cache_reset)) {
    _mi_os_reset((uint8_t*)segment + segment->segment_info_size, segment->segment_size - segment->segment_info_size, tld->stats);
  }
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
    mi_segment_os_free(segment, tld);
  }
  mi_assert_internal(tld->cache_count == 0);
  mi_assert_internal(tld->cache == NULL);
}


/* -----------------------------------------------------------
   Slices 
----------------------------------------------------------- */


static uint8_t* mi_slice_start(const mi_slice_t* slice) {
  mi_segment_t* segment = _mi_ptr_segment(slice);
  return ((uint8_t*)segment + (mi_slice_index(slice)*MI_SEGMENT_SLICE_SIZE));
}

static mi_slice_t* mi_segment_last_slice(mi_segment_t* segment) {
  return &segment->slices[segment->slice_count-1];
}

static size_t mi_slices_in(size_t size) {
  return (size + MI_SEGMENT_SLICE_SIZE - 1)/MI_SEGMENT_SLICE_SIZE;
}

/* -----------------------------------------------------------
   Page management
----------------------------------------------------------- */


static void mi_segment_page_init(mi_segment_t* segment, size_t slice_index, size_t slice_count, mi_segments_tld_t* tld) {
  mi_assert_internal(slice_index < segment->slice_count);
  mi_page_queue_t* pq = (segment->kind == MI_SEGMENT_HUGE ? NULL : mi_page_queue_for(slice_count,tld));
  if (slice_count==0) slice_count = 1;
  mi_assert_internal(slice_index + slice_count - 1 < segment->slice_count);

  // set first and last slice (the intermediates can be undetermined)
  mi_slice_t* slice = &segment->slices[slice_index];
  slice->slice_count = slice_count;
  slice->slice_offset = 0;
  if (slice_count > 1) {
    mi_slice_t* end = &segment->slices[slice_index + slice_count - 1];
    end->slice_count = 0;
    end->slice_offset = (uint16_t)slice_count - 1;
    end->block_size = 0;
  }
  // and push it on the free page queue (if it was not a huge page)
  if (pq != NULL) mi_page_queue_push( pq, mi_slice_to_page(slice) );
             else slice->block_size = 0; // mark huge page as free anyways
}

static void mi_segment_page_add_free(mi_page_t* page, mi_segments_tld_t* tld) {
  mi_segment_t* segment = _mi_page_segment(page);
  mi_assert_internal(page->block_size==0 && page->slice_count>0 && page->slice_offset==0);
  size_t slice_index = mi_slice_index(mi_page_to_slice(page));
  mi_segment_page_init(segment,slice_index,page->slice_count,tld);

}


static void mi_segment_page_split(mi_page_t* page, size_t slice_count, mi_segments_tld_t* tld) {
  mi_assert_internal(page->slice_count >= slice_count);
  mi_assert_internal(page->block_size > 0); // no more in free queue
  if (page->slice_count <= slice_count) return;
  mi_segment_t* segment = _mi_page_segment(page);
  mi_assert_internal(segment->kind != MI_SEGMENT_HUGE);
  size_t next_index = mi_slice_index(mi_page_to_slice(page)) + slice_count;
  size_t next_count = page->slice_count - slice_count;
  mi_segment_page_init( segment, next_index, next_count, tld );  
  page->slice_count = slice_count;
}

static mi_page_t* mi_segment_page_find(size_t slice_count, mi_segments_tld_t* tld) { 
  mi_assert_internal(slice_count*MI_SEGMENT_SLICE_SIZE <= MI_LARGE_SIZE_MAX);
  // search from best fit up
  mi_page_queue_t* pq = mi_page_queue_for(slice_count,tld);
  if (slice_count == 0) slice_count = 1;
  while (pq <= &tld->pages[MI_SEGMENT_BIN_MAX]) {
    for( mi_page_t* page = pq->first; page != NULL; page = page->next) {
      if (page->slice_count >= slice_count) {
        // found one
        mi_page_queue_delete(pq,page);
        if (page->slice_count > slice_count) {
          mi_segment_page_split(page,slice_count,tld);
        }
        mi_assert_internal(page != NULL && page->slice_count == slice_count);
        return page;
      }
    }
    pq++;
  }
  // could not find a page.. 
  return NULL;  
}

static void mi_segment_page_delete(mi_slice_t* slice, mi_segments_tld_t* tld) {
  mi_assert_internal(slice->slice_count > 0 && slice->slice_offset==0 && slice->block_size==0);
  mi_assert_internal(_mi_ptr_segment(slice)->kind != MI_SEGMENT_HUGE);
  mi_page_queue_t* pq = mi_page_queue_for(slice->slice_count, tld);
  mi_page_queue_delete(pq, mi_slice_to_page(slice));
}


/* -----------------------------------------------------------
   Segment allocation
----------------------------------------------------------- */

// Allocate a segment from the OS aligned to `MI_SEGMENT_SIZE` .
static mi_segment_t* mi_segment_alloc(size_t required, mi_segments_tld_t* tld, mi_os_tld_t* os_tld)
{
  // calculate needed sizes first
  size_t info_size;
  size_t pre_size;
  size_t segment_size = mi_segment_size(required, &pre_size, &info_size);
  size_t slice_count = mi_slices_in(segment_size);
  if (slice_count > MI_SLICES_PER_SEGMENT) slice_count = MI_SLICES_PER_SEGMENT;
  mi_assert_internal(segment_size - _mi_align_up(sizeof(mi_segment_t),MI_SEGMENT_SLICE_SIZE) >= required);
  mi_assert_internal(segment_size % MI_SEGMENT_SLICE_SIZE == 0);
  //mi_assert_internal(pre_size % MI_SEGMENT_SLICE_SIZE == 0);

  // Try to get it from our thread local cache first
  bool commit = mi_option_is_enabled(mi_option_eager_commit) || mi_option_is_enabled(mi_option_eager_region_commit) 
                || required > 0; // huge page
  mi_segment_t* segment = mi_segment_cache_pop(segment_size, tld);
  if (segment==NULL) {
    // Allocate the segment from the OS
    size_t memid = 0;
    segment = (mi_segment_t*)_mi_os_alloc_aligned(segment_size, MI_SEGMENT_SIZE, commit, /* &memid,*/ os_tld);
    if (segment == NULL) return NULL;  // failed to allocate
    if (!commit) {
      _mi_os_commit(segment, info_size, tld->stats);
    }
    segment->memid = memid;
    mi_segments_track_size((long)segment_size, tld);
    mi_segment_map_allocated_at(segment);
  }
  mi_assert_internal(segment != NULL && (uintptr_t)segment % MI_SEGMENT_SIZE == 0);

  // zero the segment info
  { size_t memid = segment->memid;
    memset(segment, 0, info_size);
    segment->memid = memid;
  }

  if (mi_option_is_enabled(mi_option_secure)) {
    // in secure mode, we set up a protected page in between the segment info
    // and the page data
    mi_assert_internal(info_size == pre_size - _mi_os_page_size() && info_size % _mi_os_page_size() == 0);
    _mi_os_protect((uint8_t*)segment + info_size, (pre_size - info_size));
    size_t os_page_size = _mi_os_page_size();
    // and protect the last page too
    _mi_os_protect((uint8_t*)segment + segment_size - os_page_size, os_page_size);        
    slice_count--; // don't use the last slice :-(
  }

  // initialize segment info
  segment->segment_size = segment_size;
  segment->segment_info_size = pre_size;
  segment->thread_id = _mi_thread_id();
  segment->cookie = _mi_ptr_cookie(segment);
  segment->slice_count = slice_count;
  segment->all_committed = commit;
  segment->kind = (required == 0 ? MI_SEGMENT_NORMAL : MI_SEGMENT_HUGE);
  _mi_stat_increase(&tld->stats->page_committed, segment->segment_info_size);

  // reserve first slices for segment info
  size_t islice_count = (segment->segment_info_size + MI_SEGMENT_SLICE_SIZE - 1)/MI_SEGMENT_SLICE_SIZE;
  for (size_t i = 0; i < islice_count; i++) {
    mi_slice_t* slice = &segment->slices[i];
    if (i==0) {
      slice->slice_count = islice_count;
      slice->block_size = islice_count * MI_SEGMENT_SLICE_SIZE;
    }
    else {
      slice->slice_offset = (uint16_t)i;
      slice->block_size = 1;
    }
  }

  // initialize initial free pages
  if (segment->kind == MI_SEGMENT_NORMAL) { // not a huge page
    mi_segment_page_init(segment, islice_count, segment->slice_count - islice_count, tld);
  }
  return segment;
}


static void mi_segment_free(mi_segment_t* segment, bool force, mi_segments_tld_t* tld) {
  mi_assert_internal(segment != NULL);  
  mi_assert_internal(segment->next == NULL);
  mi_assert_internal(segment->prev == NULL);
  mi_assert_internal(segment->used == 0);

  // Remove the free pages
  mi_slice_t* slice = &segment->slices[0];
  size_t page_count = 0;
  while (slice <= mi_segment_last_slice(segment)) {
    mi_assert_internal(slice->slice_count > 0);
    mi_assert_internal(slice->slice_offset == 0);
    mi_assert_internal(mi_slice_index(slice)==0 || slice->block_size == 0); // no more used pages ..
    if (slice->block_size == 0 && segment->kind != MI_SEGMENT_HUGE) {
      mi_segment_page_delete(slice, tld);
    }
    page_count++;
    slice = slice + slice->slice_count;
  }
  mi_assert_internal(page_count == 2); // first page is allocated by the segment itself

  // stats
  _mi_stat_decrease(&tld->stats->page_committed, segment->segment_info_size);
  
  if (!force && mi_segment_cache_push(segment, tld)) {
    // it is put in our cache
  }
  else {
    // otherwise return it to the OS
    mi_segment_os_free(segment,  tld);
  }
}

/* -----------------------------------------------------------
   Page allocation
----------------------------------------------------------- */

static mi_page_t* mi_segment_page_alloc(mi_page_kind_t page_kind, size_t required, mi_segments_tld_t* tld, mi_os_tld_t* os_tld) 
{
  mi_assert_internal(required <= MI_LARGE_SIZE_MAX && page_kind <= MI_PAGE_LARGE);

  // find a free page
  size_t page_size = _mi_align_up(required,MI_SEGMENT_SLICE_SIZE);
  size_t slices_needed = page_size / MI_SEGMENT_SLICE_SIZE;
  mi_page_t* page = mi_segment_page_find(slices_needed,tld); //(required <= MI_SMALL_SIZE_MAX ? 0 : slices_needed), tld);
  if (page==NULL) {
    // no free page, allocate a new segment and try again
    if (mi_segment_alloc(0, tld, os_tld) == NULL) return NULL;  // OOM    
    return mi_segment_page_alloc(page_kind, required, tld, os_tld);
  }
  mi_assert_internal(page != NULL && page->slice_count*MI_SEGMENT_SLICE_SIZE == page_size);

  // set slice back pointers and commit/unreset
  mi_segment_t* segment = _mi_page_segment(page);
  mi_slice_t* slice = mi_page_to_slice(page);
  bool commit = false;
  bool unreset = false;
  for (size_t i = 0; i < page->slice_count; i++, slice++) {
    slice->slice_offset = (uint16_t)i;
    slice->block_size = 1;
    if (i > 0) slice->slice_count = 0;
    if (!segment->all_committed && !slice->is_committed) {
      slice->is_committed = true;
      commit = true;    
    }
    if (slice->is_reset) {
      slice->is_reset = false;
      unreset = true;      
    }
  }
  uint8_t* page_start = mi_slice_start(mi_page_to_slice(page));
  if(commit) { _mi_os_commit(page_start, page_size, tld->stats); }
  if(unreset){ _mi_os_unreset(page_start, page_size, tld->stats); }

  // initialize the page and return
  mi_assert_internal(segment->thread_id == _mi_thread_id()); 
  segment->used++;
  mi_page_init_flags(page, segment->thread_id);
  return page;
}

static mi_slice_t* mi_segment_page_free_coalesce(mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert_internal(page != NULL && page->slice_count > 0 && page->slice_offset == 0 && page->block_size > 0);
  mi_segment_t* segment = _mi_page_segment(page);
  mi_assert_internal(segment->used > 0);
  segment->used--;
  
  // free and coalesce the page
  mi_slice_t* slice = mi_page_to_slice(page);
  size_t slice_count = slice->slice_count;
  mi_slice_t* next = slice + slice->slice_count;
  mi_assert_internal(next <= mi_segment_last_slice(segment) + 1);
  if (next <= mi_segment_last_slice(segment) && next->block_size==0) {
    // free next block -- remove it from free and merge
    mi_assert_internal(next->slice_count > 0 && next->slice_offset==0);
    slice_count += next->slice_count; // extend
    mi_segment_page_delete(next, tld);
  }
  if (slice > segment->slices) {
    mi_slice_t* prev = slice - 1;
    prev = prev - prev->slice_offset;
    mi_assert_internal(prev >= segment->slices);
    if (prev->block_size==0) {
      // free previous slice -- remove it from free and merge
      mi_assert_internal(prev->slice_count > 0 && prev->slice_offset==0);
      slice_count += prev->slice_count;
      mi_segment_page_delete(prev, tld);
      slice = prev;
    }
  }
  
  // and add the new free page
  mi_segment_page_init(segment, mi_slice_index(slice), slice_count, tld);
  mi_assert_expensive(mi_segment_is_valid(segment,tld));
  return slice;
}


/* -----------------------------------------------------------
   Page Free
----------------------------------------------------------- */

static void mi_segment_abandon(mi_segment_t* segment, mi_segments_tld_t* tld);

static mi_slice_t* mi_segment_page_clear(mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert_internal(page->block_size > 0);
  mi_assert_internal(mi_page_all_free(page));
  mi_segment_t* segment = _mi_ptr_segment(page);
  mi_assert_internal(segment->all_committed || page->is_committed);
  size_t inuse = page->capacity * page->block_size;
  _mi_stat_decrease(&tld->stats->page_committed, inuse);
  _mi_stat_decrease(&tld->stats->pages, 1);
  
  // reset the page memory to reduce memory pressure?
  if (!page->is_reset && mi_option_is_enabled(mi_option_page_reset)) {
    size_t psize;
    uint8_t* start = _mi_page_start(segment, page, &psize);
    page->is_reset = true;
    _mi_os_reset(start, psize, tld->stats);
  }

  // zero the page data
  size_t slice_count = page->slice_count; // don't clear the slice_count
  bool is_reset = page->is_reset;         // don't clear the reset flag
  bool is_committed = page->is_committed; // don't clear the commit flag
  memset(page, 0, sizeof(*page));
  page->slice_count = slice_count;
  page->is_reset = is_reset;
  page->is_committed = is_committed;
  page->block_size = 1;

  // and free it
  if (segment->kind != MI_SEGMENT_HUGE) {
    return mi_segment_page_free_coalesce(page, tld);
  }
  else {
    mi_assert_internal(segment->used == 1);
    segment->used--;
    page->block_size = 0;  // pretend free
    return mi_page_to_slice(page);
  }
}

void _mi_segment_page_free(mi_page_t* page, bool force, mi_segments_tld_t* tld)
{
  mi_assert(page != NULL);
  mi_segment_t* segment = _mi_page_segment(page);
  mi_assert_expensive(mi_segment_is_valid(segment,tld));

  // mark it as free now
  mi_segment_page_clear(page, tld);

  if (segment->used == 0) {
    // no more used pages; remove from the free list and free the segment
    mi_segment_free(segment, force, tld);
  }
  else if (segment->used == segment->abandoned) {
    // only abandoned pages; remove from free list and abandon
    mi_segment_abandon(segment,tld);
  }  
}


/* -----------------------------------------------------------
   Abandonment
----------------------------------------------------------- */

// When threads terminate, they can leave segments with
// live blocks (reached through other threads). Such segments
// are "abandoned" and will be reclaimed by other threads to
// reuse their pages and/or free them eventually
static volatile mi_segment_t* abandoned = NULL;
static volatile uintptr_t     abandoned_count = 0;

static void mi_segment_abandon(mi_segment_t* segment, mi_segments_tld_t* tld) {
  mi_assert_internal(segment->used == segment->abandoned);
  mi_assert_internal(segment->used > 0);
  mi_assert_internal(segment->abandoned_next == NULL);
  mi_assert_expensive(mi_segment_is_valid(segment,tld));

  // remove the free pages from our lists
  mi_slice_t* slice = &segment->slices[0];  
  while (slice <= mi_segment_last_slice(segment)) {
    mi_assert_internal(slice->slice_count > 0);
    mi_assert_internal(slice->slice_offset == 0);
    if (slice->block_size == 0) { // a free page
      mi_segment_page_delete(slice,tld);
      slice->block_size = 0; // but keep it free
    }
    slice = slice + slice->slice_count;
  }

  // add it to the abandoned list
  segment->thread_id = 0;
  do {
    segment->abandoned_next = (mi_segment_t*)abandoned;
  } while (!mi_atomic_compare_exchange_ptr((volatile void**)&abandoned, segment, segment->abandoned_next));
  mi_atomic_increment(&abandoned_count);
  _mi_stat_increase(&tld->stats->segments_abandoned,1);
  mi_segments_track_size(-((long)segment->segment_size), tld);
}

void _mi_segment_page_abandon(mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert(page != NULL);
  mi_segment_t* segment = _mi_page_segment(page);
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
  uintptr_t reclaimed = 0;
  uintptr_t atmost;
  if (try_all) {
    atmost = abandoned_count+16;   // close enough
  }
  else {
    atmost = abandoned_count/8;    // at most 1/8th of all outstanding (estimated)
    if (atmost < 2) atmost = 2;    // but at least 2
  }

  // for `atmost` `reclaimed` abandoned segments...
  while(atmost > reclaimed) {
    // try to claim the head of the abandoned segments
    mi_segment_t* segment;
    do {
      segment = (mi_segment_t*)abandoned;
    } while(segment != NULL && !mi_atomic_compare_exchange_ptr((volatile void**)&abandoned, segment->abandoned_next, segment));
    if (segment==NULL) break; // stop early if no more segments available

    // got it.
    mi_atomic_decrement(&abandoned_count);
    mi_assert_expensive(mi_segment_is_valid(segment, tld));
    segment->abandoned_next = NULL;
    segment->thread_id = _mi_thread_id();
    mi_segments_track_size((long)segment->segment_size,tld);
    mi_assert_internal(segment->next == NULL && segment->prev == NULL);
    _mi_stat_decrease(&tld->stats->segments_abandoned,1);

    mi_slice_t* slice = &segment->slices[0];
    mi_assert_internal(slice->slice_count>0 && slice->block_size>0); // segment allocated page
    slice = slice + slice->slice_count; // skip the first segment allocated page
    while (slice <= mi_segment_last_slice(segment)) {
      mi_assert_internal(slice->slice_count > 0);
      mi_assert_internal(slice->slice_offset == 0);
      mi_page_t* page = mi_slice_to_page(slice);
      if (page->block_size == 0) { // a free page, add it to our lists
        mi_segment_page_add_free(page,tld);
      }
      slice = slice + slice->slice_count;
    }

    slice = &segment->slices[0];
    mi_assert_internal(slice->slice_count>0 && slice->block_size>0); // segment allocated page
    slice = slice + slice->slice_count; // skip the first segment allocated page
    while (slice <= mi_segment_last_slice(segment)) {
      mi_assert_internal(slice->slice_count > 0);
      mi_assert_internal(slice->slice_offset == 0);
      mi_page_t* page = mi_slice_to_page(slice);
      if (page->block_size > 0) { // a used page
        mi_assert_internal(page->next == NULL && page->prev==NULL);
        _mi_stat_decrease(&tld->stats->pages_abandoned, 1);
        segment->abandoned--;
        if (mi_page_all_free(page)) {
          // if everything free by now, free the page
          slice = mi_segment_page_clear(page, tld);   // set slice again due to coalesceing        
        }
        else {
          // otherwise reclaim it
          mi_page_init_flags(page, segment->thread_id);
          _mi_page_reclaim(heap, page);
        }
      }      
      mi_assert_internal(slice->slice_count>0 && slice->slice_offset==0);
      slice = slice + slice->slice_count;
    }

    mi_assert(segment->abandoned == 0);
    if (segment->used == 0) {  // due to page_clear
      mi_segment_free(segment,false,tld);
    }
    else {
      reclaimed++;      
    }
  }
  return (reclaimed>0);
}


/* -----------------------------------------------------------
   Small page allocation
----------------------------------------------------------- */

static mi_page_t* mi_segment_huge_page_alloc(size_t size, mi_segments_tld_t* tld, mi_os_tld_t* os_tld)
{
  mi_segment_t* segment = mi_segment_alloc(size,tld,os_tld);
  if (segment == NULL) return NULL;
  mi_assert_internal(segment->segment_size - segment->segment_info_size >= size);
  segment->used = 1;
  mi_page_t* page = mi_slice_to_page(&segment->slices[0]);
  mi_assert_internal(page->block_size > 0 && page->slice_count > 0);
  size_t initial_count = page->slice_count;
  page = page + initial_count;
  page->slice_count  = (segment->segment_size - segment->segment_info_size)/MI_SEGMENT_SLICE_SIZE;
  page->slice_offset = 0;
  page->block_size = size;  
  mi_assert_internal(page->slice_count * MI_SEGMENT_SLICE_SIZE >= size);
  mi_assert_internal(page->slice_count >= segment->slice_count - initial_count);
  // set back pointers  
  for (size_t i = 1; i <segment->slice_count; i++) {
    mi_slice_t* slice = (mi_slice_t*)(page + i);
    slice->slice_offset = (uint16_t)i;
    slice->block_size = 1;
    slice->slice_count = 0;    
  }
  mi_page_init_flags(page,segment->thread_id);
  return page;
}

/* -----------------------------------------------------------
   Page allocation and free
----------------------------------------------------------- */
/*
static bool mi_is_good_fit(size_t bsize, size_t size) {
  // good fit if no more than 25% wasted
  return (bsize > 0 && size > 0 && bsize < size && (size - (size % bsize)) < (size/4));
}
*/

mi_page_t* _mi_segment_page_alloc(size_t block_size, mi_segments_tld_t* tld, mi_os_tld_t* os_tld) {
  mi_page_t* page;
  if (block_size <= MI_SMALL_SIZE_MAX) {// || mi_is_good_fit(block_size,MI_SMALL_PAGE_SIZE)) {
    page = mi_segment_page_alloc(MI_PAGE_SMALL,block_size,tld,os_tld);
  }
  else if (block_size <= MI_MEDIUM_SIZE_MAX) {// || mi_is_good_fit(block_size, MI_MEDIUM_PAGE_SIZE)) {
    page = mi_segment_page_alloc(MI_PAGE_MEDIUM,MI_MEDIUM_PAGE_SIZE,tld, os_tld);
  }
  else if (block_size <= MI_LARGE_SIZE_MAX) {
    page = mi_segment_page_alloc(MI_PAGE_LARGE,block_size,tld, os_tld);
  }
  else {
    page = mi_segment_huge_page_alloc(block_size,tld,os_tld);
  }
  mi_assert_expensive(page == NULL || mi_segment_is_valid(_mi_page_segment(page),tld));
  return page;
}


/* -----------------------------------------------------------
  The following functions are to reliably find the segment or
  block that encompasses any pointer p (or NULL if it is not
  in any of our segments).
  We maintain a bitmap of all memory with 1 bit per MI_SEGMENT_SIZE (128mb)
  set to 1 if it contains the segment meta data.
----------------------------------------------------------- */

#if (MI_INTPTR_SIZE==8)
#define MI_MAX_ADDRESS    ((size_t)1 << 44)   // 16TB 
#else
#define MI_MAX_ADDRESS    ((size_t)1 << 31)   // 2Gb
#endif

#define MI_SEGMENT_MAP_BITS  (MI_MAX_ADDRESS / MI_SEGMENT_SIZE)
#define MI_SEGMENT_MAP_SIZE  (MI_SEGMENT_MAP_BITS / 8)
#define MI_SEGMENT_MAP_WSIZE (MI_SEGMENT_MAP_SIZE / MI_INTPTR_SIZE)

static volatile uintptr_t mi_segment_map[MI_SEGMENT_MAP_WSIZE];  // 1KiB per TB with 128MiB segments

static size_t mi_segment_map_index_of(const mi_segment_t* segment, size_t* bitidx) {
  mi_assert_internal(_mi_ptr_segment(segment) == segment); // is it aligned on 128MiB?  
  uintptr_t segindex = ((uintptr_t)segment % MI_MAX_ADDRESS) / MI_SEGMENT_SIZE;
  *bitidx = segindex % (8*MI_INTPTR_SIZE);
  return (segindex / (8*MI_INTPTR_SIZE));
}

static void mi_segment_map_allocated_at(const mi_segment_t* segment) {
  size_t bitidx;
  size_t index = mi_segment_map_index_of(segment, &bitidx);
  mi_assert_internal(index < MI_SEGMENT_MAP_WSIZE);
  if (index==0) return;
  uintptr_t mask;
  uintptr_t newmask;
  do {
    mask = mi_segment_map[index];
    newmask = (mask | ((uintptr_t)1 << bitidx));
  } while (!mi_atomic_compare_exchange(&mi_segment_map[index], newmask, mask));
}

static void mi_segment_map_freed_at(const mi_segment_t* segment) {
  size_t bitidx;
  size_t index = mi_segment_map_index_of(segment, &bitidx);
  mi_assert_internal(index < MI_SEGMENT_MAP_WSIZE);
  if (index == 0) return;
  uintptr_t mask;
  uintptr_t newmask;
  do {
    mask = mi_segment_map[index];
    newmask = (mask & ~((uintptr_t)1 << bitidx));
  } while (!mi_atomic_compare_exchange(&mi_segment_map[index], newmask, mask));
}

// Determine the segment belonging to a pointer or NULL if it is not in a valid segment.
static mi_segment_t* _mi_segment_of(const void* p) {
  mi_segment_t* segment = _mi_ptr_segment(p);
  size_t bitidx;
  size_t index = mi_segment_map_index_of(segment, &bitidx);
  // fast path: for any pointer to valid small/medium/large object or first 4MiB in huge
  if (mi_likely((mi_segment_map[index] & ((uintptr_t)1 << bitidx)) != 0)) {
    return segment; // yes, allocated by us
  }
  if (index==0) return NULL;
  // search downwards for the first segment in case it is an interior pointer 
  // could be slow but searches in 256MiB steps trough valid huge objects
  // note: we could maintain a lowest index to speed up the path for invalid pointers?
  size_t lobitidx;
  size_t loindex;
  uintptr_t lobits = mi_segment_map[index] & (((uintptr_t)1 << bitidx) - 1);
  if (lobits != 0) {
    loindex = index;
    lobitidx = _mi_bsr(lobits);
  }
  else {
    loindex = index - 1;
    while (loindex > 0 && mi_segment_map[loindex] == 0) loindex--;
    if (loindex==0) return NULL;
    lobitidx = _mi_bsr(mi_segment_map[loindex]);
  }
  // take difference as the addresses could be larger than the MAX_ADDRESS space.
  size_t diff = (((index - loindex) * (8*MI_INTPTR_SIZE)) + bitidx - lobitidx) * MI_SEGMENT_SIZE;
  segment = (mi_segment_t*)((uint8_t*)segment - diff);

  if (segment == NULL) return NULL;
  mi_assert_internal((void*)segment < p);
  bool cookie_ok = (_mi_ptr_cookie(segment) == segment->cookie);
  mi_assert_internal(cookie_ok);
  if (mi_unlikely(!cookie_ok)) return NULL;
  if (((uint8_t*)segment + segment->segment_size) <= (uint8_t*)p) return NULL; // outside the range
  mi_assert_internal(p >= (void*)segment && (uint8_t*)p < (uint8_t*)segment + segment->segment_size);
  return segment;
}

// Is this a valid pointer in our heap?
static bool  mi_is_valid_pointer(const void* p) {
  return (_mi_segment_of(p) != NULL);
}

bool mi_is_in_heap_region(const void* p) mi_attr_noexcept {
  return mi_is_valid_pointer(p);
}

/*
// Return the full segment range belonging to a pointer
static void* mi_segment_range_of(const void* p, size_t* size) {
  mi_segment_t* segment = _mi_segment_of(p);
  if (segment == NULL) {
    if (size != NULL) *size = 0;
    return NULL;
  }
  else {
    if (size != NULL) *size = segment->segment_size;
    return segment;
  }
}
*/


