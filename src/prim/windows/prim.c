/* ----------------------------------------------------------------------------
Copyright (c) 2018-2023, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// This file is included in `src/prim/prim.c`

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "mimalloc/atomic.h"
#include "mimalloc/prim.h"
#include <stdio.h>   // fputs, stderr


//---------------------------------------------
// Dynamically bind Windows API points for portability
//---------------------------------------------

// We use VirtualAlloc2 for aligned allocation, but it is only supported on Windows 10 and Windows Server 2016.
// So, we need to look it up dynamically to run on older systems. (use __stdcall for 32-bit compatibility)
// NtAllocateVirtualAllocEx is used for huge OS page allocation (1GiB)
// We define a minimal MEM_EXTENDED_PARAMETER ourselves in order to be able to compile with older SDK's.
typedef enum MI_MEM_EXTENDED_PARAMETER_TYPE_E {
  MiMemExtendedParameterInvalidType = 0,
  MiMemExtendedParameterAddressRequirements,
  MiMemExtendedParameterNumaNode,
  MiMemExtendedParameterPartitionHandle,
  MiMemExtendedParameterUserPhysicalHandle,
  MiMemExtendedParameterAttributeFlags,
  MiMemExtendedParameterMax
} MI_MEM_EXTENDED_PARAMETER_TYPE;

typedef struct DECLSPEC_ALIGN(8) MI_MEM_EXTENDED_PARAMETER_S {
  struct { DWORD64 Type : 8; DWORD64 Reserved : 56; } Type;
  union  { DWORD64 ULong64; PVOID Pointer; SIZE_T Size; HANDLE Handle; DWORD ULong; } Arg;
} MI_MEM_EXTENDED_PARAMETER;

typedef struct MI_MEM_ADDRESS_REQUIREMENTS_S {
  PVOID  LowestStartingAddress;
  PVOID  HighestEndingAddress;
  SIZE_T Alignment;
} MI_MEM_ADDRESS_REQUIREMENTS;

#define MI_MEM_EXTENDED_PARAMETER_NONPAGED_HUGE   0x00000010

#include <winternl.h>
typedef PVOID    (__stdcall *PVirtualAlloc2)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MI_MEM_EXTENDED_PARAMETER*, ULONG);
typedef NTSTATUS (__stdcall *PNtAllocateVirtualMemoryEx)(HANDLE, PVOID*, SIZE_T*, ULONG, ULONG, MI_MEM_EXTENDED_PARAMETER*, ULONG);
static PVirtualAlloc2 pVirtualAlloc2 = NULL;
static PNtAllocateVirtualMemoryEx pNtAllocateVirtualMemoryEx = NULL;

// Similarly, GetNumaProcesorNodeEx is only supported since Windows 7
typedef struct MI_PROCESSOR_NUMBER_S { WORD Group; BYTE Number; BYTE Reserved; } MI_PROCESSOR_NUMBER;

typedef VOID (__stdcall *PGetCurrentProcessorNumberEx)(MI_PROCESSOR_NUMBER* ProcNumber);
typedef BOOL (__stdcall *PGetNumaProcessorNodeEx)(MI_PROCESSOR_NUMBER* Processor, PUSHORT NodeNumber);
typedef BOOL (__stdcall* PGetNumaNodeProcessorMaskEx)(USHORT Node, PGROUP_AFFINITY ProcessorMask);
typedef BOOL (__stdcall *PGetNumaProcessorNode)(UCHAR Processor, PUCHAR NodeNumber);
static PGetCurrentProcessorNumberEx pGetCurrentProcessorNumberEx = NULL;
static PGetNumaProcessorNodeEx      pGetNumaProcessorNodeEx = NULL;
static PGetNumaNodeProcessorMaskEx  pGetNumaNodeProcessorMaskEx = NULL;
static PGetNumaProcessorNode        pGetNumaProcessorNode = NULL;

//---------------------------------------------
// Get lock memory permission dynamically (if possible)
// To use large pages on Windows, or remappable memory, we first need access permission
// Set "Lock pages in memory" permission in the group policy editor
// <https://devblogs.microsoft.com/oldnewthing/20110128-00/?p=11643>  
//---------------------------------------------

static bool mi_win_get_lock_memory_privilege(void)
{
  static bool lock_memory_initialized = false;
  static int lock_memory_err = 0;
  if (lock_memory_initialized) return (lock_memory_err == 0);
  lock_memory_initialized = true;

  // Try to see if we have permission can lock memory
  unsigned long err = 0;
  HANDLE token = NULL;
  BOOL ok = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);
  if (ok) {
    TOKEN_PRIVILEGES tp;
    ok = LookupPrivilegeValue(NULL, TEXT("SeLockMemoryPrivilege"), &tp.Privileges[0].Luid);
    if (ok) {
      tp.PrivilegeCount = 1;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
      ok = AdjustTokenPrivileges(token, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
      if (ok) {
        err = GetLastError();
        ok = (err == ERROR_SUCCESS);        
      }
    }
    CloseHandle(token);
  }
  if (!ok) {
    if (err == 0) { err = GetLastError(); }
    lock_memory_err = (int)err;
    _mi_warning_message("cannot acquire the lock memory privilege (needed for large OS page or remap support), error %lu (0x%04lx)\n", err);
  }
  return (ok!=0);
}


//---------------------------------------------
// Initialize
//---------------------------------------------

void _mi_prim_mem_init( mi_os_mem_config_t* config )
{
  config->has_overcommit = false;
  config->must_free_whole = true;
  config->has_virtual_reserve = true;
  // get the page size
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  if (si.dwPageSize > 0) { config->page_size = si.dwPageSize; }
  if (si.dwAllocationGranularity > 0) { config->alloc_granularity = si.dwAllocationGranularity; }
  // get the VirtualAlloc2 function
  HINSTANCE  hDll;
  hDll = LoadLibrary(TEXT("kernelbase.dll"));
  if (hDll != NULL) {
    // use VirtualAlloc2FromApp if possible as it is available to Windows store apps
    pVirtualAlloc2 = (PVirtualAlloc2)(void (*)(void))GetProcAddress(hDll, "VirtualAlloc2FromApp");
    if (pVirtualAlloc2==NULL) pVirtualAlloc2 = (PVirtualAlloc2)(void (*)(void))GetProcAddress(hDll, "VirtualAlloc2");
    FreeLibrary(hDll);
  }
  // NtAllocateVirtualMemoryEx is used for huge page allocation
  hDll = LoadLibrary(TEXT("ntdll.dll"));
  if (hDll != NULL) {
    pNtAllocateVirtualMemoryEx = (PNtAllocateVirtualMemoryEx)(void (*)(void))GetProcAddress(hDll, "NtAllocateVirtualMemoryEx");
    FreeLibrary(hDll);
  }
  // Try to use Win7+ numa API
  hDll = LoadLibrary(TEXT("kernel32.dll"));
  if (hDll != NULL) {
    pGetCurrentProcessorNumberEx = (PGetCurrentProcessorNumberEx)(void (*)(void))GetProcAddress(hDll, "GetCurrentProcessorNumberEx");
    pGetNumaProcessorNodeEx = (PGetNumaProcessorNodeEx)(void (*)(void))GetProcAddress(hDll, "GetNumaProcessorNodeEx");
    pGetNumaNodeProcessorMaskEx = (PGetNumaNodeProcessorMaskEx)(void (*)(void))GetProcAddress(hDll, "GetNumaNodeProcessorMaskEx");
    pGetNumaProcessorNode = (PGetNumaProcessorNode)(void (*)(void))GetProcAddress(hDll, "GetNumaProcessorNode");
    FreeLibrary(hDll);
  }
  // if (mi_option_is_enabled(mi_option_allow_large_os_pages) || mi_option_is_enabled(mi_option_reserve_huge_os_pages)) {
  if (mi_win_get_lock_memory_privilege()) {
    config->large_page_size = GetLargePageMinimum();
    config->has_remap = true;
  };
  // }
}


//---------------------------------------------
// Free
//---------------------------------------------

int _mi_prim_free(void* addr, size_t size ) {
  MI_UNUSED(size);
  DWORD errcode = 0;
  bool err = (VirtualFree(addr, 0, MEM_RELEASE) == 0);
  if (err) { errcode = GetLastError(); }
  if (errcode == ERROR_INVALID_ADDRESS) {
    // In mi_os_mem_alloc_aligned the fallback path may have returned a pointer inside
    // the memory region returned by VirtualAlloc; in that case we need to free using
    // the start of the region.
    MEMORY_BASIC_INFORMATION info = { 0 };
    VirtualQuery(addr, &info, sizeof(info));
    if (info.AllocationBase < addr && ((uint8_t*)addr - (uint8_t*)info.AllocationBase) < (ptrdiff_t)MI_SEGMENT_SIZE) {
      errcode = 0;
      err = (VirtualFree(info.AllocationBase, 0, MEM_RELEASE) == 0);
      if (err) { errcode = GetLastError(); }
    }
  }
  return (int)errcode;
}


//---------------------------------------------
// VirtualAlloc
//---------------------------------------------

static void* mi_win_virtual_alloc_prim(void* addr, size_t size, size_t try_alignment, DWORD flags) {
  #if (MI_INTPTR_SIZE >= 8)
  // on 64-bit systems, try to use the virtual address area after 2TiB for 4MiB aligned allocations
  if (addr == NULL) {
    void* hint = _mi_os_get_aligned_hint(try_alignment,size);
    if (hint != NULL) {
      void* p = VirtualAlloc(hint, size, flags, PAGE_READWRITE);
      if (p != NULL) return p;
      _mi_verbose_message("warning: unable to allocate hinted aligned OS memory (%zu bytes, error code: 0x%x, address: %p, alignment: %zu, flags: 0x%x)\n", size, GetLastError(), hint, try_alignment, flags);
      // fall through on error
    }
  }
  #endif
  // on modern Windows try use VirtualAlloc2 for aligned allocation
  if (try_alignment > 1 && (try_alignment % _mi_os_page_size()) == 0 && pVirtualAlloc2 != NULL) {
    MI_MEM_ADDRESS_REQUIREMENTS reqs = { 0, 0, 0 };
    reqs.Alignment = try_alignment;
    MI_MEM_EXTENDED_PARAMETER param = { {0, 0}, {0} };
    param.Type.Type = MiMemExtendedParameterAddressRequirements;
    param.Arg.Pointer = &reqs;
    void* p = (*pVirtualAlloc2)(GetCurrentProcess(), addr, size, flags, PAGE_READWRITE, &param, 1);
    if (p != NULL) return p;
    _mi_warning_message("unable to allocate aligned OS memory (%zu bytes, error code: 0x%x, address: %p, alignment: %zu, flags: 0x%x)\n", size, GetLastError(), addr, try_alignment, flags);
    // fall through on error
  }
  // last resort
  return VirtualAlloc(addr, size, flags, PAGE_READWRITE);
}

static void* mi_win_virtual_alloc(void* addr, size_t size, size_t try_alignment, DWORD flags, bool large_only, bool allow_large, bool* is_large) {
  mi_assert_internal(!(large_only && !allow_large));
  static _Atomic(size_t) large_page_try_ok; // = 0;
  void* p = NULL;
  // Try to allocate large OS pages (2MiB) if allowed or required.
  if ((large_only || _mi_os_use_large_page(size, try_alignment))
      && allow_large && (flags&MEM_COMMIT)!=0 && (flags&MEM_RESERVE)!=0) {
    size_t try_ok = mi_atomic_load_acquire(&large_page_try_ok);
    if (!large_only && try_ok > 0) {
      // if a large page allocation fails, it seems the calls to VirtualAlloc get very expensive.
      // therefore, once a large page allocation failed, we don't try again for `large_page_try_ok` times.
      mi_atomic_cas_strong_acq_rel(&large_page_try_ok, &try_ok, try_ok - 1);
    }
    else {
      // large OS pages must always reserve and commit.
      *is_large = true;
      p = mi_win_virtual_alloc_prim(addr, size, try_alignment, flags | MEM_LARGE_PAGES);
      if (large_only) return p;
      // fall back to non-large page allocation on error (`p == NULL`).
      if (p == NULL) {
        mi_atomic_store_release(&large_page_try_ok,10UL);  // on error, don't try again for the next N allocations
      }
    }
  }
  // Fall back to regular page allocation
  if (p == NULL) {
    *is_large = ((flags&MEM_LARGE_PAGES) != 0);
    p = mi_win_virtual_alloc_prim(addr, size, try_alignment, flags);
  }
  //if (p == NULL) { _mi_warning_message("unable to allocate OS memory (%zu bytes, error code: 0x%x, address: %p, alignment: %zu, flags: 0x%x, large only: %d, allow large: %d)\n", size, GetLastError(), addr, try_alignment, flags, large_only, allow_large); }
  return p;
}

int _mi_prim_alloc(void* hint, size_t size, size_t try_alignment, bool commit, bool allow_large, bool* is_large, bool* is_zero, void** addr) {
  mi_assert_internal(size > 0 && (size % _mi_os_page_size()) == 0);
  mi_assert_internal(commit || !allow_large);
  mi_assert_internal(try_alignment > 0);
  *is_zero = true;
  int flags = MEM_RESERVE;
  if (commit) { flags |= MEM_COMMIT; }
  *addr = mi_win_virtual_alloc(hint, size, try_alignment, flags, false, allow_large, is_large);
  return (*addr != NULL ? 0 : (int)GetLastError());
}


//---------------------------------------------
// Commit/Reset/Protect
//---------------------------------------------
#ifdef _MSC_VER
#pragma warning(disable:6250)   // suppress warning calling VirtualFree without MEM_RELEASE (for decommit)
#endif

int _mi_prim_commit(void* addr, size_t size, bool* is_zero) {
  *is_zero = false;
  /*
  // zero'ing only happens on an initial commit... but checking upfront seems expensive..
  _MEMORY_BASIC_INFORMATION meminfo; _mi_memzero_var(meminfo);
  if (VirtualQuery(addr, &meminfo, size) > 0) {
    if ((meminfo.State & MEM_COMMIT) == 0) {
      *is_zero = true;
    }
  }
  */
  // commit
  void* p = VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE);
  if (p == NULL) return (int)GetLastError();
  return 0;
}

int _mi_prim_decommit(void* addr, size_t size, bool* needs_recommit) {  
  BOOL ok = VirtualFree(addr, size, MEM_DECOMMIT);
  *needs_recommit = true;  // for safety, assume always decommitted even in the case of an error.
  return (ok ? 0 : (int)GetLastError());
}

int _mi_prim_reset(void* addr, size_t size) {
  void* p = VirtualAlloc(addr, size, MEM_RESET, PAGE_READWRITE);
  mi_assert_internal(p == addr);
  #if 0
  if (p != NULL) {
    VirtualUnlock(addr,size); // VirtualUnlock after MEM_RESET removes the memory directly from the working set
  }
  #endif
  return (p != NULL ? 0 : (int)GetLastError());
}

int _mi_prim_protect(void* addr, size_t size, bool protect) {
  DWORD oldprotect = 0;
  BOOL ok = VirtualProtect(addr, size, protect ? PAGE_NOACCESS : PAGE_READWRITE, &oldprotect);
  return (ok ? 0 : (int)GetLastError());
}


//---------------------------------------------
// Huge page allocation
//---------------------------------------------

static void* _mi_prim_alloc_huge_os_pagesx(void* hint_addr, size_t size, int numa_node)
{
  const DWORD flags = MEM_LARGE_PAGES | MEM_COMMIT | MEM_RESERVE;

  if (!mi_win_get_lock_memory_privilege()) return NULL;

  MI_MEM_EXTENDED_PARAMETER params[3] = { {{0,0},{0}},{{0,0},{0}},{{0,0},{0}} };
  // on modern Windows try use NtAllocateVirtualMemoryEx for 1GiB huge pages
  static bool mi_huge_pages_available = true;
  if (pNtAllocateVirtualMemoryEx != NULL && mi_huge_pages_available) {
    params[0].Type.Type = MiMemExtendedParameterAttributeFlags;
    params[0].Arg.ULong64 = MI_MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;
    ULONG param_count = 1;
    if (numa_node >= 0) {
      param_count++;
      params[1].Type.Type = MiMemExtendedParameterNumaNode;
      params[1].Arg.ULong = (unsigned)numa_node;
    }
    SIZE_T psize = size;
    void* base = hint_addr;
    NTSTATUS err = (*pNtAllocateVirtualMemoryEx)(GetCurrentProcess(), &base, &psize, flags, PAGE_READWRITE, params, param_count);
    if (err == 0 && base != NULL) {
      return base;
    }
    else {
      // fall back to regular large pages
      mi_huge_pages_available = false; // don't try further huge pages
      _mi_warning_message("unable to allocate using huge (1GiB) pages, trying large (2MiB) pages instead (status 0x%lx)\n", err);
    }
  }
  // on modern Windows try use VirtualAlloc2 for numa aware large OS page allocation
  if (pVirtualAlloc2 != NULL && numa_node >= 0) {
    params[0].Type.Type = MiMemExtendedParameterNumaNode;
    params[0].Arg.ULong = (unsigned)numa_node;
    return (*pVirtualAlloc2)(GetCurrentProcess(), hint_addr, size, flags, PAGE_READWRITE, params, 1);
  }

  // otherwise use regular virtual alloc on older windows
  return VirtualAlloc(hint_addr, size, flags, PAGE_READWRITE);
}

int _mi_prim_alloc_huge_os_pages(void* hint_addr, size_t size, int numa_node, bool* is_zero, void** addr) {
  *is_zero = true;
  *addr = _mi_prim_alloc_huge_os_pagesx(hint_addr,size,numa_node);
  return (*addr != NULL ? 0 : (int)GetLastError());
}


//---------------------------------------------
// Numa nodes
//---------------------------------------------

size_t _mi_prim_numa_node(void) {
  USHORT numa_node = 0;
  if (pGetCurrentProcessorNumberEx != NULL && pGetNumaProcessorNodeEx != NULL) {
    // Extended API is supported
    MI_PROCESSOR_NUMBER pnum;
    (*pGetCurrentProcessorNumberEx)(&pnum);
    USHORT nnode = 0;
    BOOL ok = (*pGetNumaProcessorNodeEx)(&pnum, &nnode);
    if (ok) { numa_node = nnode; }
  }
  else if (pGetNumaProcessorNode != NULL) {
    // Vista or earlier, use older API that is limited to 64 processors. Issue #277
    DWORD pnum = GetCurrentProcessorNumber();
    UCHAR nnode = 0;
    BOOL ok = pGetNumaProcessorNode((UCHAR)pnum, &nnode);
    if (ok) { numa_node = nnode; }
  }
  return numa_node;
}

size_t _mi_prim_numa_node_count(void) {
  ULONG numa_max = 0;
  GetNumaHighestNodeNumber(&numa_max);
  // find the highest node number that has actual processors assigned to it. Issue #282
  while(numa_max > 0) {
    if (pGetNumaNodeProcessorMaskEx != NULL) {
      // Extended API is supported
      GROUP_AFFINITY affinity;
      if ((*pGetNumaNodeProcessorMaskEx)((USHORT)numa_max, &affinity)) {
        if (affinity.Mask != 0) break;  // found the maximum non-empty node
      }
    }
    else {
      // Vista or earlier, use older API that is limited to 64 processors.
      ULONGLONG mask;
      if (GetNumaNodeProcessorMask((UCHAR)numa_max, &mask)) {
        if (mask != 0) break; // found the maximum non-empty node
      };
    }
    // max node was invalid or had no processor assigned, try again
    numa_max--;
  }
  return ((size_t)numa_max + 1);
}


//----------------------------------------------------------------
// Clock
//----------------------------------------------------------------

static mi_msecs_t mi_to_msecs(LARGE_INTEGER t) {
  static LARGE_INTEGER mfreq; // = 0
  if (mfreq.QuadPart == 0LL) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    mfreq.QuadPart = f.QuadPart/1000LL;
    if (mfreq.QuadPart == 0) mfreq.QuadPart = 1;
  }
  return (mi_msecs_t)(t.QuadPart / mfreq.QuadPart);
}

mi_msecs_t _mi_prim_clock_now(void) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return mi_to_msecs(t);
}


//----------------------------------------------------------------
// Process Info
//----------------------------------------------------------------

#include <windows.h>
#include <psapi.h>

static mi_msecs_t filetime_msecs(const FILETIME* ftime) {
  ULARGE_INTEGER i;
  i.LowPart = ftime->dwLowDateTime;
  i.HighPart = ftime->dwHighDateTime;
  mi_msecs_t msecs = (i.QuadPart / 10000); // FILETIME is in 100 nano seconds
  return msecs;
}

typedef BOOL (WINAPI *PGetProcessMemoryInfo)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
static PGetProcessMemoryInfo pGetProcessMemoryInfo = NULL;

void _mi_prim_process_info(mi_process_info_t* pinfo)
{
  FILETIME ct;
  FILETIME ut;
  FILETIME st;
  FILETIME et;
  GetProcessTimes(GetCurrentProcess(), &ct, &et, &st, &ut);
  pinfo->utime = filetime_msecs(&ut);
  pinfo->stime = filetime_msecs(&st);
  
  // load psapi on demand
  if (pGetProcessMemoryInfo == NULL) {
    HINSTANCE hDll = LoadLibrary(TEXT("psapi.dll"));
    if (hDll != NULL) {
      pGetProcessMemoryInfo = (PGetProcessMemoryInfo)(void (*)(void))GetProcAddress(hDll, "GetProcessMemoryInfo");
    }
  }

  // get process info
  PROCESS_MEMORY_COUNTERS info;
  memset(&info, 0, sizeof(info));
  if (pGetProcessMemoryInfo != NULL) {
    pGetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
  } 
  pinfo->current_rss    = (size_t)info.WorkingSetSize;
  pinfo->peak_rss       = (size_t)info.PeakWorkingSetSize;
  pinfo->current_commit = (size_t)info.PagefileUsage;
  pinfo->peak_commit    = (size_t)info.PeakPagefileUsage;
  pinfo->page_faults    = (size_t)info.PageFaultCount;
}

//----------------------------------------------------------------
// Output
//----------------------------------------------------------------

void _mi_prim_out_stderr( const char* msg ) 
{
  // on windows with redirection, the C runtime cannot handle locale dependent output
  // after the main thread closes so we use direct console output.
  if (!_mi_preloading()) {
    // _cputs(msg);  // _cputs cannot be used at is aborts if it fails to lock the console
    static HANDLE hcon = INVALID_HANDLE_VALUE;
    static bool hconIsConsole;
    if (hcon == INVALID_HANDLE_VALUE) {
      CONSOLE_SCREEN_BUFFER_INFO sbi;
      hcon = GetStdHandle(STD_ERROR_HANDLE);
      hconIsConsole = ((hcon != INVALID_HANDLE_VALUE) && GetConsoleScreenBufferInfo(hcon, &sbi));
    }
    const size_t len = _mi_strlen(msg);
    if (len > 0 && len < UINT32_MAX) {
      DWORD written = 0;
      if (hconIsConsole) {
        WriteConsoleA(hcon, msg, (DWORD)len, &written, NULL);
      }
      else if (hcon != INVALID_HANDLE_VALUE) {
        // use direct write if stderr was redirected
        WriteFile(hcon, msg, (DWORD)len, &written, NULL);
      }
      else {
        // finally fall back to fputs after all
        fputs(msg, stderr);
      }
    }
  }
}


//----------------------------------------------------------------
// Environment
//----------------------------------------------------------------

// On Windows use GetEnvironmentVariable instead of getenv to work
// reliably even when this is invoked before the C runtime is initialized.
// i.e. when `_mi_preloading() == true`.
// Note: on windows, environment names are not case sensitive.
bool _mi_prim_getenv(const char* name, char* result, size_t result_size) {
  result[0] = 0;
  size_t len = GetEnvironmentVariableA(name, result, (DWORD)result_size);
  return (len > 0 && len < result_size);
}



//----------------------------------------------------------------
// Random
//----------------------------------------------------------------

#if defined(MI_USE_RTLGENRANDOM) // || defined(__cplusplus)
// We prefer to use BCryptGenRandom instead of (the unofficial) RtlGenRandom but when using
// dynamic overriding, we observed it can raise an exception when compiled with C++, and
// sometimes deadlocks when also running under the VS debugger.
// In contrast, issue #623 implies that on Windows Server 2019 we need to use BCryptGenRandom.
// To be continued..
#pragma comment (lib,"advapi32.lib")
#define RtlGenRandom  SystemFunction036
mi_decl_externc BOOLEAN NTAPI RtlGenRandom(PVOID RandomBuffer, ULONG RandomBufferLength);

bool _mi_prim_random_buf(void* buf, size_t buf_len) {
  return (RtlGenRandom(buf, (ULONG)buf_len) != 0);
}

#else

#ifndef BCRYPT_USE_SYSTEM_PREFERRED_RNG
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#endif

typedef LONG (NTAPI *PBCryptGenRandom)(HANDLE, PUCHAR, ULONG, ULONG);
static  PBCryptGenRandom pBCryptGenRandom = NULL;

bool _mi_prim_random_buf(void* buf, size_t buf_len) {
  if (pBCryptGenRandom == NULL) {
    HINSTANCE hDll = LoadLibrary(TEXT("bcrypt.dll"));
    if (hDll != NULL) {
      pBCryptGenRandom = (PBCryptGenRandom)(void (*)(void))GetProcAddress(hDll, "BCryptGenRandom");
    }
    if (pBCryptGenRandom == NULL) return false;
  }
  return (pBCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)buf_len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0);  
}

#endif  // MI_USE_RTLGENRANDOM

//----------------------------------------------------------------
// Thread init/done
//----------------------------------------------------------------

#if !defined(MI_SHARED_LIB)

// use thread local storage keys to detect thread ending
#include <fibersapi.h>
#if (_WIN32_WINNT < 0x600)  // before Windows Vista
WINBASEAPI DWORD WINAPI FlsAlloc( _In_opt_ PFLS_CALLBACK_FUNCTION lpCallback );
WINBASEAPI PVOID WINAPI FlsGetValue( _In_ DWORD dwFlsIndex );
WINBASEAPI BOOL  WINAPI FlsSetValue( _In_ DWORD dwFlsIndex, _In_opt_ PVOID lpFlsData );
WINBASEAPI BOOL  WINAPI FlsFree(_In_ DWORD dwFlsIndex);
#endif

static DWORD mi_fls_key = (DWORD)(-1);

static void NTAPI mi_fls_done(PVOID value) {
  mi_heap_t* heap = (mi_heap_t*)value;
  if (heap != NULL) {
    _mi_thread_done(heap);
    FlsSetValue(mi_fls_key, NULL);  // prevent recursion as _mi_thread_done may set it back to the main heap, issue #672
  }
}

void _mi_prim_thread_init_auto_done(void) {
  mi_fls_key = FlsAlloc(&mi_fls_done);
}

void _mi_prim_thread_done_auto_done(void) {
  // call thread-done on all threads (except the main thread) to prevent 
  // dangling callback pointer if statically linked with a DLL; Issue #208
  FlsFree(mi_fls_key);  
}

void _mi_prim_thread_associate_default_heap(mi_heap_t* heap) {
  mi_assert_internal(mi_fls_key != (DWORD)(-1));
  FlsSetValue(mi_fls_key, heap);
}

#else

// Dll; nothing to do as in that case thread_done is handled through the DLL_THREAD_DETACH event.

void _mi_prim_thread_init_auto_done(void) {
}

void _mi_prim_thread_done_auto_done(void) {
}

void _mi_prim_thread_associate_default_heap(mi_heap_t* heap) {
  MI_UNUSED(heap);
}

#endif



//----------------------------------------------------------------
// Remappable memory
//----------------------------------------------------------------

// tracks allocated physical memory backing a virtual address range
typedef struct mi_win_remap_info_s {
  size_t    page_count;      // allocated physical pages (with info in page_info)
  size_t    page_reserved;   // available entries in page_info
  ULONG_PTR page_info[1];
} mi_win_remap_info_t;


// allocate remap info
static mi_win_remap_info_t* mi_win_alloc_remap_info(size_t page_count) {
  const size_t remap_info_size = _mi_align_up(sizeof(mi_win_remap_info_t) + (page_count * sizeof(ULONG_PTR)), _mi_os_page_size());
  mi_win_remap_info_t* rinfo = NULL;
  bool os_is_zero = false;
  bool os_is_large = false;
  int err = _mi_prim_alloc(NULL, remap_info_size, 1, true, false, &os_is_large, &os_is_zero, (void**)&rinfo);
  if (err != 0) return NULL;
  if (!os_is_zero) { _mi_memzero_aligned(rinfo, remap_info_size); }
  rinfo->page_count = 0;
  rinfo->page_reserved = (remap_info_size - offsetof(mi_win_remap_info_t, page_info)) / sizeof(ULONG_PTR);
  return rinfo;
}

// reallocate remap info
static mi_win_remap_info_t* mi_win_realloc_remap_info(mi_win_remap_info_t* rinfo, size_t newpage_count) {
  if (rinfo == NULL) {
    return mi_win_alloc_remap_info(newpage_count);
  }
  else if (rinfo->page_reserved >= newpage_count) {
    return rinfo; // still fits
  }
  else {
    mi_win_remap_info_t* newrinfo = mi_win_alloc_remap_info(newpage_count);
    if (newrinfo == NULL) return NULL;
    newrinfo->page_count = rinfo->page_count;
    _mi_memcpy(newrinfo->page_info, rinfo->page_info, rinfo->page_count * sizeof(ULONG_PTR));
    _mi_prim_free(rinfo, sizeof(mi_win_remap_info_t) + ((rinfo->page_reserved - 1) * sizeof(ULONG_PTR)));
    return newrinfo;
  }
}

// free meta info and the managed physical pages
static int mi_win_free_remap_info(mi_win_remap_info_t* rinfo) {
  int err = 0;
  if (rinfo == NULL) return 0;
  if (rinfo->page_count > 0) {
    size_t req_pages = rinfo->page_count;
    if (!FreeUserPhysicalPages(GetCurrentProcess(), &req_pages, &rinfo->page_info[0])) {
      err = (int)GetLastError();
    }
    rinfo->page_count = 0;
  }
  if (!VirtualFree(rinfo, 0, MEM_RELEASE)) {
    err = (int)GetLastError();
  }
  return err;
}

// release physical pages that are no longer needed
static void mi_win_shrink_physical_pages(mi_win_remap_info_t* rinfo, size_t newpage_count)
{
  if (rinfo == NULL || rinfo->page_count <= newpage_count) return;
  ULONG_PTR fpages = rinfo->page_count - newpage_count;
  if (!FreeUserPhysicalPages(GetCurrentProcess(), &fpages, &rinfo->page_info[newpage_count])) {
    int err = (int)GetLastError();
    _mi_warning_message("unable to release physical memory on remap (error %d (0x%02x))\n", err, err);
  }
  rinfo->page_count = newpage_count;
}

// ensure enough physical pages are allocated
static int mi_win_ensure_physical_pages(mi_win_remap_info_t** prinfo, size_t newpage_count) 
{
  // ensure meta data is large enough
  mi_win_remap_info_t* rinfo = *prinfo;
  if (rinfo == NULL || newpage_count > rinfo->page_count) {
    rinfo = mi_win_realloc_remap_info(*prinfo, newpage_count);
    if (rinfo == NULL) return ENOMEM;
  }  
  *prinfo = rinfo;

  // allocate physical pages; todo: allow shrinking?
  if (newpage_count > rinfo->page_count) {
    mi_assert_internal(rinfo->page_reserved >= newpage_count);
    const size_t extra_pages = newpage_count - rinfo->page_count;
    ULONG_PTR req_pages = extra_pages;
    if (!AllocateUserPhysicalPages(GetCurrentProcess(), &req_pages, &rinfo->page_info[rinfo->page_count])) {
      return (int)GetLastError();
    }
    rinfo->page_count += req_pages;
    if (req_pages < extra_pages) {
      return ENOMEM;
    }
  }
  return 0;
}

// Remap physical memory to another virtual address range
static int mi_win_remap_virtual_pages(mi_win_remap_info_t* rinfo, void* oldaddr, size_t oldpage_count, void* newaddr, size_t newpage_count) {
  mi_assert_internal(rinfo != NULL && rinfo->page_count >= newpage_count);

  // unmap the old range
  if (oldaddr != NULL) {
    if (!MapUserPhysicalPages(oldaddr, oldpage_count, NULL)) {
      return (int)GetLastError();
    }
  }

  // and remap the virtual addresses from newbase
  if (!MapUserPhysicalPages(newaddr, newpage_count, &rinfo->page_info[0])) {
    // if this fails that would be very bad as we already unmapped the old range..
    // todo: try to remap old range?
    int err = (int)GetLastError();
    VirtualFree(newaddr, 0, MEM_RELEASE);
    return err;
  }

  return 0;
}

// Reserve a virtual address range to be mapped to physical memory later
int _mi_prim_remap_reserve(size_t size, bool* is_pinned, void** base, void** remap_info) {
  if (!mi_win_get_lock_memory_privilege()) return EINVAL;
  mi_assert_internal((size % _mi_os_page_size()) == 0);
  size = _mi_align_up(size, _mi_os_page_size());
  mi_win_remap_info_t** prinfo = (mi_win_remap_info_t**)remap_info;
  *prinfo = NULL;
  *is_pinned = true;
  *base = NULL;
  void* p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
  if (p == NULL) { return (int)GetLastError(); }
  *base = p;
  return 0;
}

// Remap to a new virtual address range
int _mi_prim_remap_to(void* base, void* addr, size_t size, void* newaddr, size_t newsize, bool* extend_is_zero, void** remap_info, void** new_remap_info) 
{
  *extend_is_zero = false; // todo: can we assume zero's ?
  mi_win_remap_info_t**prinfo = (mi_win_remap_info_t**)remap_info;
  mi_win_remap_info_t** pnewrinfo = (mi_win_remap_info_t**)new_remap_info;
  mi_assert_internal(base <= addr);
  mi_assert_internal(*pnewrinfo == NULL);
  mi_assert_internal((size % _mi_os_page_size()) == 0);
  mi_assert_internal((newsize % _mi_os_page_size()) == 0);
  size = _mi_align_up(size, _mi_os_page_size());
  newsize = _mi_align_up(newsize, _mi_os_page_size());

  size_t oldpage_count = _mi_divide_up(size, _mi_os_page_size());
  size_t newpage_count = _mi_divide_up(newsize, _mi_os_page_size());  
  
  // ensure we have enough physical memory for the new range
  int err = mi_win_ensure_physical_pages(prinfo, newpage_count);
  if (err != 0) { return err; }

  // remap the physical memory to the new virtual range
  err = mi_win_remap_virtual_pages(*prinfo, addr, oldpage_count, newaddr, newpage_count);
  if (err != 0) { return err; }

  // release old virtual range
  if (base != NULL) {
    if (!VirtualFree(base, 0, MEM_RELEASE)) {
      err = (int)GetLastError();
      _mi_warning_message("unable to release virtual address range on remap (error %d (0x%02x), address: %p)\n", err, err, base);
    }
  }

  // perhaps release physical pages that are no longer needed
  mi_win_shrink_physical_pages(*prinfo, newpage_count);
  
  *pnewrinfo = *prinfo;
  *prinfo = NULL;
  return 0;
}

// Free remappable memory
int _mi_prim_remap_free(void* base, size_t size, void* remap_info) {
  MI_UNUSED(size);
  int err = 0;
  // release virtual address range
  if (base != NULL) {
    if (!VirtualFree(base, 0, MEM_RELEASE)) {
      err = (int)GetLastError();
    }
  }
  // release backing physical pages
  mi_win_remap_info_t* rinfo = (mi_win_remap_info_t*)remap_info;
  if (rinfo != NULL) {
    err = mi_win_free_remap_info(rinfo);
  }
  return err;
}

