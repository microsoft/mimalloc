/* ----------------------------------------------------------------------------
Copyright (c) 2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// Fork while other threads allocate, start, and terminate; the child should never deadlock
// and be able to free blocks owned by threads that no longer exist in the child.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <mimalloc.h>

#if defined(_WIN32) || defined(__wasi__) || defined(__EMSCRIPTEN__)
int main(void) {
  printf("fork test skipped on this platform\n");
  return 0;
}
#else
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define THREADS  4
#define FORKS    100
#define LIVE     1000

static volatile int stop = 0;
static void* live[LIVE];  // allocated by a thread that is blocked during the forks
static mi_theap_t* live_theap;
static pthread_mutex_t live_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  live_cond = PTHREAD_COND_INITIALIZER;
static int live_ready = 0;

// quiesce: fork is only safe when no other thread is inside mimalloc,
// so churn threads pause at a safe point (outside mimalloc) around each fork.
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_cond = PTHREAD_COND_INITIALIZER;
static int q_pause = 0;      // request churn threads to pause
static int q_paused = 0;     // number of churn threads currently paused

static void quiesce_begin(void) {
  pthread_mutex_lock(&q_lock);
  q_pause = 1;
  while (q_paused < THREADS) { pthread_cond_wait(&q_cond, &q_lock); }
  pthread_mutex_unlock(&q_lock);
}
static void quiesce_end(void) {
  pthread_mutex_lock(&q_lock);
  q_pause = 0;
  pthread_cond_broadcast(&q_cond);
  pthread_mutex_unlock(&q_lock);
}

static size_t live_size(size_t i) {
  return 16 + (i % 64)*16;
}

static void* leaf(void* arg) {
  const uintptr_t i = (uintptr_t)arg;
  void* p = mi_malloc(16 + i%1024);
  void* q = mi_malloc((size_t)1 << (16 + i%8));  // up to 8 MiB
  mi_heap_t* heap = mi_heap_new();
  void* r = mi_heap_malloc(heap, 128);
  mi_free(r);
  mi_heap_delete(heap);
  mi_free(q);
  mi_free(p);
  return NULL;
}

static void* churn(void* arg) {
  (void)arg;
  uintptr_t i = 0;
  while (!stop) {
    // safe point outside mimalloc: pause here while a fork is in progress
    pthread_mutex_lock(&q_lock);
    while (q_pause && !stop) {
      q_paused++;
      pthread_cond_broadcast(&q_cond);
      while (q_pause && !stop) { pthread_cond_wait(&q_cond, &q_lock); }
      q_paused--;
    }
    pthread_mutex_unlock(&q_lock);
    pthread_t t;
    if (pthread_create(&t, NULL, &leaf, (void*)(i++)) == 0) { pthread_join(t, NULL); }
  }
  return NULL;
}

static void* holder(void* arg) {
  (void)arg;
  live_theap = mi_theap_get_default();
  for (size_t i = 0; i < LIVE; i++) {
    live[i] = mi_malloc(live_size(i));
    memset(live[i], (int)(i & 0xFF), live_size(i));
  }
  mi_free(mi_malloc(4096)); // leave an empty retired page for child cleanup
  pthread_mutex_lock(&live_lock);
  live_ready = 1;
  pthread_cond_broadcast(&live_cond);
  while (!stop) { pthread_cond_wait(&live_cond, &live_lock); }
  pthread_mutex_unlock(&live_lock);
  return NULL;
}

static bool child_check(void) {
  mi_theap_quiescent_abandon(live_theap); // dead threads are otherwise only abandoned lazily
  // free blocks of a thread that does not exist in the child
  for (size_t i = 0; i < LIVE; i++) {
    const unsigned char* p = (const unsigned char*)live[i];
    if (p[0] != (unsigned char)(i & 0xFF) || p[live_size(i)-1] != (unsigned char)(i & 0xFF) ||
        mi_thread_id_of(p) != 0) return false;
  }
  for (size_t i = 0; i < LIVE; i++) {
    mi_free(live[i]);
  }
  // threads and heaps still work
  pthread_t t;
  if (pthread_create(&t, NULL, &leaf, (void*)1) != 0) return false;
  pthread_join(t, NULL);
  leaf((void*)2);
  mi_collect(true);
  return true;
}

// returns 0 on success
static int run_child(int depth) {
  alarm(10);  // a deadlock terminates with SIGALRM
  pid_t pid = 0;
  if (depth > 0) {
    pid = fork();
    if (pid < 0) return 2;
    if (pid == 0) { _exit(run_child(depth-1)); }
  }
  if (!child_check()) return 1;
  if (pid > 0) {
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return 3;
  }
  return 0;
}

/* -----------------------------------------------------------
  Fork without quiescing (issue #1456): other threads allocate, free across threads,
  and start and terminate threads during the fork; a child that only calls `_exit` must not crash.
----------------------------------------------------------- */

#define U_SLOTS    1024
#define U_THREADS  4
#define U_FORKS    250

static void* u_slots[U_SLOTS];  // accessed with atomic builtins (the test does not run on Windows)
static int   u_stop;
static int   u_crashed;

static unsigned u_rand(unsigned* s) { *s = *s * 1103515245u + 12345u; return (*s >> 8); }

static void u_churn(unsigned* s, int n) {
  for (int i = 0; i < n; i++) {
    const size_t size = 8 + (u_rand(s) % 4) * (u_rand(s) % 2048);
    char* p = (char*)mi_malloc(size);
    memset(p, 'x', (size < 64 ? size : 64));
    mi_free(__atomic_exchange_n(&u_slots[u_rand(s) % U_SLOTS], p, __ATOMIC_ACQ_REL));  // often a block of another thread
  }
}

static void* u_job(void* arg) {
  unsigned s = (unsigned)(uintptr_t)arg;
  u_churn(&s, 200);
  return NULL;
}

static void* u_allocator(void* arg) {
  unsigned s = (unsigned)(uintptr_t)arg;
  while (!__atomic_load_n(&u_stop, __ATOMIC_ACQUIRE)) {
    pthread_t t;
    if (pthread_create(&t, NULL, &u_job, (void*)(uintptr_t)u_rand(&s)) == 0) { pthread_join(t, NULL); }
    u_churn(&s, 100);
  }
  return NULL;
}

static void* u_forker(void* arg) {
  unsigned s = (unsigned)(uintptr_t)arg;
  for (int i = 0; i < U_FORKS; i++) {
    u_churn(&s, 20);
    const pid_t pid = fork();
    if (pid < 0) { perror("fork"); break; }
    if (pid == 0) { _exit(0); }
    int status = 0;
    waitpid(pid, &status, 0);
    // only count segfaults as the macOS objc runtime can abort in an unquiesced child (`xpc_atfork_child`)
    if (WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS)) { __atomic_fetch_add(&u_crashed, 1, __ATOMIC_RELAXED); }
  }
  return NULL;
}

static int unquiesced_forks(void) {
  pthread_t allocators[U_THREADS];
  pthread_t forkers[U_THREADS];
  for (int i = 0; i < U_THREADS; i++) { pthread_create(&allocators[i], NULL, &u_allocator, (void*)(uintptr_t)(i+1)); }
  for (int i = 0; i < U_THREADS; i++) { pthread_create(&forkers[i], NULL, &u_forker, (void*)(uintptr_t)(1000+i)); }
  for (int i = 0; i < U_THREADS; i++) { pthread_join(forkers[i], NULL); }
  __atomic_store_n(&u_stop, 1, __ATOMIC_RELEASE);
  for (int i = 0; i < U_THREADS; i++) { pthread_join(allocators[i], NULL); }
  for (int i = 0; i < U_SLOTS; i++) { mi_free(u_slots[i]); }
  const int crashed = u_crashed;
  if (crashed > 0) { fprintf(stderr, "unquiesced forks: %d children crashed\n", crashed); }
  return crashed;
}

int main(void) {
  mi_thread_init(); // retain a different theap for the same heap in the surviving thread
  pthread_t hold;
  pthread_create(&hold, NULL, &holder, NULL);
  pthread_mutex_lock(&live_lock);
  while (!live_ready) { pthread_cond_wait(&live_cond, &live_lock); }
  pthread_mutex_unlock(&live_lock);

  pthread_t threads[THREADS];
  for (int i = 0; i < THREADS; i++) { pthread_create(&threads[i], NULL, &churn, NULL); }

  int failed = 0;
  for (int i = 0; i < FORKS; i++) {
    quiesce_begin();
    const pid_t pid = fork();
    if (pid < 0) { perror("fork"); failed++; break; }
    if (pid == 0) { _exit(run_child(i % 2)); }
    quiesce_end();
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
      fprintf(stderr, "fork %d: child terminated with signal %d%s\n", i, WTERMSIG(status), (WTERMSIG(status)==SIGALRM ? " (deadlock)" : ""));
      failed++;
    }
    else if (WEXITSTATUS(status) != 0) {
      fprintf(stderr, "fork %d: child failed with exit code %d\n", i, WEXITSTATUS(status));
      failed++;
    }
  }

  pthread_mutex_lock(&live_lock);
  stop = 1;
  pthread_cond_broadcast(&live_cond);
  pthread_mutex_unlock(&live_lock);
  pthread_mutex_lock(&q_lock);
  pthread_cond_broadcast(&q_cond);
  pthread_mutex_unlock(&q_lock);
  for (int i = 0; i < THREADS; i++) { pthread_join(threads[i], NULL); }
  pthread_join(hold, NULL);
  for (size_t i = 0; i < LIVE; i++) { mi_free(live[i]); }

  failed += unquiesced_forks();
  printf("forks: %d, failed: %d\n", FORKS, failed);
  return (failed == 0 ? 0 : 1);
}
#endif
