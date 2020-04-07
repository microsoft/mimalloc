/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-atomic.h"

#include <stdio.h>  // fputs, stderr
#include <string.h> // memset


/* -----------------------------------------------------------
  Statistics operations
----------------------------------------------------------- */

static bool mi_is_in_main(void* stat) {
  return ((uint8_t*)stat >= (uint8_t*)&_mi_stats_main
         && (uint8_t*)stat < ((uint8_t*)&_mi_stats_main + sizeof(mi_stats_t)));  
}

static void mi_stat_update(mi_stat_count_t* stat, int64_t amount) {
  if (amount == 0) return;
  if (mi_is_in_main(stat))
  {
    // add atomically (for abandoned pages)
    mi_atomic_addi64(&stat->current,amount);
    if (stat->current > stat->peak) stat->peak = stat->current;  // racing.. it's ok
    if (amount > 0) {
      mi_atomic_addi64(&stat->allocated,amount);
    }
    else {
      mi_atomic_addi64(&stat->freed, -amount);
    }
  }
  else {
    // add thread local
    stat->current += amount;
    if (stat->current > stat->peak) stat->peak = stat->current;
    if (amount > 0) {
      stat->allocated += amount;
    }
    else {
      stat->freed += -amount;
    }
  }
}

void _mi_stat_counter_increase(mi_stat_counter_t* stat, size_t amount) {  
  if (mi_is_in_main(stat)) {
    mi_atomic_addi64( &stat->count, 1 );
    mi_atomic_addi64( &stat->total, (int64_t)amount );
  }
  else {
    stat->count++;
    stat->total += amount;
  }
}

void _mi_stat_increase(mi_stat_count_t* stat, size_t amount) {
  mi_stat_update(stat, (int64_t)amount);
}

void _mi_stat_decrease(mi_stat_count_t* stat, size_t amount) {
  mi_stat_update(stat, -((int64_t)amount));
}

// must be thread safe as it is called from stats_merge
static void mi_stat_add(mi_stat_count_t* stat, const mi_stat_count_t* src, int64_t unit) {
  if (stat==src) return;
  mi_atomic_addi64( &stat->allocated, src->allocated * unit);
  mi_atomic_addi64( &stat->current, src->current * unit);
  mi_atomic_addi64( &stat->freed, src->freed * unit);
  // peak scores do not work across threads..
  mi_atomic_addi64( &stat->peak, src->peak * unit);
}

static void mi_stat_counter_add(mi_stat_counter_t* stat, const mi_stat_counter_t* src, int64_t unit) {
  if (stat==src) return;
  mi_atomic_addi64( &stat->total, src->total * unit);
  mi_atomic_addi64( &stat->count, src->count * unit);
}

// must be thread safe as it is called from stats_merge
static void mi_stats_add(mi_stats_t* stats, const mi_stats_t* src) {
  if (stats==src) return;
  mi_stat_add(&stats->segments, &src->segments,1);
  mi_stat_add(&stats->pages, &src->pages,1);
  mi_stat_add(&stats->reserved, &src->reserved, 1);
  mi_stat_add(&stats->committed, &src->committed, 1);
  mi_stat_add(&stats->reset, &src->reset, 1);
  mi_stat_add(&stats->page_committed, &src->page_committed, 1);

  mi_stat_add(&stats->pages_abandoned, &src->pages_abandoned, 1);
  mi_stat_add(&stats->segments_abandoned, &src->segments_abandoned, 1);
  mi_stat_add(&stats->threads, &src->threads, 1);

  mi_stat_add(&stats->malloc, &src->malloc, 1);
  mi_stat_add(&stats->segments_cache, &src->segments_cache, 1);
  mi_stat_add(&stats->huge, &src->huge, 1);
  mi_stat_add(&stats->giant, &src->giant, 1);

  mi_stat_counter_add(&stats->pages_extended, &src->pages_extended, 1);
  mi_stat_counter_add(&stats->mmap_calls, &src->mmap_calls, 1);
  mi_stat_counter_add(&stats->commit_calls, &src->commit_calls, 1);

  mi_stat_counter_add(&stats->page_no_retire, &src->page_no_retire, 1);
  mi_stat_counter_add(&stats->searches, &src->searches, 1);
  mi_stat_counter_add(&stats->huge_count, &src->huge_count, 1);
  mi_stat_counter_add(&stats->giant_count, &src->giant_count, 1);
#if MI_STAT>1
  for (size_t i = 0; i <= MI_BIN_HUGE; i++) {
    if (src->normal[i].allocated > 0 || src->normal[i].freed > 0) {
      mi_stat_add(&stats->normal[i], &src->normal[i], 1);
    }
  }
#endif
}

/* -----------------------------------------------------------
  Display statistics
----------------------------------------------------------- */

// unit > 0 : size in binary bytes 
// unit == 0: count as decimal
// unit < 0 : count in binary
static void mi_printf_amount(int64_t n, int64_t unit, mi_output_fun* out, void* arg, const char* fmt) {
  char buf[32];
  int  len = 32;
  const char* suffix = (unit <= 0 ? " " : "b");
  const int64_t base = (unit == 0 ? 1000 : 1024);
  if (unit>0) n *= unit;

  const int64_t pos = (n < 0 ? -n : n);
  if (pos < base) {
    snprintf(buf, len, "%d %s ", (int)n, suffix);
  }
  else {
    int64_t divider = base;
    const char* magnitude = "k";
    if (pos >= divider*base) { divider *= base; magnitude = "m"; }
    if (pos >= divider*base) { divider *= base; magnitude = "g"; }
    const int64_t tens = (n / (divider/10));
    const long whole = (long)(tens/10);
    const long frac1 = (long)(tens%10);
    snprintf(buf, len, "%ld.%ld %s%s", whole, frac1, magnitude, suffix);
  }
  _mi_fprintf(out, arg, (fmt==NULL ? "%11s" : fmt), buf);
}


static void mi_print_amount(int64_t n, int64_t unit, mi_output_fun* out, void* arg) {
  mi_printf_amount(n,unit,out,arg,NULL);
}

static void mi_print_count(int64_t n, int64_t unit, mi_output_fun* out, void* arg) {
  if (unit==1) _mi_fprintf(out, arg, "%11s"," ");
          else mi_print_amount(n,0,out,arg);
}

static void mi_stat_print(const mi_stat_count_t* stat, const char* msg, int64_t unit, mi_output_fun* out, void* arg ) {
  _mi_fprintf(out, arg,"%10s:", msg);
  if (unit>0) {
    mi_print_amount(stat->peak, unit, out, arg);
    mi_print_amount(stat->allocated, unit, out, arg);
    mi_print_amount(stat->freed, unit, out, arg);
    mi_print_amount(unit, 1, out, arg);
    mi_print_count(stat->allocated, unit, out, arg);
    if (stat->allocated > stat->freed)
      _mi_fprintf(out, arg, "  not all freed!\n");
    else
      _mi_fprintf(out, arg, "  ok\n");
  }
  else if (unit<0) {
    mi_print_amount(stat->peak, -1, out, arg);
    mi_print_amount(stat->allocated, -1, out, arg);
    mi_print_amount(stat->freed, -1, out, arg);
    if (unit==-1) {
      _mi_fprintf(out, arg, "%22s", "");
    }
    else {
      mi_print_amount(-unit, 1, out, arg);
      mi_print_count((stat->allocated / -unit), 0, out, arg);
    }
    if (stat->allocated > stat->freed)
      _mi_fprintf(out, arg, "  not all freed!\n");
    else
      _mi_fprintf(out, arg, "  ok\n");
  }
  else {
    mi_print_amount(stat->peak, 1, out, arg);
    mi_print_amount(stat->allocated, 1, out, arg);
    _mi_fprintf(out, arg, "\n");
  }
}

static void mi_stat_counter_print(const mi_stat_counter_t* stat, const char* msg, mi_output_fun* out, void* arg ) {
  _mi_fprintf(out, arg, "%10s:", msg);
  mi_print_amount(stat->total, -1, out, arg);
  _mi_fprintf(out, arg, "\n");
}

static void mi_stat_counter_print_avg(const mi_stat_counter_t* stat, const char* msg, mi_output_fun* out, void* arg) {
  const int64_t avg_tens = (stat->count == 0 ? 0 : (stat->total*10 / stat->count)); 
  const long avg_whole = (long)(avg_tens/10);
  const long avg_frac1 = (long)(avg_tens%10);
  _mi_fprintf(out, arg, "%10s: %5ld.%ld avg\n", msg, avg_whole, avg_frac1);
}


static void mi_print_header(mi_output_fun* out, void* arg ) {
  _mi_fprintf(out, arg, "%10s: %10s %10s %10s %10s %10s\n", "heap stats", "peak  ", "total  ", "freed  ", "unit  ", "count  ");
}

#if MI_STAT>1
static void mi_stats_print_bins(mi_stat_count_t* all, const mi_stat_count_t* bins, size_t max, const char* fmt, mi_output_fun* out, void* arg) {
  bool found = false;
  char buf[64];
  for (size_t i = 0; i <= max; i++) {
    if (bins[i].allocated > 0) {
      found = true;
      int64_t unit = _mi_bin_size((uint8_t)i);
      snprintf(buf, 64, "%s %3zu", fmt, i);
      mi_stat_add(all, &bins[i], unit);
      mi_stat_print(&bins[i], buf, unit, out, arg);
    }
  }
  //snprintf(buf, 64, "%s all", fmt);
  //mi_stat_print(all, buf, 1);
  if (found) {
    _mi_fprintf(out, arg, "\n");
    mi_print_header(out, arg);
  }
}
#endif


static void mi_process_info(mi_msecs_t* utime, mi_msecs_t* stime, size_t* peak_rss, size_t* page_faults, size_t* page_reclaim, size_t* peak_commit);

static void _mi_stats_print(mi_stats_t* stats, mi_msecs_t elapsed, mi_output_fun* out, void* arg) mi_attr_noexcept {
  mi_print_header(out,arg);
  #if MI_STAT>1
  mi_stat_count_t normal = { 0,0,0,0 };
  mi_stats_print_bins(&normal, stats->normal, MI_BIN_HUGE, "normal",out,arg);
  mi_stat_print(&normal, "normal", 1, out, arg);
  mi_stat_print(&stats->huge, "huge", (stats->huge_count.count == 0 ? 1 : -(stats->huge.allocated / stats->huge_count.count)), out, arg);
  mi_stat_print(&stats->giant, "giant", (stats->giant_count.count == 0 ? 1 : -(stats->giant.allocated / stats->giant_count.count)), out, arg);
  mi_stat_count_t total = { 0,0,0,0 };
  mi_stat_add(&total, &normal, 1);
  mi_stat_add(&total, &stats->huge, 1);
  mi_stat_add(&total, &stats->giant, 1);
  mi_stat_print(&total, "total", 1, out, arg);
  _mi_fprintf(out, arg, "malloc requested:     ");
  mi_print_amount(stats->malloc.allocated, 1, out, arg);
  _mi_fprintf(out, arg, "\n\n");
  #endif
  mi_stat_print(&stats->reserved, "reserved", 1, out, arg);
  mi_stat_print(&stats->committed, "committed", 1, out, arg);
  mi_stat_print(&stats->reset, "reset", 1, out, arg);
  mi_stat_print(&stats->page_committed, "touched", 1, out, arg);
  mi_stat_print(&stats->segments, "segments", -1, out, arg);
  mi_stat_print(&stats->segments_abandoned, "-abandoned", -1, out, arg);
  mi_stat_print(&stats->segments_cache, "-cached", -1, out, arg);
  mi_stat_print(&stats->pages, "pages", -1, out, arg);
  mi_stat_print(&stats->pages_abandoned, "-abandoned", -1, out, arg);
  mi_stat_counter_print(&stats->pages_extended, "-extended", out, arg);
  mi_stat_counter_print(&stats->page_no_retire, "-noretire", out, arg);
  mi_stat_counter_print(&stats->mmap_calls, "mmaps", out, arg);
  mi_stat_counter_print(&stats->commit_calls, "commits", out, arg);
  mi_stat_print(&stats->threads, "threads", -1, out, arg);
  mi_stat_counter_print_avg(&stats->searches, "searches", out, arg);
  _mi_fprintf(out, arg, "%10s: %7i\n", "numa nodes", _mi_os_numa_node_count());
  if (elapsed > 0) _mi_fprintf(out, arg, "%10s: %7ld.%03ld s\n", "elapsed", elapsed/1000, elapsed%1000);

  mi_msecs_t user_time;
  mi_msecs_t sys_time;
  size_t peak_rss;
  size_t page_faults;
  size_t page_reclaim;
  size_t peak_commit;
  mi_process_info(&user_time, &sys_time, &peak_rss, &page_faults, &page_reclaim, &peak_commit);
  _mi_fprintf(out, arg, "%10s: user: %ld.%03ld s, system: %ld.%03ld s, faults: %lu, reclaims: %lu, rss: ", "process", user_time/1000, user_time%1000, sys_time/1000, sys_time%1000, (unsigned long)page_faults, (unsigned long)page_reclaim );
  mi_printf_amount((int64_t)peak_rss, 1, out, arg, "%s");
  if (peak_commit > 0) {
    _mi_fprintf(out, arg, ", commit charge: ");
    mi_printf_amount((int64_t)peak_commit, 1, out, arg, "%s");
  }
  _mi_fprintf(out, arg, "\n");
}

static mi_msecs_t mi_time_start; // = 0

static mi_stats_t* mi_stats_get_default(void) {
  mi_heap_t* heap = mi_heap_get_default();
  return &heap->tld->stats;
}

static void mi_stats_merge_from(mi_stats_t* stats) {
  if (stats != &_mi_stats_main) {
    mi_stats_add(&_mi_stats_main, stats);
    memset(stats, 0, sizeof(mi_stats_t));
  }
}

void mi_stats_reset(void) mi_attr_noexcept {
  mi_stats_t* stats = mi_stats_get_default();
  if (stats != &_mi_stats_main) { memset(stats, 0, sizeof(mi_stats_t)); }
  memset(&_mi_stats_main, 0, sizeof(mi_stats_t));
  mi_time_start = _mi_clock_start();
}

void mi_stats_merge(void) mi_attr_noexcept {
  mi_stats_merge_from( mi_stats_get_default() );
}

void _mi_stats_done(mi_stats_t* stats) {  // called from `mi_thread_done`
  mi_stats_merge_from(stats);
}

void mi_stats_print_out(mi_output_fun* out, void* arg) mi_attr_noexcept {
  mi_msecs_t elapsed = _mi_clock_end(mi_time_start);
  mi_stats_merge_from(mi_stats_get_default());
  _mi_stats_print(&_mi_stats_main, elapsed, out, arg);
}

void mi_stats_print(void* out) mi_attr_noexcept {
  // for compatibility there is an `out` parameter (which can be `stdout` or `stderr`)
  mi_stats_print_out((mi_output_fun*)out, NULL);
}

void mi_thread_stats_print_out(mi_output_fun* out, void* arg) mi_attr_noexcept {
  mi_msecs_t elapsed = _mi_clock_end(mi_time_start);
  _mi_stats_print(mi_stats_get_default(), elapsed, out, arg);
}


// ----------------------------------------------------------------
// Basic timer for convenience; use milli-seconds to avoid doubles
// ----------------------------------------------------------------
#ifdef _WIN32
#include <windows.h>
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

mi_msecs_t _mi_clock_now(void) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return mi_to_msecs(t);
}
#else
#include <time.h>
#ifdef CLOCK_REALTIME
mi_msecs_t _mi_clock_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((mi_msecs_t)t.tv_sec * 1000) + ((mi_msecs_t)t.tv_nsec / 1000000);
}
#else
// low resolution timer
mi_msecs_t _mi_clock_now(void) {
  return ((mi_msecs_t)clock() / ((mi_msecs_t)CLOCKS_PER_SEC / 1000));
}
#endif
#endif


static mi_msecs_t mi_clock_diff;

mi_msecs_t _mi_clock_start(void) {
  if (mi_clock_diff == 0.0) {
    mi_msecs_t t0 = _mi_clock_now();
    mi_clock_diff = _mi_clock_now() - t0;
  }
  return _mi_clock_now();
}

mi_msecs_t _mi_clock_end(mi_msecs_t start) {
  mi_msecs_t end = _mi_clock_now();
  return (end - start - mi_clock_diff);
}


// --------------------------------------------------------
// Basic process statistics
// --------------------------------------------------------

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#pragma comment(lib,"psapi.lib")

static mi_msecs_t filetime_msecs(const FILETIME* ftime) {
  ULARGE_INTEGER i;
  i.LowPart = ftime->dwLowDateTime;
  i.HighPart = ftime->dwHighDateTime;
  mi_msecs_t msecs = (i.QuadPart / 10000); // FILETIME is in 100 nano seconds
  return msecs;
}
static void mi_process_info(mi_msecs_t* utime, mi_msecs_t* stime, size_t* peak_rss, size_t* page_faults, size_t* page_reclaim, size_t* peak_commit) {
  FILETIME ct;
  FILETIME ut;
  FILETIME st;
  FILETIME et;
  GetProcessTimes(GetCurrentProcess(), &ct, &et, &st, &ut);
  *utime = filetime_msecs(&ut);
  *stime = filetime_msecs(&st);

  PROCESS_MEMORY_COUNTERS info;
  GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
  *peak_rss = (size_t)info.PeakWorkingSetSize;
  *page_faults = (size_t)info.PageFaultCount;
  *peak_commit = (size_t)info.PeakPagefileUsage;
  *page_reclaim = 0;
}

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#endif

static mi_msecs_t timeval_secs(const struct timeval* tv) {
  return ((mi_msecs_t)tv->tv_sec * 1000L) + ((mi_msecs_t)tv->tv_usec / 1000L);
}

static void mi_process_info(mi_msecs_t* utime, mi_msecs_t* stime, size_t* peak_rss, size_t* page_faults, size_t* page_reclaim, size_t* peak_commit) {
  struct rusage rusage;
  getrusage(RUSAGE_SELF, &rusage);
#if defined(__APPLE__) && defined(__MACH__)
  *peak_rss = rusage.ru_maxrss;
#else
  *peak_rss = rusage.ru_maxrss * 1024;
#endif
  *page_faults = rusage.ru_majflt;
  *page_reclaim = rusage.ru_minflt;
  *peak_commit = 0;
  *utime = timeval_secs(&rusage.ru_utime);
  *stime = timeval_secs(&rusage.ru_stime);
}

#else
#ifndef __wasi__
// WebAssembly instances are not processes
#pragma message("define a way to get process info")
#endif

static void mi_process_info(mi_msecs_t* utime, mi_msecs_t* stime, size_t* peak_rss, size_t* page_faults, size_t* page_reclaim, size_t* peak_commit) {
  *peak_rss = 0;
  *page_faults = 0;
  *page_reclaim = 0;
  *peak_commit = 0;
  *utime = 0;
  *stime = 0;
}
#endif
