/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE   // ensure mmap flags are defined
#endif

#include "mimalloc.h"
#include "mimalloc-internal.h"

#include <string.h>  // memset
#include <errno.h>

/* -----------------------------------------------------------
  Initialization.
  On windows initializes support for aligned allocation and
  large OS pages (if MIMALLOC_LARGE_OS_PAGES is true).
----------------------------------------------------------- */

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <sys/mman.h>  // mmap
  #include <unistd.h>    // sysconf
#endif

// page size (initialized properly in `os_init`)
static size_t os_page_size = 4096;

// minimal allocation granularity
static size_t os_alloc_granularity = 4096;

// if non-zero, use large page allocation
static size_t large_os_page_size = 0;

// OS (small) page size
size_t _mi_os_page_size() {
  return os_page_size;
}

// if large OS pages are supported (2 or 4MiB), then return the size, otherwise return the small page size (4KiB)
size_t _mi_os_large_page_size() {
  return (large_os_page_size != 0 ? large_os_page_size : _mi_os_page_size());
}

static bool use_large_os_page(size_t size, size_t alignment) {
  // if we have access, check the size and alignment requirements
  if (large_os_page_size == 0) return false;
  return ((size % large_os_page_size) == 0 && (alignment % large_os_page_size) == 0);
}

// round to a good allocation size
static size_t mi_os_good_alloc_size(size_t size, size_t alignment) {
  UNUSED(alignment);
  if (size >= (SIZE_MAX - os_alloc_granularity)) return size; // possible overflow?
  return _mi_align_up(size, os_alloc_granularity);
}

#if defined(_WIN32)
// We use VirtualAlloc2 for aligned allocation, but it is only supported on Windows 10 and Windows Server 2016.
// So, we need to look it up dynamically to run on older systems.
typedef PVOID (*VirtualAlloc2Ptr)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG );
static VirtualAlloc2Ptr pVirtualAlloc2 = NULL;

void _mi_os_init(void) {
  // get the page size
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  if (si.dwPageSize > 0) os_page_size = si.dwPageSize;
  if (si.dwAllocationGranularity > 0) os_alloc_granularity = si.dwAllocationGranularity;
  // get the VirtualAlloc2 function
  HINSTANCE  hDll;
  hDll = LoadLibrary("kernelbase.dll");
  if (hDll!=NULL) {
    // use VirtualAlloc2FromApp as it is available to Windows store apps
    pVirtualAlloc2 = (VirtualAlloc2Ptr)GetProcAddress(hDll, "VirtualAlloc2FromApp");
    FreeLibrary(hDll);
  }
  // Try to see if large OS pages are supported
  unsigned long err = 0;
  bool ok = mi_option_is_enabled(mi_option_large_os_pages);
  if (ok) {
    // To use large pages on Windows, we first need access permission
    // Set "Lock pages in memory" permission in the group policy editor
    // <https://devblogs.microsoft.com/oldnewthing/20110128-00/?p=11643>
    HANDLE token = NULL;
    ok = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);
    if (ok) {
      TOKEN_PRIVILEGES tp;
      ok = LookupPrivilegeValue(NULL, "SeLockMemoryPrivilege", &tp.Privileges[0].Luid);
      if (ok) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(token, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
        if (ok) {
          err = GetLastError();
          ok = (err == ERROR_SUCCESS);
          if (ok) {
            large_os_page_size = GetLargePageMinimum();
          }
        }
      }
      CloseHandle(token);
    }
    if (!ok) {
      if (err==0) err = GetLastError();
      _mi_warning_message("cannot enable large OS page support, error %lu\n", err);
    }
  }
}
#else
void _mi_os_init() {
  // get the page size
  long result = sysconf(_SC_PAGESIZE);
  if (result > 0) {
    os_page_size = (size_t)result;
    os_alloc_granularity = os_page_size;
  }
  if (mi_option_is_enabled(mi_option_large_os_pages)) {
    large_os_page_size = (1UL<<21); // 2MiB
  }
}
#endif


/* -----------------------------------------------------------
  Raw allocation on Windows (VirtualAlloc) and Unix's (mmap).
  Defines a portable `mmap`, `munmap` and `mmap_trim`.
----------------------------------------------------------- */

uintptr_t _mi_align_up(uintptr_t sz, size_t alignment) {
  uintptr_t x = (sz / alignment) * alignment;
  if (x < sz) x += alignment;
  if (x < sz) return 0; // overflow
  return x;
}

static void* mi_align_up_ptr(void* p, size_t alignment) {
  return (void*)_mi_align_up((uintptr_t)p, alignment);
}

static uintptr_t _mi_align_down(uintptr_t sz, size_t alignment) {
  return (sz / alignment) * alignment;
}

static void* mi_align_down_ptr(void* p, size_t alignment) {
  return (void*)_mi_align_down((uintptr_t)p, alignment);
}


static bool mi_os_mem_free(void* addr, size_t size, mi_stats_t* stats)
{
  if (addr == NULL || size == 0) return true;
  bool err = false;
#if defined(_WIN32)
  err = (VirtualFree(addr, 0, MEM_RELEASE) == 0);
#else
  err = (munmap(addr, size) == -1);
#endif
  _mi_stat_decrease(&stats->committed, size); // TODO: what if never committed?
  _mi_stat_decrease(&stats->reserved, size);
  if (err) {
    #pragma warning(suppress:4996)
    _mi_warning_message("munmap failed: %s, addr 0x%8li, size %lu\n", strerror(errno), (size_t)addr, size);
    return false;
  }
  else {
    return true;
  }
}

static void* mi_os_mem_alloc(void* addr, size_t size, bool commit, int extra_flags, mi_stats_t* stats) {
  if (size == 0) return NULL;
  void* p = NULL;
#if defined(_WIN32)
  int flags = MEM_RESERVE | extra_flags;
  if (commit) flags |= MEM_COMMIT;
  if (use_large_os_page(size, 0)) {
    p = VirtualAlloc(addr, size, MEM_LARGE_PAGES | flags, PAGE_READWRITE);
  }
  if (p == NULL) {
    p = VirtualAlloc(addr, size, flags, PAGE_READWRITE);
  }
#else
  #if !defined(MAP_ANONYMOUS)
  #define MAP_ANONYMOUS  MAP_ANON
  #endif
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | extra_flags;
  if (addr != NULL) {
    #if defined(MAP_EXCL)
      flags |= MAP_FIXED | MAP_EXCL;  // BSD
    #elif defined(MAP_FIXED_NOREPLACE)
      flags |= MAP_FIXED_NOREPLACE;   // Linux
    #elif defined(MAP_FIXED)
      flags |= MAP_FIXED;
    #endif
  }
  int pflags = (commit ? (PROT_READ | PROT_WRITE) : PROT_NONE);
  #if defined(PROT_MAX)
  pflags |= PROT_MAX(PROT_READ | PROT_WRITE); // BSD
  #endif

  if (large_os_page_size > 0 && use_large_os_page(size, 0) && ((uintptr_t)addr % large_os_page_size) == 0) {
    int lflags = flags;
    #ifdef MAP_ALIGNED_SUPER
    lflags |= MAP_ALIGNED_SUPER;
    #endif
    #ifdef MAP_HUGETLB
    lflags |= MAP_HUGETLB;
    #endif
    #ifdef MAP_HUGE_2MB
    lflags |= MAP_HUGE_2MB;
    #endif
    if (lflags != flags) {
      // try large page allocation
      p = mmap(addr, size, pflags, lflags, -1, 0);
      if (p == MAP_FAILED) p = NULL; // fall back to regular mmap if large is exhausted or no permission
    }
  }
  if (p == NULL) {
    p = mmap(addr, size, pflags, flags, -1, 0);
    if (p == MAP_FAILED) p = NULL;
  }
  if (addr != NULL && p != addr) {
    mi_os_mem_free(p, size, stats);
    p = NULL;
  }
#endif
  mi_assert(p == NULL || (addr == NULL && p != addr) || (addr != NULL && p == addr));
  if (p != NULL) {
    _mi_stat_increase(&stats->mmap_calls, 1);
    _mi_stat_increase(&stats->reserved, size);
    if (commit) _mi_stat_increase(&stats->committed, size);
  }
  return p;
}

static void* mi_os_mem_alloc_aligned(size_t size, size_t alignment, bool commit, mi_stats_t* stats) {
  if (alignment < _mi_os_page_size() || ((alignment & (~alignment + 1)) != alignment)) return NULL;
  void* p = NULL;
  #if defined(_WIN32) && defined(MEM_EXTENDED_PARAMETER_TYPE_BITS)
  if (pVirtualAlloc2 != NULL) {
    // on modern Windows try use VirtualAlloc2
    MEM_ADDRESS_REQUIREMENTS reqs = {0};
    reqs.Alignment = alignment;
    MEM_EXTENDED_PARAMETER param = { 0 };
    param.Type = MemExtendedParameterAddressRequirements;
    param.Pointer = &reqs;
    DWORD flags = MEM_RESERVE;
    if (commit) flags |= MEM_COMMIT;
    if (use_large_os_page(size, alignment)) flags |= MEM_LARGE_PAGES;
    p = (*pVirtualAlloc2)(NULL, NULL, size, flags, PAGE_READWRITE, &param, 1);
  }
  #elif defined(MAP_ALIGNED)
  // on BSD, use the aligned mmap api
  size_t n = _mi_bsr(alignment);
  if (((size_t)1 << n) == alignment && n >= 12) {  // alignment is a power of 2 and >= 4096
    p = mi_os_mem_alloc(suggest, size, commit, MAP_ALIGNED(n), tld->stats);     // use the NetBSD/freeBSD aligned flags
  }
  #else
  UNUSED(size);
  UNUSED(alignment);
  #endif
  mi_assert(p == NULL || (uintptr_t)p % alignment == 0);
  if (p != NULL) {
    _mi_stat_increase(&stats->mmap_calls, 1);
    _mi_stat_increase(&stats->reserved, size);
    if (commit) _mi_stat_increase(&stats->committed, size);
  }
  return p;
}

// OS page align within a given area,
// either conservative (pages inside the area only),
// or not (straddling pages outside the area is possible)
static void* mi_os_page_align_areax(bool conservative, void* addr, size_t size, size_t* newsize) {
  mi_assert(addr != NULL && size > 0);
  if (newsize != NULL) *newsize = 0;
  if (size == 0 || addr == NULL) return NULL;

  // page align conservatively within the range
  void* start = (conservative ? mi_align_up_ptr(addr, _mi_os_page_size())
                              : mi_align_down_ptr(addr, _mi_os_page_size()));
  void* end = (conservative ? mi_align_down_ptr((uint8_t*)addr + size, _mi_os_page_size())
                            : mi_align_up_ptr((uint8_t*)addr + size, _mi_os_page_size()));
  ptrdiff_t diff = (uint8_t*)end - (uint8_t*)start;
  if (diff <= 0) return NULL;

  mi_assert_internal((size_t)diff <= size);
  if (newsize != NULL) *newsize = (size_t)diff;
  return start;
}

static void* mi_os_page_align_area_conservative(void* addr, size_t size, size_t* newsize) {
  return mi_os_page_align_areax(true,addr,size,newsize);
}



// Signal to the OS that the address range is no longer in use
// but may be used later again. This will release physical memory
// pages and reduce swapping while keeping the memory committed.
// We page align to a conservative area inside the range to reset.
bool _mi_os_reset(void* addr, size_t size, mi_stats_t* stats) {
  // page align conservatively within the range
  size_t csize;
  void* start = mi_os_page_align_area_conservative(addr,size,&csize);
  if (csize==0) return true;
  _mi_stat_increase(&stats->reset, csize);

#if defined(_WIN32)
  // Testing shows that for us (on `malloc-large`) MEM_RESET is 2x faster than DiscardVirtualMemory
  // (but this is for an access pattern that immediately reuses the memory)
  /*
  DWORD ok = DiscardVirtualMemory(start, csize);
  return (ok != 0);
  */
  void* p = VirtualAlloc(start, csize, MEM_RESET, PAGE_READWRITE);
  mi_assert(p == start);
  if (p != start) return false;
  /*
  // VirtualUnlock removes the memory eagerly from the current working set (which MEM_RESET does lazily on demand)
  // TODO: put this behind an option?
  DWORD ok = VirtualUnlock(start, csize);
  if (ok != 0) return false;
  */
  return true;
#else
  #if defined(MADV_FREE)
    static int advice = MADV_FREE;
    int err = madvise(start, csize, advice);
    if (err!=0 && errno==EINVAL && advice==MADV_FREE) {
      // if MADV_FREE is not supported, fall back to MADV_DONTNEED from now on
      advice = MADV_DONTNEED;
      err = madvise(start, csize, advice);
    }
  #else
    int err = madvise(start, csize, MADV_DONTNEED);
  #endif
  if (err != 0) {
    _mi_warning_message("madvise reset error: start: 0x%8p, csize: 0x%8zux, errno: %i\n", start, csize, errno);
  }
  //mi_assert(err == 0);
  return (err == 0);
#endif
}

// Protect a region in memory to be not accessible.
static  bool mi_os_protectx(void* addr, size_t size, bool protect) {
  // page align conservatively within the range
  size_t csize = 0;
  void* start = mi_os_page_align_area_conservative(addr, size, &csize);
  if (csize==0) return false;

  int err = 0;
#ifdef _WIN32
  DWORD oldprotect = 0;
  BOOL ok = VirtualProtect(start,csize,protect ? PAGE_NOACCESS : PAGE_READWRITE,&oldprotect);
  err = (ok ? 0 : GetLastError());
#else
  err = mprotect(start,csize,protect ? PROT_NONE : (PROT_READ|PROT_WRITE));
#endif
  if (err != 0) {
    _mi_warning_message("mprotect error: start: 0x%8p, csize: 0x%8zux, err: %i\n", start, csize, err);
  }
  return (err==0);
}

bool _mi_os_protect(void* addr, size_t size) {
  return mi_os_protectx(addr,size,true);
}

bool _mi_os_unprotect(void* addr, size_t size) {
  return mi_os_protectx(addr, size, false);
}

// Commit/Decommit memory.
// We page align to a conservative area inside the range to reset.
static bool mi_os_commitx(void* addr, size_t size, bool commit, mi_stats_t* stats) {
  // page align in the range, commit liberally, decommit conservative
  size_t csize;
  void* start = mi_os_page_align_areax(!commit, addr, size, &csize);
  if (csize == 0) return true;
  int err = 0;
  if (commit) {
    _mi_stat_increase(&stats->committed, csize);
    _mi_stat_increase(&stats->commit_calls,1);
  }
  else {
    _mi_stat_decrease(&stats->committed, csize);
  }

#if defined(_WIN32)
  if (commit) {
    void* p = VirtualAlloc(start, csize, MEM_COMMIT, PAGE_READWRITE);
    err = (p == start ? 0 : GetLastError());
  }
  else {
    BOOL ok = VirtualFree(start, csize, MEM_DECOMMIT);
    err = (ok ? 0 : GetLastError());
  }
#else
  err = mprotect(start, csize, (commit ? (PROT_READ | PROT_WRITE) : PROT_NONE));
#endif
  if (err != 0) {
    _mi_warning_message("commit/decommit error: start: 0x%8p, csize: 0x%8zux, err: %i\n", start, csize, err);
  }
  mi_assert_internal(err == 0);
  return (err == 0);
}

bool _mi_os_commit(void* addr, size_t size, mi_stats_t* stats) {
  return mi_os_commitx(addr, size, true, stats);
}

bool _mi_os_decommit(void* addr, size_t size, mi_stats_t* stats) {
  return mi_os_commitx(addr, size, false, stats);
}

bool _mi_os_shrink(void* p, size_t oldsize, size_t newsize, mi_stats_t* stats) {
  // page align conservatively within the range
  mi_assert_internal(oldsize > newsize && p != NULL);
  if (oldsize < newsize || p==NULL) return false;
  if (oldsize == newsize) return true;

  // oldsize and newsize should be page aligned or we cannot shrink precisely
  void* addr = (uint8_t*)p + newsize;
  size_t size = 0;
  void* start = mi_os_page_align_area_conservative(addr, oldsize - newsize, &size);
  if (size==0 || start != addr) return false;

  #ifdef _WIN32
  // we cannot shrink on windows, but we can decommit
  return _mi_os_decommit(start, size, stats);
  #else
  return mi_os_mem_free(start, size, stats);
  #endif
}

/* -----------------------------------------------------------
  OS allocation using mmap/munmap
----------------------------------------------------------- */

void* _mi_os_alloc(size_t size, mi_stats_t* stats) {
  if (size == 0) return NULL;
  size = mi_os_good_alloc_size(size, 0);
  void* p = mi_os_mem_alloc(NULL, size, true, 0, stats);
  mi_assert(p!=NULL);
  return p;
}

void  _mi_os_free(void* p, size_t size, mi_stats_t* stats) {
  if (size==0) return;
  size = mi_os_good_alloc_size(size, 0);
  mi_os_mem_free(p, size, stats);
}

// Slow but guaranteed way to allocated aligned memory
// by over-allocating and then reallocating at a fixed aligned
// address that should be available then.
static void* mi_os_alloc_aligned_ensured(size_t size, size_t alignment, bool commit, size_t trie, mi_stats_t* stats)
{
  if (trie >= 3) return NULL; // stop recursion (only on Windows)
  if (size > SIZE_MAX - alignment) return NULL; // overflow
  size_t alloc_size = size + alignment; // no need for -1 as we need to be page aligned anyways
  
  // allocate a chunk that includes the alignment
  void* p = mi_os_mem_alloc(NULL, alloc_size, commit, 0, stats);
  if (p == NULL) return NULL;
  // create an aligned pointer in the allocated area
  void* aligned_p = mi_align_up_ptr(p, alignment);
  mi_assert(aligned_p != NULL);
#if _WIN32
  // free it and try to allocate `size` at exactly `aligned_p`
  // note: this may fail in case another thread happens to allocate
  // concurrently at that spot. We try up to 3 times to mitigate this.
  mi_os_mem_free(p, alloc_size, stats);
  p = mi_os_mem_alloc(aligned_p, size, commit, 0, stats);
  if (p != aligned_p) {
    if (p != NULL) mi_os_mem_free(p, size, stats);
    return mi_os_alloc_aligned_ensured(size, alignment, commit, trie+1, stats);
  }
#else  
  // we selectively unmap parts around the over-allocated area.
  size_t pre_size = (uint8_t*)aligned_p - (uint8_t*)p;
  size_t mid_size = _mi_align_up(size, _mi_os_page_size());
  size_t post_size = alloc_size - pre_size - mid_size;
  if (pre_size > 0)  mi_os_mem_free(p, pre_size, stats);
  if (post_size > 0) mi_os_mem_free((uint8_t*)aligned_p + mid_size, post_size, stats);
#endif

  mi_assert(((uintptr_t)aligned_p) % alignment == 0);
  return aligned_p;
}

// Allocate an aligned block.
// Since `mi_mmap` is relatively slow we try to allocate directly at first and
// hope to get an aligned address; only when that fails we fall back
// to a guaranteed method by overallocating at first and adjusting.
void* _mi_os_alloc_aligned(size_t size, size_t alignment, bool commit, mi_os_tld_t* tld)
{
  if (size == 0) return NULL;
  size = mi_os_good_alloc_size(size,alignment);
  if (alignment < 1024) return mi_os_mem_alloc(NULL, size, commit, 0, tld->stats);

  // try direct OS aligned allocation; only supported on BSD and Windows 10+
  void* suggest = NULL;
  void* p = mi_os_mem_alloc_aligned(size,alignment,commit,tld->stats);

  // Fall back
  if (p==NULL && (tld->mmap_next_probable % alignment) == 0) {
    // if the next probable address is aligned,
    // then try to just allocate `size` and hope it is aligned...
    p = mi_os_mem_alloc(suggest, size, commit, 0, tld->stats);
    if (p == NULL) return NULL;
    if (((uintptr_t)p % alignment) == 0) _mi_stat_increase(&tld->stats->mmap_right_align, 1);
  }
  //fprintf(stderr, "segment address guess: %s, p=%lxu, guess:%lxu\n", (p != NULL && (uintptr_t)p % alignment ==0 ? "correct" : "incorrect"), (uintptr_t)p, next_probable);

  if (p==NULL || ((uintptr_t)p % alignment) != 0) {
    // if `p` is not yet aligned after all, free the block and use a slower
    // but guaranteed way to allocate an aligned block
    if (p != NULL) mi_os_mem_free(p, size, tld->stats);
    _mi_stat_increase( &tld->stats->mmap_ensure_aligned, 1);
    //fprintf(stderr, "mimalloc: slow mmap 0x%lx\n", _mi_thread_id());
    p = mi_os_alloc_aligned_ensured(size, alignment,commit,0,tld->stats);
  }
  if (p != NULL) {
    // next probable address is the page-aligned address just after the newly allocated area.
    size_t probable_size = MI_SEGMENT_SIZE;
    if (tld->mmap_previous > p) {
      // Linux tends to allocate downward
      tld->mmap_next_probable = _mi_align_down((uintptr_t)p - probable_size, os_alloc_granularity); // ((uintptr_t)previous - (uintptr_t)p);
    }
    else {
      // Otherwise, guess the next address is page aligned `size` from current pointer
      tld->mmap_next_probable = _mi_align_up((uintptr_t)p + probable_size, os_alloc_granularity);
    }
    tld->mmap_previous = p;
  }
  return p;
}
