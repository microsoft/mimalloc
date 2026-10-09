/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_PAGE_QUEUE_H
#define MI_INTERNAL_PAGE_QUEUE_H

#include "../types.h"

bool          _mi_page_queue_is_valid(mi_theap_t* theap, const mi_page_queue_t* pq);
size_t        _mi_page_stats_bin(const mi_page_t* page); // for stats
size_t        _mi_bin_size(size_t bin);                  // for stats
size_t        _mi_bin(size_t size);                      // for stats

static inline mi_page_queue_t* mi_page_queue(const mi_theap_t* theap, size_t size) {
  mi_page_queue_t* const pq = &((mi_theap_t*)theap)->pages[_mi_bin(size)];
  if (size <= MI_LARGE_MAX_OBJ_SIZE) { mi_assert_internal(pq->block_size <= MI_LARGE_MAX_OBJ_SIZE); }
  return pq;
}

#endif
