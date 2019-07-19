#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <mimalloc.h>
#include <mimalloc-override.h>

#include <new>

static void* p = malloc(8);

void free_p() {
  free(p);
  return;
}

class Test {
private:
  int i;
public:
  Test(int x) { i = x; }
  ~Test() { }
};


int main() {   
  //mi_malloc_override();
  mi_stats_reset();    
  atexit(free_p);
  void* p1 = malloc(78);
  void* p2 = _aligned_malloc(24,16);
  free(p1);
  p1 = malloc(8);
  char* s = _strdup("hello\n");
  _aligned_free(p2);
  p2 = malloc(16);
  p1 = realloc(p1, 32);
  free(p1);
  free(p2);
  free(s);
  Test* t = new Test(42);
  delete t;
  t = new (std::nothrow) Test(42);
  delete t;
  return 0;
}

class Static {
private:
  void* p;
public:
  Static() {
    p = malloc(64);
    return;
  }
  ~Static() {
    free(p);
    return;
  }
};

static Static s = Static();
