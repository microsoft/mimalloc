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
  int* q = mi(malloc)(sizeof(int));

  int* r = mi_malloc_aligned(8,16);
  mi_free(r);

  // undefined access
  // printf("undefined: %d\n", *q);
  
  *q = 42;

  // buffer overflow
  // q[1] = 43;
  
  mi(free)(q);

  // double free
  // mi(free)(q);

  // use after free
  printf("use-after-free: %d\n", *q);

  // leak p
  // mi_free(p)
  return 0;  
}