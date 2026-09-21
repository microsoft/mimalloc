#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef void* (*mi_malloc_fn)(size_t);
typedef void  (*mi_free_fn)(void*);

static const char*  lib_path;
static mi_malloc_fn mi_malloc_p;
static mi_free_fn   mi_free_p;

static void alloc_and_free(void) {
  void* p[4];
  for (int i = 0; i < 4; i++) p[i] = mi_malloc_p((size_t)64 << i);
  for (int i = 0; i < 4; i++) mi_free_p(p[i]);
}

// T1: loads mimalloc (constructor -> mi_process_init runs here), allocates, exits.
static void* t1(void* arg) {
  (void)arg;
  printf("T1 pthread_self=%p  (dlopen + first mi_malloc happen here, then T1 exits)\n", (void*)pthread_self());
  void* h = dlopen(lib_path, RTLD_NOW);  // never dlclose'd: the library stays loaded
  if (h == NULL) { fprintf(stderr, "dlopen: %s\n", dlerror()); exit(2); }
  mi_malloc_p = (mi_malloc_fn)dlsym(h, "mi_malloc");
  mi_free_p   = (mi_free_fn)dlsym(h, "mi_free");
  if (mi_malloc_p == NULL || mi_free_p == NULL) { fprintf(stderr, "dlsym: %s\n", dlerror()); exit(2); }
  alloc_and_free();
  return (void*)pthread_self();
}

// T2: a fresh thread; with the same stack size glibc's stack cache hands it T1's TCB.
static void* t2(void* arg) {
  (void)arg;
  printf("T2 pthread_self=%p  (calling mi_malloc on this thread)\n", (void*)pthread_self());
  fflush(stdout);
  alloc_and_free();
  return (void*)pthread_self();
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <libmimalloc.so>\n", argv[0]); return 2; }
  lib_path = argv[1];
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, (size_t)4 << 20);  // same size for every thread
  pthread_t th; void* t1_self; void* t2_self;
  if (pthread_create(&th, &attr, t1, NULL) != 0 || pthread_join(th, &t1_self) != 0) return 2;
  int n = 0;
  do {  // usually the very first T2 reuses T1's TCB; loop just in case
    n++;
    if (pthread_create(&th, &attr, t2, NULL) != 0 || pthread_join(th, &t2_self) != 0) return 2;
  } while (t2_self != t1_self && n < 64);
  printf("%s (after %d T2 thread%s)\n", t2_self == t1_self ? "T2 reused T1's TCB" : "no TCB reuse observed", n, n == 1 ? "" : "s");
  printf("OK\n");
  return 0;
}
