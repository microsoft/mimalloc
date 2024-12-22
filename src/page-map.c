/*----------------------------------------------------------------------------
Copyright (c) 2023-2024, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "bitmap.h"

#if MI_PAGE_MAP_FLAT

// The page-map contains a byte for each 64kb slice in the address space. 
// For an address `a` where `n = _mi_page_map[a >> 16]`:
// 0 = unused
// 1 = the slice at `a & ~0xFFFF` is a mimalloc page.
// 1 < n << 127 = the slice is part of a page, starting at `(((a>>16) - n - 1) << 16)`.
// 
// 1 byte per slice => 1 GiB page map = 2^30 slices of 2^16 = 2^46 = 64 TiB address space.
// 4 GiB virtual for 256 TiB address space (48 bit) (and 64 KiB for 4 GiB address space (on 32-bit)).

// 1MiB = 2^20*2^16 = 2^36 = 64GiB address space
// 2^12 pointers = 2^15 k = 32k
mi_decl_cache_align uint8_t* _mi_page_map = NULL;
static bool        mi_page_map_all_committed = false;
static size_t      mi_page_map_entries_per_commit_bit = MI_ARENA_SLICE_SIZE;
static void*       mi_page_map_max_address = NULL;
static mi_memid_t  mi_page_map_memid;


// (note: we need to initialize statically or otherwise C++ may run a default constructors after process initialization)
sstatic mi_bitmap_t mi_page_map_commit = { MI_ATOMIC_VAR_INIT(MI_BITMAP_DEFAULT_CHUNK_COUNT), MI_ATOMIC_VAR_INIT(0),
                                          { 0 }, { {MI_ATOMIC_VAR_INIT(0)} }, {{{ MI_ATOMIC_VAR_INIT(0) }}} };

bool _mi_page_map_init(void) {
  size_t vbits = (size_t)mi_option_get_clamp(mi_option_max_vabits, 0, MI_SIZE_BITS);  
  if (vbits == 0) {
    vbits = _mi_os_virtual_address_bits();
    #if MI_ARCH_X64
    if (vbits >= 48) { vbits = 47; }
    #endif
  }
  
  mi_page_map_max_address = (void*)(MI_PU(1) << vbits);
  const size_t page_map_size = (MI_ZU(1) << (vbits - MI_ARENA_SLICE_SHIFT));

  mi_page_map_entries_per_commit_bit = _mi_divide_up(page_map_size, MI_BITMAP_DEFAULT_BIT_COUNT);
  // mi_bitmap_init(&mi_page_map_commit, MI_BITMAP_MIN_BIT_COUNT, true);

  mi_page_map_all_committed = (page_map_size <= 1*MI_MiB || mi_option_is_enabled(mi_option_debug_commit_full_pagemap)); // _mi_os_has_overcommit(); // commit on-access on Linux systems?
  _mi_page_map = (uint8_t*)_mi_os_alloc_aligned(page_map_size, 1, mi_page_map_all_committed, true, &mi_page_map_memid);
  if (_mi_page_map==NULL) {
    _mi_error_message(ENOMEM, "unable to reserve virtual memory for the page map (%zu KiB)\n", page_map_size / MI_KiB);
    return false;
  }
  if (mi_page_map_memid.initially_committed && !mi_page_map_memid.initially_zero) {
    _mi_warning_message("the page map was committed but not zero initialized!\n");
    _mi_memzero_aligned(_mi_page_map, page_map_size);
  }
  // commit the first part so NULL pointers get resolved without an access violation
  if (!mi_page_map_all_committed) {
    bool is_zero;
    _mi_os_commit(_mi_page_map, _mi_os_page_size(), &is_zero);
    if (!is_zero && !mi_page_map_memid.initially_zero) { _mi_memzero(_mi_page_map, _mi_os_page_size()); }
  }
  _mi_page_map[0] = 1; // so _mi_ptr_page(NULL) == NULL
  mi_assert_internal(_mi_ptr_page(NULL)==NULL);
  return true;
}

static void mi_page_map_ensure_committed(size_t idx, size_t slice_count) {
  // is the page map area that contains the page address committed?  
  // we always set the commit bits so we can track what ranges are in-use.
  // we only actually commit if the map wasn't committed fully already.
  const size_t commit_bit_idx_lo = idx / mi_page_map_entries_per_commit_bit;
  const size_t commit_bit_idx_hi = (idx + slice_count - 1) / mi_page_map_entries_per_commit_bit;
  for (size_t i = commit_bit_idx_lo; i <= commit_bit_idx_hi; i++) {  // per bit to avoid crossing over bitmap chunks
    if (mi_bitmap_is_clearN(&mi_page_map_commit, i, 1)) {
      // this may race, in which case we do multiple commits (which is ok)
      if (!mi_page_map_all_committed) {
        bool is_zero;
        uint8_t* const start = _mi_page_map + (i*mi_page_map_entries_per_commit_bit);
        const size_t   size = mi_page_map_entries_per_commit_bit;
        _mi_os_commit(start, size, &is_zero);
        if (!is_zero && !mi_page_map_memid.initially_zero) { _mi_memzero(start, size); }
      }
      mi_bitmap_set(&mi_page_map_commit, i);
    }
  }
  #if MI_DEBUG > 0
  _mi_page_map[idx] = 0;
  _mi_page_map[idx+slice_count-1] = 0;
  #endif  
}

static size_t mi_page_map_get_idx(mi_page_t* page, uint8_t** page_start, size_t* slice_count) {
  size_t page_size;
  *page_start = mi_page_area(page, &page_size);
  if (page_size > MI_LARGE_PAGE_SIZE) { page_size = MI_LARGE_PAGE_SIZE - MI_ARENA_SLICE_SIZE; }  // furthest interior pointer
  *slice_count = mi_slice_count_of_size(page_size) + (((uint8_t*)*page_start - (uint8_t*)page)/MI_ARENA_SLICE_SIZE); // add for large aligned blocks
  return _mi_page_map_index(page);
}



void _mi_page_map_register(mi_page_t* page) {
  mi_assert_internal(page != NULL);
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(_mi_page_map != NULL);  // should be initialized before multi-thread access!
  if mi_unlikely(_mi_page_map == NULL) {
    if (!_mi_page_map_init()) return;
  }
  mi_assert(_mi_page_map!=NULL);
  uint8_t* page_start;
  size_t   slice_count;
  const size_t idx = mi_page_map_get_idx(page, &page_start, &slice_count);

  mi_page_map_ensure_committed(idx, slice_count);

  // set the offsets
  for (size_t i = 0; i < slice_count; i++) {
    mi_assert_internal(i < 128);
    _mi_page_map[idx + i] = (uint8_t)(i+1);
  }
}


void _mi_page_map_unregister(mi_page_t* page) {
  mi_assert_internal(_mi_page_map != NULL);
  // get index and count
  uint8_t* page_start;
  size_t   slice_count;
  const size_t idx = mi_page_map_get_idx(page, &page_start, &slice_count);
  // unset the offsets
  _mi_memzero(_mi_page_map + idx, slice_count);
}

void _mi_page_map_unregister_range(void* start, size_t size) {
  const size_t slice_count = _mi_divide_up(size, MI_ARENA_SLICE_SIZE);
  const uintptr_t index = _mi_page_map_index(start);
  mi_page_map_ensure_committed(index, slice_count); // we commit the range in total; todo: scan the commit bits and clear only those ranges?
  _mi_memzero(&_mi_page_map[index], slice_count);
}

mi_decl_nodiscard mi_decl_export bool mi_is_in_heap_region(const void* p) mi_attr_noexcept {
  // if mi_unlikely(_mi_page_map==NULL) {  // happens on macOS during loading
  //   _mi_page_map_init();  
  // }
  if mi_unlikely(p >= mi_page_map_max_address) return false;
  uintptr_t idx = ((uintptr_t)p >> MI_ARENA_SLICE_SHIFT);
  if (mi_page_map_all_committed || mi_bitmap_is_setN(&mi_page_map_commit, idx/mi_page_map_entries_per_commit_bit, 1)) {
    return (_mi_page_map[idx] != 0);
  }
  else {
    return false;
  }
}

#else 

mi_decl_cache_align uint8_t** _mi_page_map = NULL;

static void*       mi_page_map_max_address = NULL;
static mi_memid_t  mi_page_map_memid;

bool _mi_page_map_init(void) {
  size_t vbits = (size_t)mi_option_get_clamp(mi_option_max_vabits, 0, MI_SIZE_BITS);
  if (vbits == 0) {
    vbits = _mi_os_virtual_address_bits();
    mi_assert_internal(vbits <= MI_MAX_VABITS);
  }

  mi_page_map_max_address = (void*)(MI_PU(1) << vbits);
  const size_t os_page_size = _mi_os_page_size();
  const size_t page_map_size = _mi_align_up(MI_ZU(1) << (vbits - MI_PAGE_MAP_SUB_SHIFT - MI_ARENA_SLICE_SHIFT + MI_INTPTR_SHIFT), os_page_size);
  const size_t reserve_size = page_map_size + (2 * MI_PAGE_MAP_SUB_SIZE);  
  _mi_page_map = (uint8_t**)_mi_os_alloc_aligned(reserve_size, 1, true /* commit */, true, &mi_page_map_memid);
  if (_mi_page_map==NULL) {
    _mi_error_message(ENOMEM, "unable to reserve virtual memory for the page map (%zu KiB)\n", reserve_size / MI_KiB);
    return false;
  }
  if (mi_page_map_memid.initially_committed && !mi_page_map_memid.initially_zero) {
    _mi_warning_message("the page map was committed but not zero initialized!\n");
    _mi_memzero_aligned(_mi_page_map, reserve_size);
  }

  uint8_t* sub0 = (uint8_t*)_mi_page_map + page_map_size;
  uint8_t* sub1 = sub0 + MI_PAGE_MAP_SUB_SIZE;
  // initialize the first part so NULL pointers get resolved without an access violation
  _mi_page_map[0] = sub0; 
  sub0[0] = 1;                // so _mi_ptr_page(NULL) == NULL
  // and initialize the 4GiB range where we were allocated 
  _mi_page_map[_mi_page_map_index(_mi_page_map,NULL)] = sub1;

  mi_assert_internal(_mi_ptr_page(NULL)==NULL);
  return true;
}

static size_t mi_page_map_get_idx(mi_page_t* page, uint8_t** page_start, size_t* sub_idx, size_t* slice_count) {
  size_t page_size;
  *page_start = mi_page_area(page, &page_size);
  if (page_size > MI_LARGE_PAGE_SIZE) { page_size = MI_LARGE_PAGE_SIZE - MI_ARENA_SLICE_SIZE; }  // furthest interior pointer
  *slice_count = mi_slice_count_of_size(page_size) + (((uint8_t*)*page_start - (uint8_t*)page)/MI_ARENA_SLICE_SIZE); // add for large aligned blocks
  return _mi_page_map_index(page,sub_idx);
}


static inline void mi_page_map_set_range(size_t idx, size_t sub_idx, size_t slice_count, uint8_t (*set)(uint8_t ofs)) {
  // is the page map area that contains the page address committed?
  uint8_t ofs = 1;
  while (slice_count > 0) {
    uint8_t* sub = _mi_page_map[idx];
    if (sub == NULL) {
      mi_memid_t memid;
      sub = (uint8_t*)_mi_os_alloc(MI_PAGE_MAP_SUB_SIZE, &memid);
      uint8_t* expect = NULL;
      if (!mi_atomic_cas_strong_acq_rel(((_Atomic(uint8_t*)*)&_mi_page_map[idx]), &expect, sub)) {
        _mi_os_free(sub, MI_PAGE_MAP_SUB_SIZE, memid);
        sub = expect;
        mi_assert_internal(sub!=NULL);
      }
      if (sub == NULL) {
        _mi_error_message(EFAULT, "internal error: unable to extend the page map\n");
        return; // abort?
      }
    }
    // set the offsets for the page
    while (sub_idx < MI_PAGE_MAP_SUB_SIZE && slice_count > 0) {
      sub[sub_idx] = set(ofs);
      sub_idx++;
      ofs++;
      slice_count--;
    }
    sub_idx = 0; // potentially wrap around to the next idx    
  }  
}

static uint8_t set_ofs(uint8_t ofs) {
  return ofs;
}

void _mi_page_map_register(mi_page_t* page) {
  mi_assert_internal(page != NULL);
  mi_assert_internal(_mi_is_aligned(page, MI_PAGE_ALIGN));
  mi_assert_internal(_mi_page_map != NULL);  // should be initialized before multi-thread access!
  if mi_unlikely(_mi_page_map == NULL) {
    if (!_mi_page_map_init()) return;
  }
  mi_assert(_mi_page_map!=NULL);
  uint8_t* page_start;
  size_t   slice_count;
  size_t   sub_idx;
  const size_t idx = mi_page_map_get_idx(page, &page_start, &sub_idx, &slice_count);
  mi_page_map_set_range(idx, sub_idx, slice_count, &set_ofs);
}

static uint8_t set_zero(uint8_t ofs) {
  MI_UNUSED(ofs);
  return 0;
}


void _mi_page_map_unregister(mi_page_t* page) {
  mi_assert_internal(_mi_page_map != NULL);
  // get index and count
  uint8_t* page_start;
  size_t   slice_count;
  size_t   sub_idx;
  const size_t idx = mi_page_map_get_idx(page, &page_start, &sub_idx, &slice_count);
  // unset the offsets
  mi_page_map_set_range(idx, sub_idx, slice_count, &set_zero);
}

void _mi_page_map_unregister_range(void* start, size_t size) {
  const size_t slice_count = _mi_divide_up(size, MI_ARENA_SLICE_SIZE);
  size_t sub_idx;
  const size_t idx = _mi_page_map_index(start, &sub_idx);
  mi_page_map_set_range(idx, sub_idx, slice_count, &set_zero);
}

mi_decl_nodiscard mi_decl_export bool mi_is_in_heap_region(const void* p) mi_attr_noexcept {

  if mi_unlikely(p >= mi_page_map_max_address) return false;
  size_t sub_idx;
  const size_t idx = _mi_page_map_index(p, &sub_idx);
  uint8_t* sub = _mi_page_map[idx];  
  if (sub != NULL) {
    return (sub[sub_idx] != 0);
  }
  else {
    return false;
  }
}


#endif

