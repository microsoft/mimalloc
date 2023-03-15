/* ----------------------------------------------------------------------------
Copyright (c) 2018-2023, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE   // ensure mmap flags are defined
#endif

#if defined(__sun)
// illumos provides new mman.h api when any of these are defined
// otherwise the old api based on caddr_t which predates the void pointers one.
// stock solaris provides only the former, chose to atomically to discard those
// flags only here rather than project wide tough.
#undef _XOPEN_SOURCE
#undef _POSIX_C_SOURCE
#endif

#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-atomic.h"
#include "prim.h"

#include <sys/mman.h>  // mmap
#include <unistd.h>    // sysconf

#if defined(__linux__)
  #include <features.h>
  #include <fcntl.h>
  #if defined(__GLIBC__)
  #include <linux/mman.h> // linux mmap flags
  #else
  #include <sys/mman.h>
  #endif
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if !TARGET_IOS_IPHONE && !TARGET_IOS_SIMULATOR
  #include <mach/vm_statistics.h>
  #endif
#elif defined(__FreeBSD__) || defined(__DragonFly__)
  #include <sys/param.h>
  #if __FreeBSD_version >= 1200000
  #include <sys/cpuset.h>
  #include <sys/domainset.h>
  #endif
  #include <sys/sysctl.h>
#endif

//---------------------------------------------
// init
//---------------------------------------------

static bool unix_detect_overcommit(void) {
  bool os_overcommit = true;
#if defined(__linux__)
  int fd = open("/proc/sys/vm/overcommit_memory", O_RDONLY);
	if (fd >= 0) {
    char buf[32];
    ssize_t nread = read(fd, &buf, sizeof(buf));
    close(fd);
    // <https://www.kernel.org/doc/Documentation/vm/overcommit-accounting>
    // 0: heuristic overcommit, 1: always overcommit, 2: never overcommit (ignore NORESERVE)
    if (nread >= 1) {
      os_overcommit = (buf[0] == '0' || buf[0] == '1');
    }
  }
#elif defined(__FreeBSD__)
  int val = 0;
  size_t olen = sizeof(val);
  if (sysctlbyname("vm.overcommit", &val, &olen, NULL, 0) == 0) {
    os_overcommit = (val != 0);
  }
#else
  // default: overcommit is true  
#endif
  return os_overcommit;
}

void _mi_prim_mem_init( mi_os_mem_config_t* config ) {
  long psize = sysconf(_SC_PAGESIZE);
  if (psize > 0) {
    config->page_size = (size_t)psize;
    config->alloc_granularity = (size_t)psize;
  }
  config->large_page_size = 2*MI_MiB; // TODO: can we query the OS for this?
  config->has_overcommit = unix_detect_overcommit();
  config->must_free_whole = false;    // mmap can free in parts
}


//---------------------------------------------
// free
//---------------------------------------------

void _mi_prim_free(void* addr, size_t size ) {
  bool err = (munmap(addr, size) == -1);
  if (err) {
    _mi_warning_message("unable to release OS memory: %s, addr: %p, size: %zu\n", strerror(errno), addr, size);
  }
}


//---------------------------------------------
// mmap
//---------------------------------------------

static int unix_madvise(void* addr, size_t size, int advice) {
  #if defined(__sun)
  return madvise((caddr_t)addr, size, advice);  // Solaris needs cast (issue #520)
  #else
  return madvise(addr, size, advice);
  #endif
}

static void* unix_mmap_prim(void* addr, size_t size, size_t try_alignment, int protect_flags, int flags, int fd) {
  MI_UNUSED(try_alignment);
  #if defined(MAP_ALIGNED)  // BSD
  if (addr == NULL && try_alignment > 1 && (try_alignment % _mi_os_page_size()) == 0) {
    size_t n = mi_bsr(try_alignment);
    if (((size_t)1 << n) == try_alignment && n >= 12 && n <= 30) {  // alignment is a power of 2 and 4096 <= alignment <= 1GiB
      flags |= MAP_ALIGNED(n);
      void* p = mmap(addr, size, protect_flags, flags | MAP_ALIGNED(n), fd, 0);
      if (p!=MAP_FAILED) return p;
      // fall back to regular mmap
    }
  }
  #elif defined(MAP_ALIGN)  // Solaris
  if (addr == NULL && try_alignment > 1 && (try_alignment % _mi_os_page_size()) == 0) {
    void* p = mmap((void*)try_alignment, size, protect_flags, flags | MAP_ALIGN, fd, 0);  // addr parameter is the required alignment
    if (p!=MAP_FAILED) return p;
    // fall back to regular mmap
  }
  #endif
  #if (MI_INTPTR_SIZE >= 8) && !defined(MAP_ALIGNED)
  // on 64-bit systems, use the virtual address area after 2TiB for 4MiB aligned allocations
  if (addr == NULL) {
    void* hint = _mi_os_get_aligned_hint(try_alignment, size);
    if (hint != NULL) {
      void* p = mmap(hint, size, protect_flags, flags, fd, 0);
      if (p!=MAP_FAILED) return p;
      // fall back to regular mmap
    }
  }
  #endif
  // regular mmap
  void* p = mmap(addr, size, protect_flags, flags, fd, 0);
  if (p!=MAP_FAILED) return p;
  // failed to allocate
  return NULL;
}

static void* unix_mmap(void* addr, size_t size, size_t try_alignment, int protect_flags, bool large_only, bool allow_large, bool* is_large) {
  void* p = NULL;
  #if !defined(MAP_ANONYMOUS)
  #define MAP_ANONYMOUS  MAP_ANON
  #endif
  #if !defined(MAP_NORESERVE)
  #define MAP_NORESERVE  0
  #endif
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  int fd = -1;
  if (_mi_os_has_overcommit()) {
    flags |= MAP_NORESERVE;
  }
  #if defined(PROT_MAX)
  protect_flags |= PROT_MAX(PROT_READ | PROT_WRITE); // BSD
  #endif
  #if defined(VM_MAKE_TAG)
  // macOS: tracking anonymous page with a specific ID. (All up to 98 are taken officially but LLVM sanitizers had taken 99)
  int os_tag = (int)mi_option_get(mi_option_os_tag);
  if (os_tag < 100 || os_tag > 255) { os_tag = 100; }
  fd = VM_MAKE_TAG(os_tag);
  #endif
  // huge page allocation
  if ((large_only || _mi_os_use_large_page(size, try_alignment)) && allow_large) {
    static _Atomic(size_t) large_page_try_ok; // = 0;
    size_t try_ok = mi_atomic_load_acquire(&large_page_try_ok);
    if (!large_only && try_ok > 0) {
      // If the OS is not configured for large OS pages, or the user does not have
      // enough permission, the `mmap` will always fail (but it might also fail for other reasons).
      // Therefore, once a large page allocation failed, we don't try again for `large_page_try_ok` times
      // to avoid too many failing calls to mmap.
      mi_atomic_cas_strong_acq_rel(&large_page_try_ok, &try_ok, try_ok - 1);
    }
    else {
      int lflags = flags & ~MAP_NORESERVE;  // using NORESERVE on huge pages seems to fail on Linux
      int lfd = fd;
      #ifdef MAP_ALIGNED_SUPER
      lflags |= MAP_ALIGNED_SUPER;
      #endif
      #ifdef MAP_HUGETLB
      lflags |= MAP_HUGETLB;
      #endif
      #ifdef MAP_HUGE_1GB
      static bool mi_huge_pages_available = true;
      if ((size % MI_GiB) == 0 && mi_huge_pages_available) {
        lflags |= MAP_HUGE_1GB;
      }
      else
      #endif
      {
        #ifdef MAP_HUGE_2MB
        lflags |= MAP_HUGE_2MB;
        #endif
      }
      #ifdef VM_FLAGS_SUPERPAGE_SIZE_2MB
      lfd |= VM_FLAGS_SUPERPAGE_SIZE_2MB;
      #endif
      if (large_only || lflags != flags) {
        // try large OS page allocation
        *is_large = true;
        p = unix_mmap_prim(addr, size, try_alignment, protect_flags, lflags, lfd);
        #ifdef MAP_HUGE_1GB
        if (p == NULL && (lflags & MAP_HUGE_1GB) != 0) {
          mi_huge_pages_available = false; // don't try huge 1GiB pages again
          _mi_warning_message("unable to allocate huge (1GiB) page, trying large (2MiB) pages instead (error %i)\n", errno);
          lflags = ((lflags & ~MAP_HUGE_1GB) | MAP_HUGE_2MB);
          p = unix_mmap_prim(addr, size, try_alignment, protect_flags, lflags, lfd);
        }
        #endif
        if (large_only) return p;
        if (p == NULL) {
          mi_atomic_store_release(&large_page_try_ok, (size_t)8);  // on error, don't try again for the next N allocations
        }
      }
    }
  }
  // regular allocation
  if (p == NULL) {
    *is_large = false;
    p = unix_mmap_prim(addr, size, try_alignment, protect_flags, flags, fd);
    if (p != NULL) {
      #if defined(MADV_HUGEPAGE)
      // Many Linux systems don't allow MAP_HUGETLB but they support instead
      // transparent huge pages (THP). Generally, it is not required to call `madvise` with MADV_HUGE
      // though since properly aligned allocations will already use large pages if available
      // in that case -- in particular for our large regions (in `memory.c`).
      // However, some systems only allow THP if called with explicit `madvise`, so
      // when large OS pages are enabled for mimalloc, we call `madvise` anyways.
      if (allow_large && _mi_os_use_large_page(size, try_alignment)) {
        if (unix_madvise(p, size, MADV_HUGEPAGE) == 0) {
          *is_large = true; // possibly
        };
      }
      #elif defined(__sun)
      if (allow_large && _mi_os_use_large_page(size, try_alignment)) {
        struct memcntl_mha cmd = {0};
        cmd.mha_pagesize = large_os_page_size;
        cmd.mha_cmd = MHA_MAPSIZE_VA;
        if (memcntl((caddr_t)p, size, MC_HAT_ADVISE, (caddr_t)&cmd, 0, 0) == 0) {
          *is_large = true;
        }
      }
      #endif
    }
  }
  if (p == NULL) {
    _mi_warning_message("unable to allocate OS memory (%zu bytes, error code: %i, address: %p, large only: %d, allow large: %d)\n", size, errno, addr, large_only, allow_large);
  }
  return p;
}

// Note: the `try_alignment` is just a hint and the returned pointer is not guaranteed to be aligned.
void* _mi_prim_alloc(size_t size, size_t try_alignment, bool commit, bool allow_large, bool* is_large) {
  mi_assert_internal(size > 0 && (size % _mi_os_page_size()) == 0);
  mi_assert_internal(commit || !allow_large);
  mi_assert_internal(try_alignment > 0);
  
  int protect_flags = (commit ? (PROT_WRITE | PROT_READ) : PROT_NONE);
  return unix_mmap(NULL, size, try_alignment, protect_flags, false, allow_large, is_large);
}


//---------------------------------------------
// Commit/Reset
//---------------------------------------------

static void unix_mprotect_hint(int err) {
  #if defined(__linux__) && (MI_SECURE>=2) // guard page around every mimalloc page
  if (err == ENOMEM) {
    _mi_warning_message("The next warning may be caused by a low memory map limit.\n"
                        "  On Linux this is controlled by the vm.max_map_count -- maybe increase it?\n"
                        "  For example: sudo sysctl -w vm.max_map_count=262144\n");
  }
  #else
  MI_UNUSED(err);
  #endif
}


int _mi_prim_commit(void* start, size_t size, bool commit) {
  /*
  #if 0 && defined(MAP_FIXED) && !defined(__APPLE__)
  // Linux: disabled for now as mmap fixed seems much more expensive than MADV_DONTNEED (and splits VMA's?)
  if (commit) {
    // commit: just change the protection
    err = mprotect(start, csize, (PROT_READ | PROT_WRITE));
    if (err != 0) { err = errno; }
  }
  else {
    // decommit: use mmap with MAP_FIXED to discard the existing memory (and reduce rss)
    const int fd = mi_unix_mmap_fd();
    void* p = mmap(start, csize, PROT_NONE, (MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE), fd, 0);
    if (p != start) { err = errno; }
  }
  #else
  */
  int err = 0;
  if (commit) {
    // commit: ensure we can access the area
    err = mprotect(start, size, (PROT_READ | PROT_WRITE));
    if (err != 0) { err = errno; }
  }
  else {
    #if defined(MADV_DONTNEED) && MI_DEBUG == 0 && MI_SECURE == 0
    // decommit: use MADV_DONTNEED as it decreases rss immediately (unlike MADV_FREE)
    // (on the other hand, MADV_FREE would be good enough.. it is just not reflected in the stats :-( )
    err = unix_madvise(start, size, MADV_DONTNEED);
    #else
    // decommit: just disable access (also used in debug and secure mode to trap on illegal access)
    err = mprotect(start, size, PROT_NONE);
    if (err != 0) { err = errno; }
    #endif    
  }
  unix_mprotect_hint(err);
  return err;
}

int _mi_prim_reset(void* start, size_t size) {
  #if defined(MADV_FREE)
  static _Atomic(size_t) advice = MI_ATOMIC_VAR_INIT(MADV_FREE);
  int oadvice = (int)mi_atomic_load_relaxed(&advice);
  int err;
  while ((err = unix_madvise(start, size, oadvice)) != 0 && errno == EAGAIN) { errno = 0;  };
  if (err != 0 && errno == EINVAL && oadvice == MADV_FREE) {
    // if MADV_FREE is not supported, fall back to MADV_DONTNEED from now on
    mi_atomic_store_release(&advice, (size_t)MADV_DONTNEED);
    err = unix_madvise(start, size, MADV_DONTNEED);
  }
  #else
  int err = unix_madvise(start, csize, MADV_DONTNEED);
  #endif
  return err;
}

int _mi_prim_protect(void* start, size_t size, bool protect) {
  int err = mprotect(start, size, protect ? PROT_NONE : (PROT_READ | PROT_WRITE));
  if (err != 0) { err = errno; }  
  unix_mprotect_hint(err);
  return err;
}



//---------------------------------------------
// Huge page allocation
//---------------------------------------------

#if (MI_INTPTR_SIZE >= 8) && !defined(__HAIKU__)

#include <sys/syscall.h>

#ifndef MPOL_PREFERRED
#define MPOL_PREFERRED 1
#endif

#if defined(SYS_mbind)
static long mi_prim_mbind(void* start, unsigned long len, unsigned long mode, const unsigned long* nmask, unsigned long maxnode, unsigned flags) {
  return syscall(SYS_mbind, start, len, mode, nmask, maxnode, flags);
}
#else
static long mi_prim_mbind(void* start, unsigned long len, unsigned long mode, const unsigned long* nmask, unsigned long maxnode, unsigned flags) {
  MI_UNUSED(start); MI_UNUSED(len); MI_UNUSED(mode); MI_UNUSED(nmask); MI_UNUSED(maxnode); MI_UNUSED(flags);
  return 0;
}
#endif

void* _mi_prim_alloc_huge_os_pages(void* addr, size_t size, int numa_node) {
  bool is_large = true;
  void* p = unix_mmap(addr, size, MI_SEGMENT_SIZE, PROT_READ | PROT_WRITE, true, true, &is_large);
  if (p == NULL) return NULL;
  if (numa_node >= 0 && numa_node < 8*MI_INTPTR_SIZE) { // at most 64 nodes
    unsigned long numa_mask = (1UL << numa_node);
    // TODO: does `mbind` work correctly for huge OS pages? should we
    // use `set_mempolicy` before calling mmap instead?
    // see: <https://lkml.org/lkml/2017/2/9/875>
    long err = mi_prim_mbind(p, size, MPOL_PREFERRED, &numa_mask, 8*MI_INTPTR_SIZE, 0);
    if (err != 0) {
      _mi_warning_message("failed to bind huge (1GiB) pages to numa node %d: %s\n", numa_node, strerror(errno));
    }
  }
  return p;
}

#else

void* _mi_prim_alloc_huge_os_pages(void* addr, size_t size, int numa_node) {
  MI_UNUSED(addr); MI_UNUSED(size); MI_UNUSED(numa_node);
  return NULL;
}

#endif

//---------------------------------------------
// NUMA nodes
//---------------------------------------------

#if defined(__linux__)

#include <sys/syscall.h>  // getcpu
#include <stdio.h>        // access

size_t _mi_prim_numa_node(void) {
  #ifdef SYS_getcpu
    unsigned long node = 0;
    unsigned long ncpu = 0;
    long err = syscall(SYS_getcpu, &ncpu, &node, NULL);
    if (err != 0) return 0;
    return node;
  #else
    return 0;
  #endif
}

size_t _mi_prim_numa_node_count(void) {
  char buf[128];
  unsigned node = 0;
  for(node = 0; node < 256; node++) {
    // enumerate node entries -- todo: it there a more efficient way to do this? (but ensure there is no allocation)
    snprintf(buf, 127, "/sys/devices/system/node/node%u", node + 1);
    if (access(buf,R_OK) != 0) break;
  }
  return (node+1);
}

#elif defined(__FreeBSD__) && __FreeBSD_version >= 1200000

size_t mi_prim_numa_node(void) {
  domainset_t dom;
  size_t node;
  int policy;
  if (cpuset_getdomain(CPU_LEVEL_CPUSET, CPU_WHICH_PID, -1, sizeof(dom), &dom, &policy) == -1) return 0ul;
  for (node = 0; node < MAXMEMDOM; node++) {
    if (DOMAINSET_ISSET(node, &dom)) return node;
  }
  return 0ul;
}

size_t _mi_prim_numa_node_count(void) {
  size_t ndomains = 0;
  size_t len = sizeof(ndomains);
  if (sysctlbyname("vm.ndomains", &ndomains, &len, NULL, 0) == -1) return 0ul;
  return ndomains;
}

#elif defined(__DragonFly__)

size_t _mi_prim_numa_node(void) {
  // TODO: DragonFly does not seem to provide any userland means to get this information.
  return 0ul;
}

size_t _mi_prim_numa_node_count(void) {
  size_t ncpus = 0, nvirtcoresperphys = 0;
  size_t len = sizeof(size_t);
  if (sysctlbyname("hw.ncpu", &ncpus, &len, NULL, 0) == -1) return 0ul;
  if (sysctlbyname("hw.cpu_topology_ht_ids", &nvirtcoresperphys, &len, NULL, 0) == -1) return 0ul;
  return nvirtcoresperphys * ncpus;
}

#else

size_t _mi_prim_numa_node(void) {
  return 0;
}

size_t _mi_prim_numa_node_count(void) {
  return 1;
}

#endif

// ----------------------------------------------------------------
// Clock
// ----------------------------------------------------------------

#include <time.h>

#if defined(CLOCK_REALTIME) || defined(CLOCK_MONOTONIC)

mi_msecs_t _mi_prim_clock_now(void) {
  struct timespec t;
  #ifdef CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &t);
  #else
  clock_gettime(CLOCK_REALTIME, &t);
  #endif
  return ((mi_msecs_t)t.tv_sec * 1000) + ((mi_msecs_t)t.tv_nsec / 1000000);
}

#else

// low resolution timer
mi_msecs_t _mi_prim_clock_now(void) {
  #if !defined(CLOCKS_PER_SEC) || (CLOCKS_PER_SEC == 1000) || (CLOCKS_PER_SEC == 0)
  return (mi_msecs_t)clock();  
  #elif (CLOCKS_PER_SEC < 1000)
  return (mi_msecs_t)clock() * (1000 / (mi_msecs_t)CLOCKS_PER_SEC);  
  #else
  return (mi_msecs_t)clock() / ((mi_msecs_t)CLOCKS_PER_SEC / 1000);
  #endif
}

#endif




//----------------------------------------------------------------
// Process info
//----------------------------------------------------------------

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__) || defined(__HAIKU__)
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#if defined(__HAIKU__)
#include <kernel/OS.h>
#endif

static mi_msecs_t timeval_secs(const struct timeval* tv) {
  return ((mi_msecs_t)tv->tv_sec * 1000L) + ((mi_msecs_t)tv->tv_usec / 1000L);
}

void _mi_prim_process_info(mi_msecs_t* utime, mi_msecs_t* stime, size_t* current_rss, size_t* peak_rss, size_t* current_commit, size_t* peak_commit, size_t* page_faults)
{
  struct rusage rusage;
  getrusage(RUSAGE_SELF, &rusage);
  *utime = timeval_secs(&rusage.ru_utime);
  *stime = timeval_secs(&rusage.ru_stime);
#if !defined(__HAIKU__)
  *page_faults = rusage.ru_majflt;
#endif
  // estimate commit using our stats
  *peak_commit    = (size_t)(mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&_mi_stats_main.committed.peak));
  *current_commit = (size_t)(mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&_mi_stats_main.committed.current));
  *current_rss    = *current_commit;  // estimate
#if defined(__HAIKU__)
  // Haiku does not have (yet?) a way to
  // get these stats per process
  thread_info tid;
  area_info mem;
  ssize_t c;
  get_thread_info(find_thread(0), &tid);
  while (get_next_area_info(tid.team, &c, &mem) == B_OK) {
    *peak_rss += mem.ram_size;
  }
  *page_faults = 0;
#elif defined(__APPLE__)
  *peak_rss = rusage.ru_maxrss;         // BSD reports in bytes
  struct mach_task_basic_info info;
  mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
    *current_rss = (size_t)info.resident_size;
  }
#else
  *peak_rss = rusage.ru_maxrss * 1024;  // Linux reports in KiB
#endif
}

#else

#ifndef __wasi__
// WebAssembly instances are not processes
#pragma message("define a way to get process info")
#endif

void _mi_prim_process_info(mi_msecs_t* utime, mi_msecs_t* stime, size_t* current_rss, size_t* peak_rss, size_t* current_commit, size_t* peak_commit, size_t* page_faults)
{
  *peak_commit    = (size_t)(mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&_mi_stats_main.committed.peak));
  *current_commit = (size_t)(mi_atomic_loadi64_relaxed((_Atomic(int64_t)*)&_mi_stats_main.committed.current));
  *peak_rss    = *peak_commit;
  *current_rss = *current_commit;
  *page_faults = 0;
  *utime = 0;
  *stime = 0;
}

#endif


//----------------------------------------------------------------
// Output
//----------------------------------------------------------------

void _mi_prim_out_stderr( const char* msg ) {
  fputs(msg,stderr);
}


//----------------------------------------------------------------
// Environment
//----------------------------------------------------------------

#if !defined(MI_USE_ENVIRON) || (MI_USE_ENVIRON!=0)
// On Posix systemsr use `environ` to acces environment variables
// even before the C runtime is initialized.
#if defined(__APPLE__) && defined(__has_include) && __has_include(<crt_externs.h>)
#include <crt_externs.h>
static char** mi_get_environ(void) {
  return (*_NSGetEnviron());
}
#else
extern char** environ;
static char** mi_get_environ(void) {
  return environ;
}
#endif
bool _mi_prim_getenv(const char* name, char* result, size_t result_size) {
  if (name==NULL) return false;
  const size_t len = _mi_strlen(name);
  if (len == 0) return false;
  char** env = mi_get_environ();
  if (env == NULL) return false;
  // compare up to 256 entries
  for (int i = 0; i < 256 && env[i] != NULL; i++) {
    const char* s = env[i];
    if (_mi_strnicmp(name, s, len) == 0 && s[len] == '=') { // case insensitive
      // found it
      _mi_strlcpy(result, s + len + 1, result_size);
      return true;
    }
  }
  return false;
}
#else
// fallback: use standard C `getenv` but this cannot be used while initializing the C runtime
bool _mi_prim_getenv(const char* name, char* result, size_t result_size) {
  // cannot call getenv() when still initializing the C runtime.
  if (_mi_preloading()) return false;
  const char* s = getenv(name);
  if (s == NULL) {
    // we check the upper case name too.
    char buf[64+1];
    size_t len = _mi_strnlen(name,sizeof(buf)-1);
    for (size_t i = 0; i < len; i++) {
      buf[i] = _mi_toupper(name[i]);
    }
    buf[len] = 0;
    s = getenv(buf);
  }
  if (s == NULL || _mi_strnlen(s,result_size) >= result_size)  return false;
  _mi_strlcpy(result, s, result_size);
  return true;
}
#endif  // !MI_USE_ENVIRON
