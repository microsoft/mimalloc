/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_TRACK_H
#define MIMALLOC_TRACK_H

// ------------------------------------------------------
// Track memory ranges with macros for tools like Valgrind
// or other memory checkers.
// ------------------------------------------------------


#define MI_VALGRIND 1

#if MI_VALGRIND
#include <valgrind/valgrind.h>
#include <valgrind/memcheck.h>

#define MI_TRACK_ZALLOC(p,size,zero)    VALGRIND_MALLOCLIKE_BLOCK(p,size,0 /*red zone*/,zero)
#define MI_TRACK_MALLOC(p,size)         MI_TRACK_ZALLOC(p,size,false)
#define MI_TRACK_FREE(p)                VALGRIND_FREELIKE_BLOCK(p,0 /*red zone*/)
#define MI_TRACK_MEM_DEFINED(p,size)    VALGRIND_MAKE_MEM_DEFINED(p,size)
#define MI_TRACK_MEM_UNDEFINED(p,size)  VALGRIND_MAKE_MEM_UNDEFINED(p,size)
#define MI_TRACK_MEM_NOACCESS(p,size)   VALGRIND_MAKE_MEM_NOACCESS(p,size)

#else

#define MI_TRACK_ZALLOC(p,size,zero)  
#define MI_TRACK_MALLOC(p,size)        
#define MI_TRACK_FREE(p)              
#define MI_TRACK_MEM_DEFINED(p,size)  
#define MI_TRACK_MEM_UNDEFINED(p,size)  
#define MI_TRACK_MEM_NOACCESS(p,size)  

#endif

#endif
