/* ----------------------------------------------------------------------------
Copyright (c) 2018,2019 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_OVERRIDE_H
#define MIMALLOC_OVERRIDE_H

/* ----------------------------------------------------------------------------
This header can be used to statically redirect malloc/free and new/delete
to the mimalloc variants. This can be useful if one can include this file on
each source file in a project (but be careful when using external code to
not accidentally mix pointers from different allocators).
-----------------------------------------------------------------------------*/

#include <mimalloc.h>

// Standard C allocation
#define malloc(n)               mi_source_malloc(n  MI_SOURCE_LOC())
#define calloc(n,c)             mi_source_calloc(n,c  MI_SOURCE_LOC())
#define realloc(p,n)            mi_source_realloc(p,n  MI_SOURCE_LOC())
#define free(p)                 mi_free(p)

#define strdup(s)               mi_source_strdup(s  MI_SOURCE_LOC())
#define strndup(s)              mi_source_strndup(s  MI_SOURCE_LOC())
#define realpath(f,n)           mi_source_realpath(f,n  MI_SOURCE_LOC())

// Microsoft extensions
#define _expand(p,n)            mi_expand(p,n)
#define _msize(p)               mi_usable_size(p)
#define _recalloc(p,n,c)        mi_source_recalloc(p,n,c  MI_SOURCE_LOC())

#define _strdup(s)              mi_source_strdup(s  MI_SOURCE_LOC())
#define _strndup(s)             mi_source_strndup(s  MI_SOURCE_LOC())
#define _wcsdup(s)              (wchar_t*)mi_source_wcsdup((const unsigned short*)(s)  MI_SOURCE_LOC())
#define _mbsdup(s)              mi_source_mbsdup(s  MI_SOURCE_LOC())
#define _dupenv_s(b,n,v)        mi_source_dupenv_s(b,n,v  MI_SOURCE_LOC())
#define _wdupenv_s(b,n,v)       mi_source_wdupenv_s((unsigned short*)(b),n,(const unsigned short*)(v)  MI_SOURCE_LOC())

// Various Posix and Unix variants
#define reallocf(p,n)           mi_source_reallocf(p,n  MI_SOURCE_LOC())
#define malloc_size(p)          mi_usable_size(p)
#define malloc_usable_size(p)   mi_usable_size(p)
#define cfree(p)                mi_free(p)

#define valloc(n)               mi_source_valloc(n  MI_SOURCE_LOC())
#define pvalloc(n)              mi_source_pvalloc(n  MI_SOURCE_LOC())
#define reallocarray(p,s,n)     mi_source_reallocarray(p,s,n  MI_SOURCE_LOC())
#define memalign(a,n)           mi_source_memalign(a,n  MI_SOURCE_LOC())
#define aligned_alloc(a,n)      mi_source_aligned_alloc(a,n  MI_SOURCE_LOC())
#define posix_memalign(p,a,n)   mi_source_posix_memalign(p,a,n  MI_SOURCE_LOC())
#define _posix_memalign(p,a,n)  mi_source_posix_memalign(p,a,n  MI_SOURCE_LOC())

// Microsoft aligned variants
#define _aligned_malloc(n,a)                  mi_source_malloc_aligned(n,a  MI_SOURCE_LOC())
#define _aligned_realloc(p,n,a)               mi_source_realloc_aligned(p,n,a  MI_SOURCE_LOC())
#define _aligned_recalloc(p,s,n,a)            mi_source_recalloc_aligned(p,s,n,a  MI_SOURCE_LOC())
#define _aligned_msize(p,a,o)                 mi_usable_size(p)
#define _aligned_free(p)                      mi_free(p)
#define _aligned_offset_malloc(n,a,o)         mi_source_malloc_aligned_at(n,a,o  MI_SOURCE_LOC())
#define _aligned_offset_realloc(p,n,a,o)      mi_source_realloc_aligned_at(p,n,a,o  MI_SOURCE_LOC())
#define _aligned_offset_recalloc(p,s,n,a,o)   mi_source_recalloc_aligned_at(p,s,n,a,o  MI_SOURCE_LOC())

// Overload new operators
// This requires including <mimalloc-new-delete.h> somewhere!
// See also <https://www.modernescpp.com/index.php/overloading-operator-new-and-delete-2>
#if !defined(NDEBUG) && defined(__cplusplus) && !defined(MI_NO_NEW_OVERRIDE)
#define new  new(mi_source_loc(__FILE__,__LINE__))
#endif

#endif // MIMALLOC_OVERRIDE_H
