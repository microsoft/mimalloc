#include <stdio.h>
#include <assert.h>
#include <mimalloc.h>

void test_theap(void* p_out) {
  mi_theap_t* theap = mi_theap_new();
  void* p1 = mi_theap_malloc(theap,32);
  void* p2 = mi_theap_malloc(theap,48);
  mi_free(p_out);
  mi_theap_destroy(theap);
  //mi_theap_delete(theap); mi_free(p1); mi_free(p2);
}

void test_large() {
  const size_t N = 1000;

  for (size_t i = 0; i < N; ++i) {
    size_t sz = 1ull << 21;
    char* a = mi_mallocn_tp(char,sz);
    for (size_t k = 0; k < sz; k++) { a[k] = 'x'; }
    mi_free(a);
  }
}

int main() {
  void* p1 = mi_malloc(16);
  void* p2 = mi_malloc(1000000);
  mi_free(p1);
  mi_free(p2);
  p1 = mi_malloc(16);
  p2 = mi_malloc(16);
  mi_free(p1);
  mi_free(p2);

  test_theap(mi_malloc(32));

  p1 = mi_malloc_aligned(64, 16);
  p2 = mi_malloc_aligned(160,24);
  mi_free(p2);
  mi_free(p1);
  //test_large();

  mi_collect(true);
  mi_stats_print(NULL);
  return 0;
}
