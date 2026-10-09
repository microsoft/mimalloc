/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_INTERNAL_PRIM_H
#define MI_INTERNAL_PRIM_H

#include "../types.h"

bool          _mi_is_redirected(void);
bool          _mi_allocator_init(const char** message);
void          _mi_allocator_done(void);

#endif
