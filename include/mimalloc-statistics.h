/* ----------------------------------------------------------------------------
Copyright (c) 2018-2020 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_STATISTICS_H
#define MIMALLOC_STATISTICS_H

#include "mimalloc/stats.h"
#include "mimalloc.h"

mi_decl_export const mi_stats_t*  mi_thread_stats(void) mi_attr_noexcept;
mi_decl_export const mi_stats_t*  mi_thread_heap_stats(const mi_heap_t* heap) mi_attr_noexcept;

#endif
