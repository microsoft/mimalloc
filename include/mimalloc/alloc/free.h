/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_FREE_H
#define MI_INTERNAL_FREE_H

#include "../types.h"

mi_block_t*   _mi_page_ptr_unalign(const mi_page_t* page, const void* p);
void          _mi_padding_shrink(const mi_page_t* page, const mi_block_t* block, const size_t min_size);
void          _mi_free_subproc_safe(void* p) mi_attr_noexcept;
size_t        _mi_page_usable_size(const mi_page_t* page, const void* p) mi_attr_noexcept;
// Requires the owner, or exclusive access to a detached/quiescent theap.
void          _mi_theap_free_batch_collect(mi_theap_t* theap);
// Requires the owning thread.
void          _mi_tld_free_batch_collect(mi_tld_t* tld, bool disable);

#endif
