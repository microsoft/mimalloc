#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <mimalloc.h>
#include <mimalloc-override.h>  // redefines malloc etc.
#include <mimalloc-internal.h>

typedef struct mi_visit_info_s {
  size_t area_count;
  size_t block_count;
  mi_output_fun* out;
  void* out_arg;
} mi_visit_info_t;

static bool visit(const mi_heap_t* heap, const mi_heap_area_t* area, const mi_block_info_t* info, void* arg) {
  mi_visit_info_t* varg = (mi_visit_info_t*)(arg);
  if (info==NULL) {
    _mi_fprintf(varg->out, varg->out_arg, varg->area_count==0 ? " {" : "  ]\n}\n,{");
    varg->area_count++;
    varg->block_count = 0;
    _mi_fprintf(varg->out, varg->out_arg, "\"area\": %zu, \"start\": 0x%p, \"block_size\": %zu, \"used_size\": %zu,\n  \"reserved\": %zu, \"committed\": %zu,", varg->area_count, area->blocks, area->block_size, area->used, area->reserved, area->committed);
    _mi_fprintf(varg->out, varg->out_arg, " \"blocks\": [\n");
  }
  else {
    _mi_fprintf(varg->out, varg->out_arg, varg->block_count==0 ? "   {" : "  ,{");
    varg->block_count++;
    _mi_fprintf(varg->out, varg->out_arg, "\"block\": 0x%p, \"valid\": %s, \"size\": %zu, \"usable_size\": %zu, \"allocated_size\": %zu,\n    ", info->block, info->valid ? "true" : "false", info->size, info->usable_size, info->allocated_size);
    int lineno;
    const char* fname;
    void* ret = mi_source_unpack(info->source, &fname, &lineno);
    if (fname!=NULL) _mi_fprintf(varg->out, varg->out_arg, "\"source\": \"%s:%i\" }\n", fname, lineno);
    else if (ret != NULL) _mi_fprintf(varg->out, varg->out_arg, "\"source\": \"(%p)\" }\n", ret);
    else _mi_fprintf(varg->out, varg->out_arg, "\"source\": \"\" }\n");
  }
  return true;
}

static void mi_heap_to_json(mi_heap_t* heap, mi_output_fun* out, void* arg ) {
  if (heap==NULL) heap = mi_heap_get_default();
  mi_visit_info_t info = { 0, 0, out, arg };
  _mi_fprintf(info.out, info.out_arg, "[\n");
  mi_heap_visit_blocks(heap, true, &visit, &info);
  _mi_fprintf(info.out, info.out_arg, info.area_count==0 ? "]\n" : "  ] }\n]\n");
}



static void double_free1();
static void double_free2();
static void corrupt_free();
static void block_overflow1();
static void block_overflow2();
static void dangling_ptr_write();

int main() {
  mi_version();

  // detect double frees and heap corruption
  // double_free1();
  // double_free2();
  // corrupt_free();
  // block_overflow1();
  // block_overflow2();
  // dangling_ptr_write();

  void* p1 = malloc(78);
  void* p2 = malloc(24);
  free(p1);
  p1 = mi_malloc(8);
  //char* s = strdup("hello\n");
  free(p2);
  p2 = malloc(16);
  p1 = realloc(p1, 32);
  mi_heap_to_json(NULL,NULL,NULL);
  free(p1);
  free(p2);
  //free(s);
  //mi_collect(true);

  /* now test if override worked by allocating/freeing across the api's*/
  //p1 = mi_malloc(32);
  //free(p1);
  //p2 = malloc(32);
  //mi_free(p2);
  mi_stats_print(NULL);
  return 0;
}

static void block_overflow1() {
  uint8_t* p = (uint8_t*)mi_malloc(17);
  p[18] = 0;
  free(p);
}
static void block_overflow2() {
  void* p[100];
  for (int i = 0; i < 100; i++) {
    p[i] = mi_malloc(17);
  }
  memset(p[10], 0, 90);
  memset(p[40], 0, 90);
  memset(p[79], 0, 70);
  for (int i = 99; i >= 0; i-=2) {
    if (i > 0) free(p[i - 1]);
    free(p[i]);
  }
}


static void dangling_ptr_write() {
  for (int i = 0; i < 1000; i++) {
    uint8_t* p = (uint8_t*)mi_malloc(16);
    free(p);
    p[0] = 0;
  }  
}

// The double free samples come ArcHeap [1] by Insu Yun (issue #161)
// [1]: https://arxiv.org/pdf/1903.00503.pdf

static void double_free1() {
  void* p[256];
  //uintptr_t buf[256];

  p[0] = mi_malloc(622616);
  p[1] = mi_malloc(655362);
  p[2] = mi_malloc(786432);
  mi_free(p[2]);
  // [VULN] Double free
  mi_free(p[2]);
  p[3] = mi_malloc(786456);
  // [BUG] Found overlap
  // p[3]=0x429b2ea2000 (size=917504), p[1]=0x429b2e42000 (size=786432)
  fprintf(stderr, "p3: %p-%p, p1: %p-%p, p2: %p\n", p[3], (uint8_t*)(p[3]) + 786456, p[1], (uint8_t*)(p[1]) + 655362, p[2]);
}

static void double_free2() {
  void* p[256];
  //uintptr_t buf[256];
  // [INFO] Command buffer: 0x327b2000
  // [INFO] Input size: 182
  p[0] = malloc(712352);
  p[1] = malloc(786432);
  free(p[0]);
  // [VULN] Double free
  free(p[0]);
  p[2] = malloc(786440);
  p[3] = malloc(917504);
  p[4] = malloc(786440);
  // [BUG] Found overlap
  // p[4]=0x433f1402000 (size=917504), p[1]=0x433f14c2000 (size=786432)
  fprintf(stderr, "p1: %p-%p, p2: %p-%p\n", p[4], (uint8_t*)(p[4]) + 917504, p[1], (uint8_t*)(p[1]) + 786432);
}


// Try to corrupt the heap through buffer overflow
#define N   1024
#define SZ  40

static void corrupt_free() {
  void* p[N];
  // allocate
  for (int i = 0; i < N; i++) {
    p[i] = malloc(SZ);
  }
  // free some
  for (int i = 0; i < N; i += (N/10)) {
    free(p[i]);
    p[i] = NULL;
  }
  // try to corrupt the free list
  for (int i = 0; i < N; i++) {
    if (p[i] != NULL) {
      memset(p[i], 0, SZ+32);
    }
  }
  // allocate more.. trying to trigger an allocation from a corrupted entry
  // this may need many allocations to get there (if at all)
  for (int i = 0; i < 4*4096; i++) {
    malloc(SZ);
  }
}
