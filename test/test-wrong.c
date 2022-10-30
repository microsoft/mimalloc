#include <stdio.h>
#include <stdlib.h>
#include "mimalloc.h"

#ifdef USE_STD_MALLOC
# define mi(x) x
#else
# define mi(x) mi_##x
#endif

int main(int argc, char** argv) {
  int* p = mi(malloc)(3*sizeof(int));
  
  int* r = mi_malloc_aligned(8,16);
  mi_free(r);

  // illegal byte wise read
  char* c = (char*)mi(malloc)(3);
  printf("invalid byte: over: %d, under: %d\n", c[4], c[-1]);
  mi(free)(c);

  // undefined access
  int* q = mi(malloc)(sizeof(int));
  printf("undefined: %d\n", *q);

  // illegal int read
  printf("invalid: over: %d, under: %d\n", q[1], q[-1]);
  
  *q = 42;

  // buffer overflow
  q[1] = 43;

  // buffer underflow
  q[-1] = 44;
  
  mi(free)(q);

  
  // double free
  mi(free)(q);

  // use after free
  printf("use-after-free: %d\n", *q);

  // leak p
  // mi_free(p)
  return 0;  
}