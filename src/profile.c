/* ----------------------------------------------------------------------------
Copyright (c) 2019-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc/internal.h"

static mi_profiler_t* mi_heap_profiler(const mi_heap_t* heap) {
  return mi_atomic_load_ptr_acquire(mi_profiler_t,&heap->profiler);
} 



/* ----------------------------------------------------------------------------
  Profile an allocation and free
-----------------------------------------------------------------------------*/
void* _mi_theap_profile_alloc(mi_theap_t* theap, mi_profiler_t* prof, size_t size, bool zero, mi_page_t** ppage) 
{
  mi_assert_internal(theap!=NULL && prof!=NULL);
  mi_assert_internal(theap->profile_allocated > theap->profile_threshold);
  const size_t allocated = theap->profile_allocated;
  theap->profile_allocated = 0;
  
  // Overallocate a larger block to store the profiler data
  // [MI_BLOCK_TAG_PROFILE] [usable size] [ ... profile data ... ] [... user data ...]
  const size_t zero_huge_alignment = (zero ? 1 : 0);
  const size_t profiler_data_offset = sizeof(mi_block_t);
  size_t profiler_data_size = sizeof(mi_profiler_data_t);
  if (prof->profiler_data_size > 2*sizeof(size_t)) { profiler_data_size = (prof->profiler_data_size > 512 ? 512 : prof->profiler_data_size); };
  const size_t profiler_user_offset = _mi_align_up(profiler_data_offset + profiler_data_size, MI_MAX_ALIGN_SIZE);
  const size_t oversize = profiler_user_offset + size;
  mi_page_t* page = NULL;
  mi_block_t* const block = (mi_block_t*)_mi_malloc_generic(theap,oversize,zero_huge_alignment,&page);  // cannot recurse as the profile_allocated is now zero
  if (block==NULL) return NULL;
  mi_assert_internal(page!=NULL);
  if (ppage!=NULL) { *ppage = page; }
  mi_assert_internal(!mi_block_ptr_is_guarded(_mi_page_ptr_unalign(page,block),block));

  // Set up the profiled block
  mi_page_set_has_interior_pointers(page, true);
  block->next = MI_BLOCK_TAG_PROFILED;  
  const size_t usable_size = _mi_page_usable_size(page,block) - profiler_user_offset;
  void* const p = (uint8_t*)block + profiler_user_offset;
  mi_profiler_data_t* profiler_data = (mi_profiler_data_t*)((uint8_t*)block + profiler_data_offset);
  profiler_data->requested_size = size - MI_PADDING_SIZE;
  profiler_data->usable_size = usable_size;

  // and call the profiler on_alloc
  if (prof->on_alloc!=NULL) { 
    const size_t new_threshold = (*prof->on_alloc)(profiler_data, p, theap->profile_threshold, allocated, _mi_theap_heap(theap), prof->profiler_arg);
    if (new_threshold!=0) { 
      theap->profile_threshold = new_threshold;
    }
    mi_theap_stat_counter_increase(theap,profile_samples,1);
  }
  return p;
}

void _mi_page_profile_free(mi_page_t* page, mi_block_t* block, void* p) {
  mi_assert_internal(mi_block_ptr_is_profiled(block,p));

  // get the heap and profiler
  mi_heap_t* const heap = mi_page_heap(page);
  if (heap==NULL) return;
  mi_profiler_t* prof = mi_heap_profiler(heap);
  if (prof==NULL || !mi_profiler_is_enabled(prof) || prof->on_free==NULL) return;
  
  // call the on_free callback
  mi_profiler_data_t* const profiler_data = (mi_profiler_data_t*)((uint8_t*)block + sizeof(mi_block_t));
  prof->on_free(profiler_data, p, heap, prof->profiler_arg);
}


/* ----------------------------------------------------------------------------
  API
-----------------------------------------------------------------------------*/

bool mi_heap_profile(mi_heap_t* heap, const mi_profiler_t* profiler) {
  // if (mi_heap_profiler(heap)!=NULL) return false;  // always overwrite?
  mi_atomic_store_ptr_release(mi_profiler_t,&heap->profiler,(mi_profiler_t*)profiler);
  return true;
}

bool mi_subproc_profile(mi_subproc_id_t subproc_id, const mi_profiler_t* profiler) {
  mi_subproc_t* subproc = _mi_subproc_from_id(subproc_id);
  if (subproc==NULL) return false; 
  // if (mi_subproc_profiler(subproc)!=NULL) return false;  // always overwrite?
  mi_atomic_store_ptr_release(mi_profiler_t,&subproc->profiler, (mi_profiler_t*)profiler);
  mi_lock(&subproc->heaps_lock) {
    for (mi_heap_t* heap = subproc->heaps; heap!=NULL; heap = heap->next) {
      mi_heap_profile(heap,profiler);
    }
  }
  return true;
}

bool mi_profile(const mi_profiler_t* profiler) {
  return mi_subproc_profile(mi_subproc_main(),profiler);
}

bool mi_profiler_start(const mi_profiler_t* profiler ) {
  return mi_profiler_set_enabled((mi_profiler_t*)profiler,true);  
}

bool mi_profiler_stop(const mi_profiler_t* profiler) {
  return mi_profiler_set_enabled((mi_profiler_t*)profiler,false);
}
