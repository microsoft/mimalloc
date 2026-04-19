#include "include/mimalloc/internal.h"
int main() {
  int x = 1;
  if mi_unlikely(x) { printf("ok\n"); }
  return 0;
}
