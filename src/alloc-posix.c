/* ----------------------------------------------------------------------------
Copyright (c) 2018,2019, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// ------------------------------------------------------------------------
// mi prefixed publi definitions of various Posix, Unix, and C++ functions
// for convenience and used when overriding these functions.
// ------------------------------------------------------------------------
#define MI_NO_SOURCE_DEBUG
#include "mimalloc.h"
#include "mimalloc-internal.h"

// ------------------------------------------------------
// Posix & Unix functions definitions
// ------------------------------------------------------

#include <errno.h>
#include <string.h>  // memcpy
#include <stdlib.h>  // getenv

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

MI_SOURCE_API2(void*, memalign, size_t, alignment, size_t, size)
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

MI_SOURCE_API1(void*, valloc, size_t, size)
{
  return MI_SOURCE_ARG(mi_malloc_aligned, size, _mi_os_page_size());
}

MI_SOURCE_API1(void*, pvalloc, size_t, size)
{
  size_t psize = _mi_os_page_size();
  if (size >= SIZE_MAX - psize) return NULL; // overflow
  size_t asize = _mi_align_up(size, psize); 
  return MI_SOURCE_ARG(mi_malloc_aligned, asize, psize);
}

MI_SOURCE_API2(void*, aligned_alloc, size_t, alignment, size_t, size)
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

static int mi_base_posix_memalign(void** p, size_t alignment, size_t size  MI_SOURCE_XPARAM)
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

#ifndef NDEBUG
int dbg_mi_posix_memalign(void** p, size_t alignment, size_t size, mi_source_t __mi_source) mi_attr_noexcept {
  UNUSED(__mi_source);
  return mi_base_posix_memalign(p, alignment, size  MI_SOURCE_XARG);
}
#endif

int mi_posix_memalign(void** p, size_t alignment, size_t size) mi_attr_noexcept  {
  return mi_base_posix_memalign(p, alignment, size  MI_SOURCE_XRET());
}


MI_SOURCE_API1(unsigned short*, wcsdup, const unsigned short*, s)
{
  if (s==NULL) return NULL;
  size_t len;
  for(len = 0; s[len] != 0; len++) { }
  size_t size = (len+1)*sizeof(unsigned short);
  unsigned short* p = (unsigned short*)MI_SOURCE_ARG(mi_malloc, size);
  if (p != NULL) {
    memcpy(p,s,size);
  }
  return p;
}

MI_SOURCE_API1(unsigned char*, mbsdup, const unsigned char*, s)
{
  return (unsigned char*)MI_SOURCE_ARG(mi_strdup,(const char*)s);
}

static int mi_base_dupenv_s(char** buf, size_t* size, const char* name  MI_SOURCE_XPARAM)
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

#ifndef NDEBUG
int dbg_mi_dupenv_s(char** buf, size_t* size, const char* name, mi_source_t __mi_source) mi_attr_noexcept {
  UNUSED(__mi_source);
  return mi_base_dupenv_s(buf, size, name  MI_SOURCE_XARG);
}
#endif

int mi_dupenv_s(char** buf, size_t* size, const char* name) mi_attr_noexcept {
  return mi_base_dupenv_s(buf, size, name  MI_SOURCE_XRET());
}


static int mi_base_wdupenv_s(unsigned short** buf, size_t* size, const unsigned short* name  MI_SOURCE_XPARAM)
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
  unsigned short* p = (unsigned short*)_wgetenv((const wchar_t*)name);
  if (p==NULL) {
    *buf = NULL;
  }
  else {
    *buf = MI_SOURCE_ARG(mi_wcsdup, p);
    if (*buf==NULL) return ENOMEM;
    if (size != NULL) *size = wcslen((const wchar_t*)p);
  }
  return 0;
#endif
}

#ifndef NDEBUG
int dbg_mi_wdupenv_s(unsigned short** buf, size_t* size, const unsigned short* name, mi_source_t __mi_source) mi_attr_noexcept {
  UNUSED(__mi_source);
  return mi_base_wdupenv_s(buf, size, name  MI_SOURCE_XARG);
}
#endif

int mi_wdupenv_s(unsigned short** buf, size_t* size, const unsigned short* name) mi_attr_noexcept  {
  return mi_base_wdupenv_s(buf, size, name  MI_SOURCE_XRET());
}


#ifndef NDEBUG
mi_decl_restrict void* dbg_mi_aligned_offset_recalloc(void* p, size_t newcount, size_t size, size_t alignment, size_t offset, mi_source_t __mi_source) mi_attr_noexcept { // Microsoft
  return dbg_mi_recalloc_aligned_at(p, newcount, size, alignment, offset, __mi_source);
}

mi_decl_restrict void* dbg_mi_aligned_recalloc(void* p, size_t newcount, size_t size, size_t alignment, mi_source_t __mi_source) mi_attr_noexcept { // Microsoft
  return dbg_mi_recalloc_aligned(p, newcount, size, alignment, __mi_source);
}
#endif

mi_decl_restrict void* mi_aligned_offset_recalloc(void* p, size_t newcount, size_t size, size_t alignment, size_t offset) mi_attr_noexcept { // Microsoft
  return MI_SOURCE_RET(mi_recalloc_aligned_at,p, newcount, size, alignment, offset);
}

mi_decl_restrict void* mi_aligned_recalloc(void* p, size_t newcount, size_t size, size_t alignment) mi_attr_noexcept { // Microsoft
  return MI_SOURCE_RET(mi_recalloc_aligned,p, newcount, size, alignment);
}
