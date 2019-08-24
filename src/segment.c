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
   Slices
----------------------------------------------------------- */

static const mi_slice_t* mi_segment_slices_end(const mi_segment_t* segment) {
  return &segment->slices[segment->slice_entries];
}


static uint8_t* mi_slice_start(const mi_slice_t* slice) {
  mi_segment_t* segment = _mi_ptr_segment(slice);
  mi_assert_internal(slice >= segment->slices && slice < mi_segment_slices_end(segment));
  return ((uint8_t*)segment + ((slice - segment->slices)*MI_SEGMENT_SLICE_SIZE));
}


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
  mi_assert_internal(mi_slice_bin8(MI_SLICES_PER_SEGMENT) <= MI_SEGMENT_BIN_MAX);
  size_t bin = (slice_count==0 ? 0 : mi_slice_bin8(slice_count));
  mi_assert_internal(bin <= MI_SEGMENT_BIN_MAX);
  return bin;
}

static size_t mi_slice_index(const mi_slice_t* slice) {
  mi_segment_t* segment = _mi_ptr_segment(slice);
  ptrdiff_t index = slice - segment->slices;
  mi_assert_internal(index >= 0 && index < (ptrdiff_t)segment->slice_entries);
  return index;
}


/* -----------------------------------------------------------
   Slice span queues
----------------------------------------------------------- */

static void mi_span_queue_push(mi_span_queue_t* sq, mi_slice_t* slice) {
  // todo: or push to the end?
  mi_assert_internal(slice->prev == NULL && slice->next==NULL);
  slice->prev = NULL; // paranoia
  slice->next = sq->first;
  sq->first = slice;
  if (slice->next != NULL) slice->next->prev = slice;
                     else sq->last = slice;
  slice->block_size = 0; // free
}

static mi_span_queue_t* mi_span_queue_for(size_t slice_count, mi_segments_tld_t* tld) {
  size_t bin = mi_slice_bin(slice_count);
  mi_span_queue_t* sq = &tld->spans[bin];
  mi_assert_internal(sq->slice_count >= slice_count);
  return sq;
}

static void mi_span_queue_delete(mi_span_queue_t* sq, mi_slice_t* slice) {
  mi_assert_internal(slice->block_size==0 && slice->slice_count>0 && slice->slice_offset==0);
  // should work too if the queue does not contain slice (which can happen during reclaim)
  if (slice->prev != NULL) slice->prev->next = slice->next;
  if (slice == sq->first) sq->first = slice->next;
  if (slice->next != NULL) slice->next->prev = slice->prev;
  if (slice == sq->last) sq->last = slice->prev;
  slice->prev = NULL;
  slice->next = NULL;
  slice->block_size = 1; // no more free
}


/* -----------------------------------------------------------
 Invariant checking
----------------------------------------------------------- */

#if (MI_DEBUG > 1)
static bool mi_span_queue_contains(mi_span_queue_t* sq, mi_slice_t* slice) {
  for (mi_slice_t* s = sq->first; s != NULL; s = s->next) {
    if (s==slice) return true;
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
  const mi_slice_t* end = mi_segment_slices_end(segment);
  size_t used_count = 0;
  mi_span_queue_t* sq;
  while(slice < end) {
    mi_assert_internal(slice->slice_count > 0);
    mi_assert_internal(slice->slice_offset == 0);
    size_t index = mi_slice_index(slice);
    size_t maxindex = (index + slice->slice_count >= segment->slice_entries ? segment->slice_entries : index + slice->slice_count) - 1;
    if (slice->block_size > 0) { // a page in use, we need at least MAX_SLICE_OFFSET valid back offsets
      used_count++;
      for (size_t i = 0; i <= MI_MAX_SLICE_OFFSET && index + i <= maxindex; i++) {
        mi_assert_internal(segment->slices[index + i].slice_offset == i*sizeof(mi_slice_t));
        mi_assert_internal(i==0 || segment->slices[index + i].slice_count == 0);
        mi_assert_internal(i==0 || segment->slices[index + i].block_size == 1);
      }
      // and the last entry as well (for coalescing)
      const mi_slice_t* last = slice + slice->slice_count - 1;
      if (last > slice && last < mi_segment_slices_end(segment)) {
        mi_assert_internal(last->slice_offset == (slice->slice_count-1)*sizeof(mi_slice_t));
        mi_assert_internal(last->slice_count == 0);
        mi_assert_internal(last->block_size == 1);
      }
    }
    else {  // free range of slices; only last slice needs a valid back offset
      mi_slice_t* last = &segment->slices[maxindex];
      mi_assert_internal((uint8_t*)slice == (uint8_t*)last - last->slice_offset);
      mi_assert_internal(slice == last || last->slice_count == 0 );
      mi_assert_internal(last->block_size == 0);
      if (segment->kind == MI_SEGMENT_NORMAL && segment->thread_id != 0) { // segment is not huge or abandonded
        sq = mi_span_queue_for(slice->slice_count,tld);
        mi_assert_internal(mi_span_queue_contains(sq,slice));
      }
    }
    slice = &segment->slices[maxindex+1];
  }
  mi_assert_internal(slice == end);
  mi_assert_internal(used_count == segment->used + 1);
  return true;
}
#endif

/* -----------------------------------------------------------
 Segment size calculations
----------------------------------------------------------- */

static size_t mi_segment_size(mi_segment_t* segment) {
  return segment->segment_slices * MI_SEGMENT_SLICE_SIZE;
}
static size_t mi_segment_info_size(mi_segment_t* segment) {
  return segment->segment_info_slices * MI_SEGMENT_SLICE_SIZE;
}

// Start of the page available memory; can be used on uninitialized pages
uint8_t* _mi_segment_page_start(const mi_segment_t* segment, const mi_page_t* page, size_t* page_size)
{
  const mi_slice_t* slice = mi_page_to_slice((mi_page_t*)page);
  ptrdiff_t idx = slice - segment->slices;
  size_t psize  = slice->slice_count*MI_SEGMENT_SLICE_SIZE;
  uint8_t* p    = (uint8_t*)segment + (idx*MI_SEGMENT_SLICE_SIZE);
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
  if (secure > 1 || (secure == 1 && slice == &segment->slices[segment->slice_entries - 1])) {
    // secure == 1: the last page has an os guard page at the end
    // secure >  1: every page has an os guard page
    psize -= _mi_os_page_size();
  }

  if (page_size != NULL) *page_size = psize;
  mi_assert_internal(_mi_ptr_page(p) == page);
  mi_assert_internal(_mi_ptr_segment(p) == segment);
  return p;
}

static size_t mi_segment_calculate_slices(size_t required, size_t* pre_size, size_t* info_slices) {
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
  if (pre_size != NULL) *pre_size = isize;
  isize = _mi_align_up(isize + guardsize, MI_SEGMENT_SLICE_SIZE);
  if (info_slices != NULL) *info_slices = isize / MI_SEGMENT_SLICE_SIZE;
  size_t segment_size = (required==0 ? MI_SEGMENT_SIZE : _mi_align_up( required + isize + guardsize, MI_SEGMENT_SLICE_SIZE) );  
  mi_assert_internal(segment_size % MI_SEGMENT_SLICE_SIZE == 0);
  return (segment_size / MI_SEGMENT_SLICE_SIZE);
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
  mi_segments_track_size(-((long)mi_segment_size(segment)),tld);
  if (mi_option_is_enabled(mi_option_secure)) {
    _mi_os_unprotect(segment, mi_segment_size(segment)); // ensure no more guard pages are set
  }
  _mi_os_free(segment, mi_segment_size(segment), /*segment->memid,*/ tld->stats);
}


// The thread local segment cache is limited to be at most 1/8 of the peak size of segments in use,
// and no more than 1.
#define MI_SEGMENT_CACHE_MAX      (4)
#define MI_SEGMENT_CACHE_FRACTION (8)

// note: returned segment may be partially reset
static mi_segment_t* mi_segment_cache_pop(size_t segment_slices, mi_segments_tld_t* tld) {
  if (segment_slices != 0 && segment_slices != MI_SLICES_PER_SEGMENT) return NULL;
  mi_segment_t* segment = tld->cache;
  if (segment == NULL) return NULL;
  tld->cache_count--;
  tld->cache = segment->next;
  segment->next = NULL;
  mi_assert_internal(segment->segment_slices == MI_SLICES_PER_SEGMENT);
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
  if (segment->segment_slices != MI_SLICES_PER_SEGMENT || mi_segment_cache_full(tld)) {
    return false;
  }
  mi_assert_internal(segment->segment_slices == MI_SLICES_PER_SEGMENT);
  if (mi_option_is_enabled(mi_option_cache_reset)) {
    _mi_os_reset((uint8_t*)segment + mi_segment_info_size(segment), mi_segment_size(segment) - mi_segment_info_size(segment), tld->stats);
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
   Span management
----------------------------------------------------------- */

static uintptr_t mi_segment_commit_mask(mi_segment_t* segment, bool conservative, uint8_t* p, size_t size, uint8_t** start_p, size_t* full_size) {
  mi_assert_internal(_mi_ptr_segment(p) == segment);
  if (size == 0 || size > MI_SEGMENT_SIZE) return 0;
  if (p >= (uint8_t*)segment + mi_segment_size(segment)) return 0;

  uintptr_t diff = (p - (uint8_t*)segment);
  uintptr_t start;
  uintptr_t end;
  if (conservative) {
    start = _mi_align_up(diff, MI_COMMIT_SIZE);
    end   = _mi_align_down(diff + size, MI_COMMIT_SIZE);
  }
  else {
    start = _mi_align_down(diff, MI_COMMIT_SIZE);
    end   = _mi_align_up(diff + size, MI_COMMIT_SIZE);
  }
  mi_assert_internal(start % MI_COMMIT_SIZE==0 && end % MI_COMMIT_SIZE == 0);
  *start_p   = (uint8_t*)segment + start;
  *full_size = (end > start ? end - start : 0);

  uintptr_t bitidx = start / MI_COMMIT_SIZE;
  mi_assert_internal(bitidx < (MI_INTPTR_SIZE*8));
  
  uintptr_t bitcount = *full_size / MI_COMMIT_SIZE; // can be 0
  if (bitidx + bitcount > MI_INTPTR_SIZE*8) {
    _mi_warning_message("%zu %zu %zu %zu 0x%p %zu\n", bitidx, bitcount, start, end, p, size);
  }
  mi_assert_internal((bitidx + bitcount) <= (MI_INTPTR_SIZE*8));

  uintptr_t mask = (((uintptr_t)1 << bitcount) - 1) << bitidx;

  return mask;
}

static void mi_segment_commitx(mi_segment_t* segment, bool commit, uint8_t* p, size_t size, mi_stats_t* stats) {    
  // commit liberal, but decommit conservative
  uint8_t* start;
  size_t   full_size;
  uintptr_t mask = mi_segment_commit_mask(segment,!commit/*conservative*/,p,size,&start,&full_size);
  if (mask==0 || full_size==0) return;

  if (commit && (segment->commit_mask & mask) != mask) {
    _mi_os_commit(start,full_size,stats);
    segment->commit_mask |= mask; 
  }
  else if (!commit && (segment->commit_mask & mask) != 0) {
    _mi_os_decommit(start, full_size,stats);
    segment->commit_mask &= ~mask;
  }
}

static void mi_segment_ensure_committed(mi_segment_t* segment, uint8_t* p, size_t size, mi_stats_t* stats) {
  if (~segment->commit_mask == 0) return; // fully committed
  mi_segment_commitx(segment,true,p,size,stats);
}

static void mi_segment_perhaps_decommit(mi_segment_t* segment, uint8_t* p, size_t size, mi_stats_t* stats) {
  if (!segment->allow_decommit || !mi_option_is_enabled(mi_option_decommit)) return;
  if (segment->commit_mask == 1) return; // fully decommitted
  mi_segment_commitx(segment, false, p, size, stats);
}

static void mi_segment_span_free(mi_segment_t* segment, size_t slice_index, size_t slice_count, mi_segments_tld_t* tld) {
  mi_assert_internal(slice_index < segment->slice_entries);
  mi_span_queue_t* sq = (segment->kind == MI_SEGMENT_HUGE ? NULL : mi_span_queue_for(slice_count,tld));
  if (slice_count==0) slice_count = 1;
  mi_assert_internal(slice_index + slice_count - 1 < segment->slice_entries);

  // set first and last slice (the intermediates can be undetermined)
  mi_slice_t* slice = &segment->slices[slice_index];
  slice->slice_count = (uint32_t)slice_count;
  mi_assert_internal(slice->slice_count == slice_count); // no overflow?
  slice->slice_offset = 0;
  if (slice_count > 1) {
    mi_slice_t* last = &segment->slices[slice_index + slice_count - 1];
    last->slice_count = 0;
    last->slice_offset = (uint32_t)(sizeof(mi_page_t)*(slice_count - 1));
    last->block_size = 0;
  }

  // perhaps decommit
  mi_segment_perhaps_decommit(segment,mi_slice_start(slice),slice_count*MI_SEGMENT_SLICE_SIZE,tld->stats);

  // and push it on the free page queue (if it was not a huge page)
  if (sq != NULL) mi_span_queue_push( sq, slice );
             else slice->block_size = 0; // mark huge page as free anyways
}

// called from reclaim to add existing free spans
static void mi_segment_span_add_free(mi_slice_t* slice, mi_segments_tld_t* tld) {
  mi_segment_t* segment = _mi_ptr_segment(slice);
  mi_assert_internal(slice->block_size==0 && slice->slice_count>0 && slice->slice_offset==0);
  size_t slice_index = mi_slice_index(slice);
  mi_segment_span_free(segment,slice_index,slice->slice_count,tld);
}

static void mi_segment_span_remove_from_queue(mi_slice_t* slice, mi_segments_tld_t* tld) {
  mi_assert_internal(slice->slice_count > 0 && slice->slice_offset==0 && slice->block_size==0);
  mi_assert_internal(_mi_ptr_segment(slice)->kind != MI_SEGMENT_HUGE);
  mi_span_queue_t* sq = mi_span_queue_for(slice->slice_count, tld);
  mi_span_queue_delete(sq, slice);
}


static mi_slice_t* mi_segment_span_free_coalesce(mi_slice_t* slice, mi_segments_tld_t* tld) {
  mi_assert_internal(slice != NULL && slice->slice_count > 0 && slice->slice_offset == 0 && slice->block_size > 0);
  mi_segment_t* segment = _mi_ptr_segment(slice);
  mi_assert_internal(segment->used > 0);
  segment->used--;

  // for huge pages, just mark as free but don't add to the queues
  if (segment->kind == MI_SEGMENT_HUGE) {
    mi_assert_internal(segment->used == 0);
    slice->block_size = 0;  // mark as free anyways
    return slice;
  }

  // otherwise coalesce the span and add to the free span queues
  size_t slice_count = slice->slice_count;
  mi_slice_t* next = slice + slice->slice_count;
  mi_assert_internal(next <= mi_segment_slices_end(segment));
  if (next < mi_segment_slices_end(segment) && next->block_size==0) {
    // free next block -- remove it from free and merge
    mi_assert_internal(next->slice_count > 0 && next->slice_offset==0);
    slice_count += next->slice_count; // extend
    mi_segment_span_remove_from_queue(next, tld);
  }
  if (slice > segment->slices) {
    mi_slice_t* prev = mi_slice_first(slice - 1);
    mi_assert_internal(prev >= segment->slices);
    if (prev->block_size==0) {
      // free previous slice -- remove it from free and merge
      mi_assert_internal(prev->slice_count > 0 && prev->slice_offset==0);
      slice_count += prev->slice_count;
      mi_segment_span_remove_from_queue(prev, tld);
      slice = prev;
    }
  }

  // and add the new free page
  mi_segment_span_free(segment, mi_slice_index(slice), slice_count, tld);
  mi_assert_expensive(mi_segment_is_valid(segment, tld));
  return slice;
}


static void mi_segment_slice_split(mi_segment_t* segment, mi_slice_t* slice, size_t slice_count, mi_segments_tld_t* tld) {
  mi_assert_internal(_mi_ptr_segment(slice)==segment);
  mi_assert_internal(slice->slice_count >= slice_count);
  mi_assert_internal(slice->block_size > 0); // no more in free queue
  if (slice->slice_count <= slice_count) return;
  mi_assert_internal(segment->kind != MI_SEGMENT_HUGE);
  size_t next_index = mi_slice_index(slice) + slice_count;
  size_t next_count = slice->slice_count - slice_count;
  mi_segment_span_free(segment, next_index, next_count, tld);
  slice->slice_count = (uint32_t)slice_count;
}


static mi_page_t* mi_segment_span_allocate(mi_segment_t* segment, size_t slice_index, size_t slice_count, mi_segments_tld_t* tld) {
  mi_assert_internal(slice_index < segment->slice_entries);
  mi_slice_t* slice = &segment->slices[slice_index];
  mi_assert_internal(slice->block_size==0 || slice->block_size==1);
  slice->slice_offset = 0;
  slice->slice_count = (uint32_t)slice_count;
  mi_assert_internal(slice->slice_count == slice_count);
  slice->block_size = slice_count * MI_SEGMENT_SLICE_SIZE;
  mi_page_t*  page = mi_slice_to_page(slice);

  // set slice back pointers for the first MI_MAX_SLICE_OFFSET entries
  size_t extra = slice_count-1;
  if (extra > MI_MAX_SLICE_OFFSET) extra = MI_MAX_SLICE_OFFSET;
  if (slice_index + extra >= segment->slice_entries) extra = segment->slice_entries - slice_index - 1;  // huge objects may have more slices than avaiable entries in the segment->slices
  slice++;
  for (size_t i = 1; i <= extra; i++, slice++) {
    slice->slice_offset = (uint32_t)(sizeof(mi_slice_t)*i);
    slice->slice_count = 0;
    slice->block_size = 1;
  }

  // and also for the last one (if not set already) (the last one is needed for coalescing)
  mi_slice_t* last = &segment->slices[slice_index + slice_count - 1];
  if (last < mi_segment_slices_end(segment) && last >= slice) {
    last->slice_offset = (uint32_t)(sizeof(mi_slice_t)*(slice_count-1));
    last->slice_count = 0;
    last->block_size = 1;
  }

  // ensure the memory is committed
  mi_segment_ensure_committed(segment, _mi_page_start(segment,page,NULL), slice_count * MI_SEGMENT_SLICE_SIZE, tld->stats);
  segment->used++;
  return page;
}

static mi_page_t* mi_segments_page_find_and_allocate(size_t slice_count, mi_segments_tld_t* tld) {
  mi_assert_internal(slice_count*MI_SEGMENT_SLICE_SIZE <= MI_LARGE_OBJ_SIZE_MAX);
  // search from best fit up
  mi_span_queue_t* sq = mi_span_queue_for(slice_count, tld);
  if (slice_count == 0) slice_count = 1;
  while (sq <= &tld->spans[MI_SEGMENT_BIN_MAX]) {
    for (mi_slice_t* slice = sq->first; slice != NULL; slice = slice->next) {
      if (slice->slice_count >= slice_count) {
        // found one
        mi_span_queue_delete(sq, slice);
        mi_segment_t* segment = _mi_ptr_segment(slice);
        if (slice->slice_count > slice_count) {
          mi_segment_slice_split(segment, slice, slice_count, tld);
        }
        mi_assert_internal(slice != NULL && slice->slice_count == slice_count && slice->block_size > 0);
        return mi_segment_span_allocate(segment, mi_slice_index(slice), slice->slice_count, tld);
      }
    }
    sq++;
  }
  // could not find a page..
  return NULL;
}


/* -----------------------------------------------------------
   Segment allocation
----------------------------------------------------------- */

// Allocate a segment from the OS aligned to `MI_SEGMENT_SIZE` .
static mi_segment_t* mi_segment_alloc(size_t required, mi_segments_tld_t* tld, mi_os_tld_t* os_tld, mi_page_t** huge_page)
{
  // calculate needed sizes first
  size_t info_slices;
  size_t pre_size;
  size_t segment_slices = mi_segment_calculate_slices(required, &pre_size, &info_slices);
  size_t slice_entries = (segment_slices > MI_SLICES_PER_SEGMENT ? MI_SLICES_PER_SEGMENT : segment_slices);
  size_t segment_size = segment_slices * MI_SEGMENT_SLICE_SIZE;

  // Commit eagerly only if not the first N lazy segments (to reduce impact of many threads that allocate just a little)
  size_t lazy = (size_t)mi_option_get(mi_option_lazy_commit);
  bool commit_lazy = (lazy > tld->count) && required == 0; // lazy, and not a huge page

  // Try to get from our cache first
  mi_segment_t* segment = mi_segment_cache_pop(segment_slices, tld);
  if (segment==NULL) {
    // Allocate the segment from the OS
    segment = (mi_segment_t*)_mi_os_alloc_aligned(segment_size, MI_SEGMENT_SIZE, !commit_lazy, /* &memid,*/ os_tld);
    if (segment == NULL) return NULL;  // failed to allocate
    mi_assert_internal(segment != NULL && (uintptr_t)segment % MI_SEGMENT_SIZE == 0);
    if (commit_lazy) {
      // at least commit the info slices
      mi_assert_internal(MI_COMMIT_SIZE > info_slices*MI_SEGMENT_SLICE_SIZE);
      _mi_os_commit(segment, MI_COMMIT_SIZE, tld->stats);
    }
    mi_segments_track_size((long)(segment_size), tld);
    mi_segment_map_allocated_at(segment);
  }

  // zero the segment info? -- not needed as it is zero initialized from the OS 
  // memset(segment, 0, info_size);  

  
  // initialize segment info
  memset(segment,0,offsetof(mi_segment_t,slices));  
  segment->segment_slices = segment_slices;
  segment->segment_info_slices = info_slices;
  segment->thread_id = _mi_thread_id();
  segment->cookie = _mi_ptr_cookie(segment);
  segment->slice_entries = slice_entries;
  segment->kind = (required == 0 ? MI_SEGMENT_NORMAL : MI_SEGMENT_HUGE);
  segment->allow_decommit = commit_lazy;
  segment->commit_mask = (commit_lazy ? 0x01 : ~((uintptr_t)0)); // on lazy commit, the initial part is always committed
  memset(segment->slices, 0, sizeof(mi_slice_t)*(info_slices+1));
  _mi_stat_increase(&tld->stats->page_committed, mi_segment_info_size(segment));

  // set up guard pages
  if (mi_option_is_enabled(mi_option_secure)) {
    // in secure mode, we set up a protected page in between the segment info
    // and the page data
    size_t os_page_size = _mi_os_page_size();    
    mi_assert_internal(mi_segment_info_size(segment) - os_page_size >= pre_size);
    _mi_os_protect((uint8_t*)segment + mi_segment_info_size(segment) - os_page_size, os_page_size);
    uint8_t* end = (uint8_t*)segment + mi_segment_size(segment) - os_page_size;
    mi_segment_ensure_committed(segment, end, os_page_size, tld->stats);
    _mi_os_protect(end, os_page_size);
    if (slice_entries == segment_slices) segment->slice_entries--; // don't use the last slice :-(
  }

  // reserve first slices for segment info
  mi_segment_span_allocate(segment, 0, info_slices, tld);
  mi_assert_internal(segment->used == 1);
  segment->used = 0; // don't count our internal slices towards usage
  
  // initialize initial free pages
  if (segment->kind == MI_SEGMENT_NORMAL) { // not a huge page
    mi_assert_internal(huge_page==NULL);
    mi_segment_span_free(segment, info_slices, segment->slice_entries - info_slices, tld);
  }
  else {
    mi_assert_internal(huge_page!=NULL);
    *huge_page = mi_segment_span_allocate(segment, info_slices, segment_slices - info_slices, tld);
  }

  return segment;
}


static void mi_segment_free(mi_segment_t* segment, bool force, mi_segments_tld_t* tld) {
  mi_assert_internal(segment != NULL);
  mi_assert_internal(segment->next == NULL);
  mi_assert_internal(segment->used == 0);

  // Remove the free pages
  mi_slice_t* slice = &segment->slices[0];
  const mi_slice_t* end = mi_segment_slices_end(segment);
  size_t page_count = 0;
  while (slice < end) {
    mi_assert_internal(slice->slice_count > 0);
    mi_assert_internal(slice->slice_offset == 0);
    mi_assert_internal(mi_slice_index(slice)==0 || slice->block_size == 0); // no more used pages ..
    if (slice->block_size == 0 && segment->kind != MI_SEGMENT_HUGE) {
      mi_segment_span_remove_from_queue(slice, tld);
    }
    page_count++;
    slice = slice + slice->slice_count;
  }
  mi_assert_internal(page_count == 2); // first page is allocated by the segment itself

  // stats
  _mi_stat_decrease(&tld->stats->page_committed, mi_segment_info_size(segment));

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

static mi_page_t* mi_segments_page_alloc(mi_page_kind_t page_kind, size_t required, mi_segments_tld_t* tld, mi_os_tld_t* os_tld)
{
  mi_assert_internal(required <= MI_LARGE_OBJ_SIZE_MAX && page_kind <= MI_PAGE_LARGE);

  // find a free page
  size_t page_size = _mi_align_up(required,(required > MI_MEDIUM_PAGE_SIZE ? MI_MEDIUM_PAGE_SIZE : MI_SEGMENT_SLICE_SIZE));
  size_t slices_needed = page_size / MI_SEGMENT_SLICE_SIZE;
  mi_page_t* page = mi_segments_page_find_and_allocate(slices_needed,tld); //(required <= MI_SMALL_SIZE_MAX ? 0 : slices_needed), tld);
  if (page==NULL) {
    // no free page, allocate a new segment and try again
    if (mi_segment_alloc(0, tld, os_tld, NULL) == NULL) return NULL;  // OOM
    return mi_segments_page_alloc(page_kind, required, tld, os_tld);
  }
  mi_assert_internal(page != NULL && page->slice_count*MI_SEGMENT_SLICE_SIZE == page_size);
  mi_assert_internal(_mi_ptr_segment(page)->thread_id == _mi_thread_id());  
  return page;
}



/* -----------------------------------------------------------
   Page Free
----------------------------------------------------------- */

static void mi_segment_abandon(mi_segment_t* segment, mi_segments_tld_t* tld);

static mi_slice_t* mi_segment_page_clear(mi_page_t* page, mi_segments_tld_t* tld) {
  mi_assert_internal(page->block_size > 0);
  mi_assert_internal(mi_page_all_free(page));
  mi_segment_t* segment = _mi_ptr_segment(page);
  
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
  uint32_t slice_count = page->slice_count; // don't clear the slice_count
  bool is_reset = page->is_reset;         // don't clear the reset flag
  bool is_committed = page->is_committed; // don't clear the commit flag
  memset(page, 0, sizeof(*page));
  page->slice_count = slice_count;
  page->is_reset = is_reset;
  page->is_committed = is_committed;
  page->block_size = 1;

  // and free it
  return mi_segment_span_free_coalesce(mi_page_to_slice(page), tld);  
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
  const mi_slice_t* end = mi_segment_slices_end(segment);
  while (slice < end) {
    mi_assert_internal(slice->slice_count > 0);
    mi_assert_internal(slice->slice_offset == 0);
    if (slice->block_size == 0) { // a free page
      mi_segment_span_remove_from_queue(slice,tld);
      slice->block_size = 0; // but keep it free
    }
    slice = slice + slice->slice_count;
  }

  // add it to the abandoned list
  _mi_stat_increase(&tld->stats->segments_abandoned, 1);
  mi_segments_track_size(-((long)mi_segment_size(segment)), tld);
  segment->thread_id = 0;
  mi_segment_t* next;
  do {
    next = (mi_segment_t*)abandoned;
    mi_atomic_write_ptr((volatile void**)&segment->abandoned_next, next);
  } while (!mi_atomic_compare_exchange_ptr((volatile void**)&abandoned, segment, next));
  mi_atomic_increment(&abandoned_count);
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
    } while(segment != NULL && !mi_atomic_compare_exchange_ptr((volatile void**)&abandoned, (mi_segment_t*)segment->abandoned_next, segment));
    if (segment==NULL) break; // stop early if no more segments available

    // got it.
    mi_atomic_decrement(&abandoned_count);
    mi_assert_expensive(mi_segment_is_valid(segment, tld));
    segment->abandoned_next = NULL;
    segment->thread_id = _mi_thread_id();
    mi_segments_track_size((long)mi_segment_size(segment),tld);
    mi_assert_internal(segment->next == NULL);
    _mi_stat_decrease(&tld->stats->segments_abandoned,1);

    mi_slice_t* slice = &segment->slices[0];
    const mi_slice_t* end = mi_segment_slices_end(segment);
    mi_assert_internal(slice->slice_count>0 && slice->block_size>0); // segment allocated page
    slice = slice + slice->slice_count; // skip the first segment allocated page
    while (slice < end) {
      mi_assert_internal(slice->slice_count > 0);
      mi_assert_internal(slice->slice_offset == 0);
      if (slice->block_size == 0) { // a free page, add it to our lists
        mi_segment_span_add_free(slice,tld);
      }
      slice = slice + slice->slice_count;
    }

    slice = &segment->slices[0];
    mi_assert_internal(slice->slice_count>0 && slice->block_size>0); // segment allocated page
    slice = slice + slice->slice_count; // skip the first segment allocated page
    while (slice < end) {
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
          _mi_page_reclaim(heap,page);
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
   Huge page allocation
----------------------------------------------------------- */

static mi_page_t* mi_segment_huge_page_alloc(size_t size, mi_segments_tld_t* tld, mi_os_tld_t* os_tld)
{
  mi_page_t* page = NULL;
  mi_segment_t* segment = mi_segment_alloc(size,tld,os_tld,&page);
  if (segment == NULL || page==NULL) return NULL;
  mi_assert_internal(segment->used==1);
  mi_assert_internal(page->block_size >= size);
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
  if (block_size <= MI_SMALL_OBJ_SIZE_MAX) {// || mi_is_good_fit(block_size,MI_SMALL_PAGE_SIZE)) {
    page = mi_segments_page_alloc(MI_PAGE_SMALL,block_size,tld,os_tld);
  }
  else if (block_size <= MI_MEDIUM_OBJ_SIZE_MAX) {// || mi_is_good_fit(block_size, MI_MEDIUM_PAGE_SIZE)) {
    page = mi_segments_page_alloc(MI_PAGE_MEDIUM,MI_MEDIUM_PAGE_SIZE,tld, os_tld);
  }
  else if (block_size <= MI_LARGE_OBJ_SIZE_MAX) {
    page = mi_segments_page_alloc(MI_PAGE_LARGE,block_size,tld, os_tld);
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
  We maintain a bitmap of all memory with 1 bit per MI_SEGMENT_SIZE (64MiB)
  set to 1 if it contains the segment meta data.
----------------------------------------------------------- */

#if (MI_INTPTR_SIZE==8)
#define MI_MAX_ADDRESS    ((size_t)20 << 40)  // 20TB
#else
#define MI_MAX_ADDRESS    ((size_t)2 << 30)   // 2Gb
#endif

#define MI_SEGMENT_MAP_BITS  (MI_MAX_ADDRESS / MI_SEGMENT_SIZE)
#define MI_SEGMENT_MAP_SIZE  (MI_SEGMENT_MAP_BITS / 8)
#define MI_SEGMENT_MAP_WSIZE (MI_SEGMENT_MAP_SIZE / MI_INTPTR_SIZE)

static volatile uintptr_t mi_segment_map[MI_SEGMENT_MAP_WSIZE];  // 2KiB per TB with 64MiB segments

static size_t mi_segment_map_index_of(const mi_segment_t* segment, size_t* bitidx) {
  mi_assert_internal(_mi_ptr_segment(segment) == segment); // is it aligned on MI_SEGMENT_SIZE?
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
  // fast path: for any pointer to valid small/medium/large object or first MI_SEGMENT_SIZE in huge
  if (mi_likely((mi_segment_map[index] & ((uintptr_t)1 << bitidx)) != 0)) {
    return segment; // yes, allocated by us
  }
  if (index==0) return NULL;
  // search downwards for the first segment in case it is an interior pointer
  // could be slow but searches in MI_INTPTR_SIZE * MI_SEGMENT_SIZE (4GiB) steps trough 
  // valid huge objects
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
  if (((uint8_t*)segment + mi_segment_size(segment)) <= (uint8_t*)p) return NULL; // outside the range
  mi_assert_internal(p >= (void*)segment && (uint8_t*)p < (uint8_t*)segment + mi_segment_size(segment));
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
