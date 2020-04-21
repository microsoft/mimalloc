/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#define MI_DEBUG_NO_SOURCE_LOC
#include "mimalloc.h"
#include "mimalloc-internal.h"

#include <string.h>  // memset, memcpy

// ------------------------------------------------------
// Aligned Allocation
// ------------------------------------------------------

static mi_decl_restrict void* mi_base_malloc_zero_aligned_at(mi_heap_t* const heap, const size_t size, const size_t alignment, const size_t offset, const bool zero   MI_SOURCE_XPARAM) mi_attr_noexcept {
  // note: we don't require `size > offset`, we just guarantee that
  // the address at offset is aligned regardless of the allocated size.
  mi_assert(alignment > 0 && alignment % sizeof(void*) == 0);

  if (mi_unlikely(size > PTRDIFF_MAX)) return NULL;   // we don't allocate more than PTRDIFF_MAX (see <https://sourceware.org/ml/libc-announce/2019/msg00001.html>)
  if (mi_unlikely(alignment==0 || !_mi_is_power_of_two(alignment))) return NULL; // require power-of-two (see <https://en.cppreference.com/w/c/memory/aligned_alloc>)
  const uintptr_t align_mask = alignment-1;  // for any x, `(x & align_mask) == (x % alignment)`
  
  // try if there is a small block available with just the right alignment
  const size_t extra_padding = mi_extra_padding(heap);
  const size_t padsize = size + extra_padding;        // safe for overflow as size <= PTRDIFF_MAX 
  if (mi_likely(padsize <= MI_SMALL_SIZE_MAX)) {
    mi_page_t* page = _mi_heap_get_free_small_page(heap,padsize);
    const bool is_aligned = (((uintptr_t)page->free+offset) & align_mask)==0;
    if (mi_likely(page->free != NULL && is_aligned))
    {
      #if MI_STAT>1
      mi_heap_stat_increase( heap, malloc, padsize);
      #endif
      void* p = _mi_page_malloc(heap,page,padsize MI_SOURCE_XARG); // TODO: inline _mi_page_malloc
      mi_assert_internal(p != NULL);
      mi_assert_internal(((uintptr_t)p + offset) % alignment == 0);
      if (zero) _mi_block_zero_init(page,p,size);
      return p;
    }
  }

  // use regular allocation if it is guaranteed to fit the alignment constraints
  if (offset==0 && alignment<=padsize && padsize<=MI_MEDIUM_OBJ_SIZE_MAX && (padsize&align_mask)==0) {
    void* p = _mi_base_malloc_zero(heap, size, zero  MI_SOURCE_XARG); // base malloc adds padding again to size
    mi_assert_internal(p == NULL || ((uintptr_t)p % alignment) == 0);
    return p;
  }
  
  // otherwise over-allocate
  void* p = _mi_base_malloc_zero(heap, size + alignment - 1, zero  MI_SOURCE_XARG);
  if (p == NULL) return NULL;

  // .. and align within the allocation
  uintptr_t adjust = alignment - (((uintptr_t)p + offset) & align_mask);
  mi_assert_internal(adjust % sizeof(uintptr_t) == 0);
  void* aligned_p = (adjust == alignment ? p : (void*)((uintptr_t)p + adjust));
  if (aligned_p != p) mi_page_set_has_aligned(_mi_ptr_page(p), true); 
  mi_assert_internal(((uintptr_t)aligned_p + offset) % alignment == 0);
  mi_assert_internal( p == _mi_page_ptr_unalign(_mi_ptr_segment(aligned_p),_mi_ptr_page(aligned_p),aligned_p) );
  return aligned_p;
}


MI_ALLOC_API3(mi_decl_restrict void*, malloc_aligned_at, mi_heap_t*, heap, size_t, size, size_t, alignment, size_t, offset)
{
  return mi_base_malloc_zero_aligned_at(heap, size, alignment, offset, false  MI_SOURCE_XARG);
}

MI_ALLOC_API2(mi_decl_restrict void*, malloc_aligned, mi_heap_t*,heap, size_t, size, size_t, alignment)
{
  return mi_base_malloc_zero_aligned_at(heap, size, alignment, 0, false  MI_SOURCE_XARG);
}

MI_ALLOC_API3(mi_decl_restrict void*, zalloc_aligned_at, mi_heap_t*, heap, size_t, size, size_t, alignment, size_t, offset)
{
  return mi_base_malloc_zero_aligned_at(heap, size, alignment, offset, true  MI_SOURCE_XARG);
}

MI_ALLOC_API2(mi_decl_restrict void*, zalloc_aligned, mi_heap_t*,heap, size_t, size, size_t, alignment)
{
  return mi_base_malloc_zero_aligned_at(heap, size, alignment, 0, true  MI_SOURCE_XARG);
}

MI_ALLOC_API4(mi_decl_restrict void*, calloc_aligned_at, mi_heap_t*, heap, size_t, count, size_t, size, size_t, alignment, size_t, offset)
{
  size_t total;
  if (mi_count_size_overflow(count, size, &total)) return NULL;
  return mi_base_malloc_zero_aligned_at(heap, total, alignment, offset, true  MI_SOURCE_XARG);
}

MI_ALLOC_API3(mi_decl_restrict void*, calloc_aligned, mi_heap_t*, heap, size_t, count, size_t, size, size_t, alignment)
{
  size_t total;
  if (mi_count_size_overflow(count, size, &total)) return NULL;
  return mi_base_malloc_zero_aligned_at(heap, total, alignment, 0, true  MI_SOURCE_XARG);
}


static void* mi_base_realloc_zero_aligned_at(mi_heap_t* heap, void* p, size_t newsize, size_t alignment, size_t offset, bool zero  MI_SOURCE_XPARAM) mi_attr_noexcept {
  mi_assert(alignment > 0);
  if (alignment <= sizeof(uintptr_t)) return _mi_base_realloc_zero(heap,p,newsize,zero  MI_SOURCE_XARG);
  if (p == NULL) return mi_base_malloc_zero_aligned_at(heap,newsize,alignment,offset,zero  MI_SOURCE_XARG);
  size_t size = mi_usable_size(p);
  if (newsize <= size && newsize >= (size - (size / 2))
      && (((uintptr_t)p + offset) % alignment) == 0) {
    return p;  // reallocation still fits, is aligned and not more than 50% waste
  }
  else {
    void* newp = mi_base_malloc_aligned_at(heap,newsize,alignment,offset  MI_SOURCE_XARG);
    if (newp != NULL) {
      if (zero && newsize > size) {
        const mi_page_t* page = _mi_ptr_page(newp);
        if (page->is_zero) {
          // already zero initialized
          mi_assert_expensive(mi_mem_is_zero(newp,newsize));
        }
        else {
          // also set last word in the previous allocation to zero to ensure any padding is zero-initialized
          size_t start = (size >= sizeof(intptr_t) ? size - sizeof(intptr_t) : 0);
          memset((uint8_t*)newp + start, 0, newsize - start);
        }
      }
      memcpy(newp, p, (newsize > size ? size : newsize));
      mi_free(p); // only free if successful
    }
    return newp;
  }
}

static void* mi_base_realloc_zero_aligned(mi_heap_t* heap, void* p, size_t newsize, size_t alignment, bool zero  MI_SOURCE_XPARAM) mi_attr_noexcept {
  mi_assert(alignment > 0);
  if (alignment <= sizeof(uintptr_t)) return _mi_base_realloc_zero(heap,p,newsize,zero  MI_SOURCE_XARG);
  size_t offset = ((uintptr_t)p % alignment); // use offset of previous allocation (p can be NULL)
  return mi_base_realloc_zero_aligned_at(heap,p,newsize,alignment,offset,zero  MI_SOURCE_XARG);
}


MI_ALLOC_API4(void*, realloc_aligned_at, mi_heap_t*, heap, void*, p, size_t, newsize, size_t, alignment, size_t, offset)
{
  return mi_base_realloc_zero_aligned_at(heap,p,newsize,alignment,offset,false  MI_SOURCE_XARG);
}

MI_ALLOC_API3(void*, realloc_aligned, mi_heap_t*, heap, void*, p, size_t, newsize, size_t, alignment)
{
  return mi_base_realloc_zero_aligned(heap,p,newsize,alignment,false  MI_SOURCE_XARG);
}

MI_ALLOC_API4(void*, rezalloc_aligned_at, mi_heap_t*, heap, void*, p, size_t, newsize, size_t, alignment, size_t, offset)
{
  return mi_base_realloc_zero_aligned_at(heap, p, newsize, alignment, offset, true  MI_SOURCE_XARG);
}

MI_ALLOC_API3(void*, rezalloc_aligned, mi_heap_t*, heap, void*, p, size_t, newsize, size_t, alignment)
{
  return mi_base_realloc_zero_aligned(heap, p, newsize, alignment, true  MI_SOURCE_XARG);
}

MI_ALLOC_API5(void*, recalloc_aligned_at, mi_heap_t*, heap, void*, p, size_t, newcount, size_t, size, size_t, alignment, size_t, offset)
{
  size_t total;
  if (mi_count_size_overflow(newcount, size, &total)) return NULL;
  return mi_base_realloc_zero_aligned_at(heap, p, total, alignment, offset, true  MI_SOURCE_XARG);
}


MI_ALLOC_API4(void*, recalloc_aligned, mi_heap_t*, heap, void*, p, size_t, newcount, size_t, size, size_t, alignment)
{
  size_t total;
  if (mi_count_size_overflow(newcount, size, &total)) return NULL;
  return mi_base_realloc_zero_aligned_at(heap, p, total, alignment, 0, true  MI_SOURCE_XARG);
}
