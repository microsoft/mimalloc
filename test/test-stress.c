/* ----------------------------------------------------------------------------
Copyright (c) 2018,2019 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license.
-----------------------------------------------------------------------------*/

/* This is a stress test for the allocator, using multiple threads and
   transferring objects between threads. This is not a typical workload
   but uses a random linear size distribution. Do not use this test as a benchmark! 
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <mimalloc.h>

// argument defaults
static int THREADS = 32;    // more repeatable if THREADS <= #processors
static int N       = 20;    // scaling factor

// static int THREADS = 8;    // more repeatable if THREADS <= #processors
// static int N       = 100;  // scaling factor

#define TRANSFERS     (1000)

static volatile void* transfer[TRANSFERS];

#if (UINTPTR_MAX != UINT32_MAX)
const uintptr_t cookie = 0xbf58476d1ce4e5b9UL;
#else
const uintptr_t cookie = 0x1ce4e5b9UL;
#endif

static void* atomic_exchange_ptr(volatile void** p, void* newval);

typedef uintptr_t* random_t;

static uintptr_t pick(random_t r) {
  uintptr_t x = *r;
  #if (UINTPTR_MAX > UINT32_MAX)
    // by Sebastiano Vigna, see: <http://xoshiro.di.unimi.it/splitmix64.c>
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9UL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebUL;
  x ^= x >> 31;
  #else
    // by Chris Wellons, see: <https://nullprogram.com/blog/2018/07/31/>
  x ^= x >> 16;
  x *= 0x7feb352dUL;
  x ^= x >> 15;
  x *= 0x846ca68bUL;
  x ^= x >> 16;
  #endif
  *r = x;
  return x;
}

static bool chance(size_t perc, random_t r) {
  return (pick(r) % 100 <= perc);
}

static void* alloc_items(size_t items, random_t r) {
  if (chance(1, r)) items *= 100; // 1% huge objects;
  if (items==40) items++;              // pthreads uses that size for stack increases
  uintptr_t* p = (uintptr_t*)mi_malloc(items*sizeof(uintptr_t));
  for (uintptr_t i = 0; i < items; i++) p[i] = (items - i) ^ cookie;
  return p;
}

static void free_items(void* p) {
  if (p != NULL) {
    uintptr_t* q = (uintptr_t*)p;
    uintptr_t items = (q[0] ^ cookie);
    for (uintptr_t i = 0; i < items; i++) {
      if ((q[i]^cookie) != items - i) {
        fprintf(stderr, "memory corruption at block %p at %zu\n", p, i);
        abort();
      }
    }
  }
  mi_free(p);
}


static void stress(intptr_t tid) {
  //bench_start_thread();
  uintptr_t r = tid ^ 42;
  const size_t max_item = 128;  // in words
  const size_t max_item_retained = 10*max_item;
  size_t allocs = 25*N*(tid%8 + 1); // some threads do more
  size_t retain = allocs/2;
  void** data = NULL;
  size_t data_size = 0;
  size_t data_top = 0;
  void** retained = (void**)mi_malloc(retain*sizeof(void*));
  size_t retain_top = 0;

  while (allocs>0 || retain>0) {
    if (retain == 0 || (chance(50, &r) && allocs > 0)) {
      // 50%+ alloc
      allocs--;
      if (data_top >= data_size) {
        data_size += 100000;
        data = (void**)mi_realloc(data, data_size*sizeof(void*));
      }
      data[data_top++] = alloc_items((pick(&r) % max_item) + 1, &r);
    }
    else {
      // 25% retain
      retained[retain_top++] = alloc_items(10*((pick(&r) % max_item_retained) + 1), &r);
      retain--;
    }
    if (chance(66, &r) && data_top > 0) {
      // 66% free previous alloc
      size_t idx = pick(&r) % data_top;
      free_items(data[idx]);
      data[idx] = NULL;
    }
    if (chance(25, &r) && data_top > 0) {
      // 25% transfer-swap
      size_t data_idx = pick(&r) % data_top;
      size_t transfer_idx = pick(&r) % TRANSFERS;
      void* p = data[data_idx];
      void* q = atomic_exchange_ptr(&transfer[transfer_idx], p);
      data[data_idx] = q;
    }
  }
  // free everything that is left
  for (size_t i = 0; i < retain_top; i++) {
    free_items(retained[i]);
  }
  for (size_t i = 0; i < data_top; i++) {
    free_items(data[i]);
  }
  mi_free(retained);
  mi_free(data);
  //bench_end_thread();
}

static void run_os_threads(size_t nthreads);

int main(int argc, char** argv) {
  if (argc>=2) {
    char* end;
    long n = strtol(argv[1], &end, 10);
    if (n > 0) THREADS = n;
  }
  if (argc>=3) {
    char* end;
    long n = (strtol(argv[2], &end, 10));
    if (n > 0) N = n;
  }
  printf("start with %i threads with a %i%% load-per-thread\n", THREADS, N);  
  //int res = mi_reserve_huge_os_pages(4,1);
  //printf("(reserve huge: %i\n)", res);

  //bench_start_program();
  memset((void*)transfer, 0, TRANSFERS*sizeof(void*));
  run_os_threads(THREADS);
  for (int i = 0; i < TRANSFERS; i++) {
    free_items((void*)transfer[i]);
  }
  #ifndef NDEBUG
  mi_collect(false);
  mi_collect(true);
  #endif
  mi_stats_print(NULL);
  //bench_end_program();
  return 0;
}


#ifdef _WIN32

#include <windows.h>

static DWORD WINAPI thread_entry(LPVOID param) {
  stress((intptr_t)param);
  return 0;
}

static void run_os_threads(size_t nthreads) {
  DWORD* tids = (DWORD*)malloc(nthreads * sizeof(DWORD));
  HANDLE* thandles = (HANDLE*)malloc(nthreads * sizeof(HANDLE));
  for (uintptr_t i = 0; i < nthreads; i++) {
    thandles[i] = CreateThread(0, 4096, &thread_entry, (void*)(i), 0, &tids[i]);
  }
  for (size_t i = 0; i < nthreads; i++) {
    WaitForSingleObject(thandles[i], INFINITE);
  }
}

static void* atomic_exchange_ptr(volatile void** p, void* newval) {
  #if (INTPTR_MAX == UINT32_MAX)
  return (void*)InterlockedExchange((volatile LONG*)p, (LONG)newval);
  #else
  return (void*)InterlockedExchange64((volatile LONG64*)p, (LONG64)newval);
  #endif
}
#else

#include <pthread.h>
#include <stdatomic.h>

static void* thread_entry(void* param) {
  stress((uintptr_t)param);
  return NULL;
}

static void run_os_threads(size_t nthreads) {
  pthread_t* threads = (pthread_t*)mi_malloc(nthreads*sizeof(pthread_t));
  memset(threads, 0, sizeof(pthread_t)*nthreads);
  //pthread_setconcurrency(nthreads);
  for (uintptr_t i = 0; i < nthreads; i++) {
    pthread_create(&threads[i], NULL, &thread_entry, (void*)i);
  }
  for (size_t i = 0; i < nthreads; i++) {
    pthread_join(threads[i], NULL);
  }
}

static void* atomic_exchange_ptr(volatile void** p, void* newval) {
  return atomic_exchange_explicit((volatile _Atomic(void*)*)p, newval, memory_order_acquire);
}

#endif
