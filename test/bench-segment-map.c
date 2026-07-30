/* ----------------------------------------------------------------------------
Copyright (c) 2026 Microsoft Corporation
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* Measure contended segment-map updates without including OS allocation costs.

   Build:
     cmake -S . -B out/segment-map -DCMAKE_BUILD_TYPE=Release \
       -DMI_NO_OPT_ARCH=ON -DMI_BUILD_SEGMENT_MAP_BENCH=ON
     cmake --build out/segment-map --target mimalloc-bench-segment-map

   Run:
     taskset -c 0-7 out/segment-map/mimalloc-bench-segment-map 8 5000000

   Each thread repeatedly sets and clears a separate segment bit. Consecutive
   synthetic segments ensure those bits share one atomic segment-map word.
*/

#define _GNU_SOURCE

#include <mimalloc.h>
#include <mimalloc/internal.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

typedef struct mi_segment_map_bench_s {
  int threads;
  size_t iterations;
  mi_segment_t* segments;
  pthread_barrier_t barrier;
} mi_segment_map_bench_t;

typedef struct mi_segment_map_worker_s {
  mi_segment_map_bench_t* bench;
  int id;
} mi_segment_map_worker_t;

static double elapsed_seconds(struct timespec start, struct timespec end) {
  return (double)(end.tv_sec - start.tv_sec) +
         (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static void* segment_map_worker(void* argument) {
  mi_segment_map_worker_t* worker = (mi_segment_map_worker_t*)argument;
  mi_segment_map_bench_t* bench = worker->bench;
  mi_segment_t* segment =
      (mi_segment_t*)((uint8_t*)bench->segments + (size_t)worker->id * MI_SEGMENT_SIZE);

  pthread_barrier_wait(&bench->barrier);
  for (size_t i = 0; i < bench->iterations; i++) {
    _mi_segment_map_allocated_at(segment);
    _mi_segment_map_freed_at(segment);
  }
  return NULL;
}

static void* reserve_segment_range(size_t size) {
  for (uintptr_t address = (uintptr_t)1 << 40;
       address < ((uintptr_t)32 << 40);
       address += (uintptr_t)1 << 40) {
    void* mapping = mmap((void*)address, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mapping != MAP_FAILED) return mapping;
  }
  return NULL;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s THREADS ITERATIONS\n", argv[0]);
    return 2;
  }

  mi_segment_map_bench_t bench;
  bench.threads = atoi(argv[1]);
  bench.iterations = (size_t)strtoull(argv[2], NULL, 10);
  if (bench.threads <= 0 || bench.threads > (int)MI_INTPTR_BITS ||
      bench.iterations == 0) {
    fprintf(stderr, "threads must be between 1 and %d; iterations must be positive\n",
            MI_INTPTR_BITS);
    return 2;
  }

  const size_t range_size = (size_t)bench.threads * MI_SEGMENT_SIZE;
  bench.segments = (mi_segment_t*)reserve_segment_range(range_size);
  if (bench.segments == NULL) {
    fprintf(stderr, "unable to reserve a tracked segment-aligned address range\n");
    return 1;
  }

  for (int i = 0; i < bench.threads; i++) {
    mi_segment_t* segment =
        (mi_segment_t*)((uint8_t*)bench.segments + (size_t)i * MI_SEGMENT_SIZE);
    segment->memid = _mi_memid_create(MI_MEM_OS);
    _mi_segment_map_allocated_at(segment);
    _mi_segment_map_freed_at(segment);
  }

  pthread_t* threads = (pthread_t*)calloc((size_t)bench.threads, sizeof(*threads));
  mi_segment_map_worker_t* workers =
      (mi_segment_map_worker_t*)calloc((size_t)bench.threads, sizeof(*workers));
  if (threads == NULL || workers == NULL) {
    free(workers);
    free(threads);
    munmap(bench.segments, range_size);
    return 1;
  }

  pthread_barrier_init(&bench.barrier, NULL, (unsigned)bench.threads + 1);
  for (int i = 0; i < bench.threads; i++) {
    workers[i].bench = &bench;
    workers[i].id = i;
    if (pthread_create(&threads[i], NULL, segment_map_worker, &workers[i]) != 0) {
      abort();
    }
  }

  struct timespec start;
  struct timespec end;
  pthread_barrier_wait(&bench.barrier);
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int i = 0; i < bench.threads; i++) {
    pthread_join(threads[i], NULL);
  }
  clock_gettime(CLOCK_MONOTONIC, &end);

  const double seconds = elapsed_seconds(start, end);
  const double operations =
      2.0 * (double)bench.threads * (double)bench.iterations;
  printf("elapsed: %.3f s\n", seconds);
  printf("segment-map operation: %.3f ns\n", seconds * 1000000000.0 / operations);

  pthread_barrier_destroy(&bench.barrier);
  free(workers);
  free(threads);
  munmap(bench.segments, range_size);
  return 0;
}
