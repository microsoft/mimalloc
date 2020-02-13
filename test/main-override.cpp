#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <mimalloc.h>
#include <new>
#include <vector>

#include <thread>
#include <mimalloc.h>
#include <assert.h>

// Issue #202
void thread_main() {
  mi_heap_t* heap = mi_heap_new();
  void* q = mi_heap_malloc(heap,1024);
  // mi_heap_delete(heap); // uncomment to prevent assertion
}

int main() {
  auto t1 = std::thread(thread_main);
  t1.join();
  return 0;
}

/*

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
  mi_stats_reset();  // ignore earlier allocations
  atexit(free_p);
  void* p1 = malloc(78);
  void* p2 = mi_malloc_aligned(16,24);
  free(p1);  
  p1 = malloc(8);
  char* s = mi_strdup("hello\n");
  
  //char* s = _strdup("hello\n");
  //char* buf = NULL;
  //size_t len;
  //_dupenv_s(&buf,&len,"MIMALLOC_VERBOSE"); 
  //mi_free(buf);
  
  mi_free(p2);
  p2 = malloc(16);
  p1 = realloc(p1, 32);
  free(p1);
  free(p2);
  mi_free(s);
  Test* t = new Test(42);
  delete t;
  t = new (std::nothrow) Test(42);
  delete t;  
  mi_stats_print(NULL);
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


bool test_stl_allocator1() {
  std::vector<int, mi_stl_allocator<int> > vec;
  vec.push_back(1);
  vec.pop_back();
  return vec.size() == 0;
}

struct some_struct { int i; int j; double z; };

bool test_stl_allocator2() {  
  std::vector<some_struct, mi_stl_allocator<some_struct> > vec;
  vec.push_back(some_struct());
  vec.pop_back();
  return vec.size() == 0;
}
*/