/*----------------------------------------------------------------------------
Copyright (c) 2023-2024, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "bitmap.h"

mi_decl_cache_align uint8_t* _mi_page_map = NULL;
static bool        mi_page_map_all_committed = false;
static size_t      mi_page_map_entries_per_commit_bit = MI_ARENA_SLICE_SIZE;
static mi_memid_t  mi_page_map_memid;
static mi_bitmap_t mi_page_map_commit;

static bool mi_page_map_init(void) {
  size_t vbits = _mi_os_virtual_address_bits();
  if (vbits >= 48) vbits = 47;
  // 1 byte per block =  2 GiB for 128 TiB address space  (48 bit = 256 TiB address space)
  //                    64 KiB for 4 GiB address space (on 32-bit)
  const size_t page_map_size = (MI_ZU(1) << (vbits - MI_ARENA_SLICE_SHIFT));

  mi_page_map_entries_per_commit_bit = _mi_divide_up(page_map_size,MI_BITMAP_MAX_BITS);

  mi_page_map_all_committed = _mi_os_has_overcommit(); // commit on-access on Linux systems
  _mi_page_map = (uint8_t*)_mi_os_alloc_aligned(page_map_size, 1, mi_page_map_all_committed, true, &mi_page_map_memid, NULL);
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
    _mi_os_commit(_mi_page_map, _mi_os_page_size(), NULL, NULL);
    _mi_page_map[0] = -1; // so _mi_ptr_page(NULL) == NULL
  }
  return true;
}

static void mi_page_map_ensure_committed(size_t idx, size_t slice_count) {
  // is the page map area that contains the page address committed?
  if (!mi_page_map_all_committed) {
    const size_t commit_bit_idx_lo = idx / mi_page_map_entries_per_commit_bit;
    const size_t commit_bit_idx_hi = (idx + slice_count - 1) / mi_page_map_entries_per_commit_bit;
    for (size_t i = commit_bit_idx_lo; i <= commit_bit_idx_hi; i++) {  // per bit to avoid crossing over bitmap chunks
      if (mi_bitmap_is_xsetN(MI_BIT_CLEAR, &mi_page_map_commit, i, 1)) {
        // this may race, in which case we do multiple commits (which is ok)
        _mi_os_commit(_mi_page_map + (i*mi_page_map_entries_per_commit_bit), mi_page_map_entries_per_commit_bit, NULL, NULL);
        mi_bitmap_xsetN(MI_BIT_SET, &mi_page_map_commit, i, 1, NULL);
      }
    }
  }
}

static size_t mi_page_map_get_idx(mi_page_t* page, uint8_t** page_start, size_t* slice_count) {
  size_t page_size;
  *page_start = mi_page_area(page, &page_size);
  if (page_size > MI_LARGE_PAGE_SIZE) { page_size = MI_LARGE_PAGE_SIZE; }  // furthest interior pointer
  *slice_count = mi_slice_count_of_size(page_size);
  return ((uintptr_t)*page_start >> MI_ARENA_SLICE_SHIFT);
}



void _mi_page_map_register(mi_page_t* page) {
  if mi_unlikely(_mi_page_map == NULL) {
    if (!mi_page_map_init()) return;
  }
  mi_assert(_mi_page_map!=NULL);
  uint8_t* page_start;
  size_t   slice_count;
  const size_t idx = mi_page_map_get_idx(page, &page_start, &slice_count);

  mi_page_map_ensure_committed(idx, slice_count);

  // set the offsets
  for (int i = 0; i < (int)slice_count; i++) {
    mi_assert_internal(i < 128);
    _mi_page_map[idx + i] = (i+1);
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


mi_decl_nodiscard mi_decl_export bool mi_is_in_heap_region(const void* p) mi_attr_noexcept {
  uintptr_t idx = ((uintptr_t)p >> MI_ARENA_SLICE_SHIFT);
  if (!mi_page_map_all_committed || mi_bitmap_is_xsetN(MI_BIT_SET, &mi_page_map_commit, idx/mi_page_map_entries_per_commit_bit, 1)) {
    return (_mi_page_map[idx] != 0);
  }
  else {
    return false;
  }
}
