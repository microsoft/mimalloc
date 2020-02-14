/* ----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// ------------------------------------------------------------------------
// mi prefixed publi definitions of various Posix, Unix, and C++ functions
// for convenience and used when overriding these functions.
// ------------------------------------------------------------------------
#define  _CRT_SECURE_NO_WARNINGS
#define  MI_DEBUG_NO_SOURCE_LOC
#include "mimalloc.h"
#include "mimalloc-internal.h"

// ------------------------------------------------------
// Posix & Unix functions definitions
// ------------------------------------------------------

#include <errno.h>
#include <string.h>  // memcpy
#include <stdlib.h>  // getenv
#include <wchar.h>   // wcslen, wcrcpy

#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif


size_t mi_malloc_size(const void* p) mi_attr_noexcept {
  return mi_usable_size(p);
}

size_t mi_malloc_usable_size(const void *p) mi_attr_noexcept {
  return mi_usable_size(p);
}

void mi_cfree(void* p) mi_attr_noexcept {
  if (mi_is_in_heap_region(p)) {
    mi_free(p);
  }
}

void* mi__expand(void* p, size_t newsize) mi_attr_noexcept {  // Microsoft
  void* res = mi_expand(p, newsize);
  if (res == NULL) errno = ENOMEM;
  return res;
}

MI_SOURCE_API3(void*, reallocarray, void*, p, size_t, count, size_t, size)
{
  void* newp = MI_SOURCE_ARG(mi_reallocn, p, count, size);
  if (newp==NULL) errno = ENOMEM;
  return newp;
}

MI_SOURCE_API2(mi_decl_restrict void*, memalign, size_t, alignment, size_t, size)
{
  void* p;
  if (alignment <= MI_MAX_ALIGN_SIZE) {
    p = MI_SOURCE_ARG(mi_malloc, size);
  }
  else {
    p = MI_SOURCE_ARG(mi_malloc_aligned, size, alignment);
  }
  mi_assert_internal(((uintptr_t)p % alignment) == 0);
  return p;
}

MI_SOURCE_API1(mi_decl_restrict void*, valloc, size_t, size)
{
  return MI_SOURCE_ARG(mi_malloc_aligned, size, _mi_os_page_size());
}

MI_SOURCE_API1(mi_decl_restrict void*, pvalloc, size_t, size)
{
  size_t psize = _mi_os_page_size();
  if (size >= SIZE_MAX - psize) return NULL; // overflow
  size_t asize = _mi_align_up(size, psize); 
  return MI_SOURCE_ARG(mi_malloc_aligned, asize, psize);
}

MI_SOURCE_API2(mi_decl_restrict void*, aligned_alloc, size_t, alignment, size_t, size)
{
  if (alignment==0 || !_mi_is_power_of_two(alignment)) return NULL; 
  if ((size&(alignment-1)) != 0) return NULL; // C11 requires integral multiple, see <https://en.cppreference.com/w/c/memory/aligned_alloc>
  void* p;
  if (alignment <= MI_MAX_ALIGN_SIZE) {
    p = MI_SOURCE_ARG(mi_malloc, size);
  }
  else {
    p = MI_SOURCE_ARG(mi_malloc_aligned, size, alignment);
  }
  mi_assert_internal(((uintptr_t)p % alignment) == 0);
  return p;
}

MI_SOURCE_API3(int, posix_memalign, void**, p, size_t, alignment, size_t, size)
{
  // Note: The spec dictates we should not modify `*p` on an error. (issue#27)
  // <http://man7.org/linux/man-pages/man3/posix_memalign.3.html>
  if (p == NULL) return EINVAL;
  if (alignment % sizeof(void*) != 0) return EINVAL;   // natural alignment
  if (!_mi_is_power_of_two(alignment)) return EINVAL;  // not a power of 2
  void* q;
  if (alignment <= MI_MAX_ALIGN_SIZE) {
    q = MI_SOURCE_ARG(mi_malloc, size);
  }
  else {
    q = MI_SOURCE_ARG(mi_malloc_aligned, size, alignment);
  }
  if (q==NULL && size != 0) return ENOMEM;
  mi_assert_internal(((uintptr_t)q % alignment) == 0);
  *p = q;
  return 0;
}

MI_SOURCE_API1(mi_decl_restrict wchar_t*, wcsdup, const wchar_t*, s)
{
  if (s==NULL) return NULL;
  size_t len;
  for(len = 0; s[len] != 0; len++) { }
  size_t size = (len+1)*sizeof(wchar_t);
  wchar_t* p = (wchar_t*)MI_SOURCE_ARG(mi_malloc, size);
  if (p != NULL) {
    memcpy(p,s,size);
  }
  return p;
}

MI_SOURCE_API1(mi_decl_restrict unsigned char*, mbsdup, const unsigned char*, s)
{
  return (unsigned char*)MI_SOURCE_ARG(mi_strdup,(const char*)s);
}


#ifdef _WIN32
#include <direct.h>  // getcwd
#else
#include <unistd.h>  // getcwd
#endif

MI_SOURCE_API2(mi_decl_restrict char*, getcwd, char*, buf, size_t, buf_len) {
  if (buf!=NULL && buf_len > 0) {
    #pragma warning(suppress:4996)
    return getcwd(buf, (int)buf_len);
  }
  else {
    size_t pmax = _mi_path_max();
    char* cbuf = (char*)MI_SOURCE_ARG(mi_malloc, pmax+1);
    #pragma warning(suppress:4996)
    char* res = getcwd(cbuf, (int)pmax);
    if (res != NULL) {
      res = MI_SOURCE_ARG(mi_strdup, cbuf); // shrink
    }
    mi_free(cbuf);
    return res;
  }  
}

MI_SOURCE_API3(mi_decl_restrict char*, _fullpath, char*, buf, const char*, path, size_t, buf_len) {
  if (path==NULL) return NULL;
  char* full = MI_SOURCE_ARG(mi_realpath, path, NULL);
  if (full==NULL) return NULL;
  if (buf==NULL) {
    return full;
  }
  else {
    size_t len = strlen(full);
    if (len < buf_len) {
      strcpy(buf, full);
    }
    mi_free(full);
    return (len < buf_len ? buf : NULL);
  }
}


// -----------------------------------------------------------------------------
// Microsoft: _wgetcwd, _wfullpath, _(w)dupenv, _aligned_recalloc, _aligned_offset_recalloc
// -----------------------------------------------------------------------------

static wchar_t* mi_mbstowcs_dup(const char* s  MI_SOURCE_XPARAM) {
  if (s==NULL) return NULL;
  size_t len = strlen(s);
  wchar_t* ws = (wchar_t*)MI_SOURCE_ARG(mi_malloc, (len + 1)*sizeof(wchar_t));  // over allocate by a factor 2
  mbstowcs(ws, s, len + 1);
  ws[len] = 0;
  return ws;
}

static char* mi_wcstombs_dup(const wchar_t* ws  MI_SOURCE_XPARAM) {
  if (ws==NULL) return NULL;
  size_t len = wcslen(ws);
  size_t sz  = (len + 1)*sizeof(wchar_t)*2; // over allocate by a factor 4 :( ok for our purposes though
  char* s = (char*)MI_SOURCE_ARG(mi_malloc, sz);  
  wcstombs(s, ws, sz);
  s[sz] = 0;
  return s;
}

MI_SOURCE_API3(mi_decl_restrict wchar_t*, _wfullpath, wchar_t*, wbuf, const wchar_t*, wpath, size_t, wbuf_len) {
  if (wpath==NULL) return NULL;
  char* path = mi_wcstombs_dup(wbuf  MI_SOURCE_XARG);
  char* full = MI_SOURCE_ARG(mi_realpath, path, NULL);
  mi_free(path);
  if (full==NULL) return NULL;
  wchar_t* wfull = mi_mbstowcs_dup( full  MI_SOURCE_XARG);
  mi_free(full);
  if (wbuf==NULL) {    
    return wfull;
  }
  else {
    size_t len = wcslen(wfull);
    if (len < wbuf_len) {
      wcscpy(wbuf, wfull);
    }
    mi_free(wfull);
    return (len < wbuf_len ? wbuf : NULL);
  }
}

MI_SOURCE_API2(mi_decl_restrict wchar_t*, _wgetcwd, wchar_t*, wbuf, size_t, wbuf_len) 
{
  char* res = MI_SOURCE_ARG(mi_getcwd, NULL, 0);
  if (res == NULL) return NULL;
  wchar_t* wres = mi_mbstowcs_dup( res  MI_SOURCE_XARG);
  mi_free(res);
  if (wbuf == NULL || wbuf_len == 0) {
    return wres;
  }
  else {
    size_t len = wcslen(wres);
    if (len < wbuf_len) {
      wcscpy(wbuf, wres);
    }
    mi_free(wres);
    return (len < wbuf_len ? wbuf : NULL);
  }
}


MI_SOURCE_API3(int, _dupenv_s, char**, buf, size_t*, size, const char*, name)
{
  if (buf==NULL || name==NULL) return EINVAL;
  if (size != NULL) *size = 0;
  #pragma warning(suppress:4996)
  char* p = getenv(name);
  if (p==NULL) {
    *buf = NULL;
  }
  else {
    *buf = MI_SOURCE_ARG(mi_strdup, p);
    if (*buf==NULL) return ENOMEM;
    if (size != NULL) *size = strlen(p);
  }
  return 0;
}

MI_SOURCE_API3(int, _wdupenv_s, wchar_t**, buf, size_t*, size, const wchar_t*, name)
{
  if (buf==NULL || name==NULL) return EINVAL;
  if (size != NULL) *size = 0;
#if !defined(_WIN32) || (defined(WINAPI_FAMILY) && (WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP))
  // not supported
  #ifndef NDEBUG
  UNUSED(__mi_source);
  #endif
  *buf = NULL;
  return EINVAL;
#else
  #pragma warning(suppress:4996)
  wchar_t* p = (wchar_t*)_wgetenv(name);
  if (p==NULL) {
    *buf = NULL;
  }
  else {
    *buf = MI_SOURCE_ARG(mi_wcsdup, p);
    if (*buf==NULL) return ENOMEM;
    if (size != NULL) *size = wcslen(p);
  }
  return 0;
#endif
}


#ifndef NDEBUG
void* dbg_mi_aligned_offset_recalloc(void* p, size_t newcount, size_t size, size_t alignment, size_t offset, mi_source_t __mi_source) mi_attr_noexcept { // Microsoft
  return dbg_mi_recalloc_aligned_at(p, newcount, size, alignment, offset, __mi_source);
}

void* dbg_mi_aligned_recalloc(void* p, size_t newcount, size_t size, size_t alignment, mi_source_t __mi_source) mi_attr_noexcept { // Microsoft
  return dbg_mi_recalloc_aligned(p, newcount, size, alignment, __mi_source);
}
#endif

void* mi_aligned_offset_recalloc(void* p, size_t newcount, size_t size, size_t alignment, size_t offset) mi_attr_noexcept { // Microsoft
  return MI_SOURCE_RET(mi_recalloc_aligned_at,p, newcount, size, alignment, offset);
}

void* mi_aligned_recalloc(void* p, size_t newcount, size_t size, size_t alignment) mi_attr_noexcept { // Microsoft
  return MI_SOURCE_RET(mi_recalloc_aligned,p, newcount, size, alignment);
}
