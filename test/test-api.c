/* ----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Walloc-size-larger-than="
#endif

/*
Testing allocators is difficult as bugs may only surface after particular
allocation patterns. The main approach to testing _mimalloc_ is therefore
to have extensive internal invariant checking (see `page_is_valid` in `page.c`
for example), which is enabled in debug mode with `-DMI_DEBUG_FULL=ON`.
The main testing is then to run `mimalloc-bench` [1] using full invariant checking
to catch any potential problems over a wide range of intensive allocation bench
marks.

However, this does not test well for the entire API surface. In this test file
we therefore test the API over various inputs. Please add more tests :-)

[1] https://github.com/daanx/mimalloc-bench
*/

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#ifdef __cplusplus
#include <vector>
#endif

#include "mimalloc.h"
// #include "mimalloc-internal.h"
#include "mimalloc-types.h" // for MI_DEBUG

// ---------------------------------------------------------------------------
// Test macros: CHECK(name,predicate) and CHECK_BODY(name,body)
// ---------------------------------------------------------------------------
static int ok = 0;
static int failed = 0;

#define CHECK_BODY(name,body) \
 do { \
  fprintf(stderr,"test: %s...  ", name ); \
  bool result = true;                                     \
  do { body } while(false);                                \
  if (!(result)) {                                        \
    failed++; \
    fprintf(stderr,                                       \
            "\n  FAILED: %s:%d:\n  %s\n",                 \
            __FILE__,                                     \
            __LINE__,                                     \
            #body);                                       \
    /* exit(1); */ \
  } \
  else { \
    ok++;                               \
    fprintf(stderr,"ok.\n");                    \
  }                                             \
 } while (false)

#define CHECK(name,expr)      CHECK_BODY(name,{ result = (expr); })

// ---------------------------------------------------------------------------
// Test functions
// ---------------------------------------------------------------------------
bool test_heap1(void);
bool test_heap2(void);
bool test_stl_allocator1(void);
bool test_stl_allocator2(void);

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------
bool check_zero_init(uint8_t* p, size_t size);
#if MI_DEBUG >= 2
bool check_debug_fill_uninit(uint8_t* p, size_t size);
bool check_debug_fill_freed(uint8_t* p, size_t size);
#endif

// ---------------------------------------------------------------------------
// Main testing
// ---------------------------------------------------------------------------
int main(void) {
  mi_option_disable(mi_option_verbose);

  // ---------------------------------------------------
  // Malloc
  // ---------------------------------------------------

  CHECK_BODY("malloc-zero",{
    void* p = mi_malloc(0); mi_free(p);
  });
  CHECK_BODY("malloc-nomem1",{
    result = (mi_malloc((size_t)PTRDIFF_MAX + (size_t)1) == NULL);
  });
  CHECK_BODY("malloc-null",{
    mi_free(NULL);
  });
  CHECK_BODY("calloc-overflow",{
    // use (size_t)&mi_calloc to get some number without triggering compiler warnings
    result = (mi_calloc((size_t)&mi_calloc,SIZE_MAX/1000) == NULL);
  });
  CHECK_BODY("calloc0",{
    result = (mi_usable_size(mi_calloc(0,1000)) <= 16);
  });

  // ---------------------------------------------------
  // Zeroing allocation
  // ---------------------------------------------------
  CHECK_BODY("zalloc-small", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_zalloc(zalloc_size);
    result = check_zero_init(p, zalloc_size);
    mi_free(p);
  });
  CHECK_BODY("zalloc-large", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_zalloc(zalloc_size);
    result = check_zero_init(p, zalloc_size);
    mi_free(p);
  });
  CHECK_BODY("zalloc_small", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_zalloc_small(zalloc_size);
    result = check_zero_init(p, zalloc_size);
    mi_free(p);
  });

  CHECK_BODY("calloc-small", {
    size_t calloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_calloc(calloc_size, 1);
    result = check_zero_init(p, calloc_size);
    mi_free(p);
  });
  CHECK_BODY("calloc-large", {
    size_t calloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_calloc(calloc_size, 1);
    result = check_zero_init(p, calloc_size);
    mi_free(p);
  });

  CHECK_BODY("rezalloc-small", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_zalloc(zalloc_size);
    result = check_zero_init(p, zalloc_size);
    zalloc_size *= 3;
    p = (uint8_t*)mi_rezalloc(p, zalloc_size);
    result &= check_zero_init(p, zalloc_size);
    mi_free(p);
  });
  CHECK_BODY("rezalloc-large", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_zalloc(zalloc_size);
    result = check_zero_init(p, zalloc_size);
    zalloc_size *= 3;
    p = (uint8_t*)mi_rezalloc(p, zalloc_size);
    result &= check_zero_init(p, zalloc_size);
    mi_free(p);
  });

  CHECK_BODY("recalloc-small", {
    size_t calloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_calloc(calloc_size, 1);
    result = check_zero_init(p, calloc_size);
    calloc_size *= 3;
    p = (uint8_t*)mi_recalloc(p, calloc_size, 1);
    result &= check_zero_init(p, calloc_size);
    mi_free(p);
  });
  CHECK_BODY("recalloc-large", {
    size_t calloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_calloc(calloc_size, 1);
    result = check_zero_init(p, calloc_size);
    calloc_size *= 3;
    p = (uint8_t*)mi_recalloc(p, calloc_size, 1);
    result &= check_zero_init(p, calloc_size);
    mi_free(p);
  });

  CHECK_BODY("zalloc_aligned-small", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_zalloc_aligned(zalloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, zalloc_size);
    mi_free(p);
  });
  CHECK_BODY("zalloc_aligned-large", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_zalloc_aligned(zalloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, zalloc_size);
    mi_free(p);
  });

  CHECK_BODY("calloc_aligned-small", {
    size_t calloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_calloc_aligned(calloc_size, 1, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, calloc_size);
    mi_free(p);
  });
  CHECK_BODY("calloc_aligned-large", {
    size_t calloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_calloc_aligned(calloc_size, 1, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, calloc_size);
    mi_free(p);
  });

  CHECK_BODY("rezalloc_aligned-small", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_zalloc_aligned(zalloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, zalloc_size);
    zalloc_size *= 3;
    p = (uint8_t*)mi_rezalloc_aligned(p, zalloc_size, MI_MAX_ALIGN_SIZE * 2);
    result &= check_zero_init(p, zalloc_size);
    mi_free(p);
  });
  CHECK_BODY("rezalloc_aligned-large", {
    size_t zalloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_zalloc_aligned(zalloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, zalloc_size);
    zalloc_size *= 3;
    p = (uint8_t*)mi_rezalloc_aligned(p, zalloc_size, MI_MAX_ALIGN_SIZE * 2);
    result &= check_zero_init(p, zalloc_size);
    mi_free(p);
  });

  CHECK_BODY("recalloc_aligned-small", {
    size_t calloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_calloc_aligned(calloc_size, 1, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, calloc_size);
    calloc_size *= 3;
    p = (uint8_t*)mi_recalloc_aligned(p, calloc_size, 1, MI_MAX_ALIGN_SIZE * 2);
    result &= check_zero_init(p, calloc_size);
    mi_free(p);
  });
  CHECK_BODY("recalloc_aligned-large", {
    size_t calloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_calloc_aligned(calloc_size, 1, MI_MAX_ALIGN_SIZE * 2);
    result = check_zero_init(p, calloc_size);
    calloc_size *= 3;
    p = (uint8_t*)mi_recalloc_aligned(p, calloc_size, 1, MI_MAX_ALIGN_SIZE * 2);
    result &= check_zero_init(p, calloc_size);
    mi_free(p);
  });


#if MI_DEBUG >= 2
  // ---------------------------------------------------
  // Debug filling
  // ---------------------------------------------------
  CHECK_BODY("malloc-uninit-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_malloc(malloc_size);
    result = check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });
  CHECK_BODY("malloc-uninit-large", {
    size_t malloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_malloc(malloc_size);
    result = check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });

  CHECK_BODY("malloc_small-uninit-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_malloc_small(malloc_size);
    result = check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });

  CHECK_BODY("realloc-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_malloc(malloc_size);
    result = check_debug_fill_uninit(p, malloc_size);
    malloc_size *= 3;
    p = (uint8_t*)mi_realloc(p, malloc_size);
    result &= check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });
  CHECK_BODY("realloc-large", {
    size_t malloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_malloc(malloc_size);
    result = check_debug_fill_uninit(p, malloc_size);
    malloc_size *= 3;
    p = (uint8_t*)mi_realloc(p, malloc_size);
    result &= check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });

  CHECK_BODY("mallocn-uninit-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_mallocn(malloc_size, 1);
    result = check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });
  CHECK_BODY("mallocn-uninit-large", {
    size_t malloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_mallocn(malloc_size, 1);
    result = check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });

  CHECK_BODY("reallocn-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_mallocn(malloc_size, 1);
    result = check_debug_fill_uninit(p, malloc_size);
    malloc_size *= 3;
    p = (uint8_t*)mi_reallocn(p, malloc_size, 1);
    result &= check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });
  CHECK_BODY("reallocn-large", {
    size_t malloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_mallocn(malloc_size, 1);
    result = check_debug_fill_uninit(p, malloc_size);
    malloc_size *= 3;
    p = (uint8_t*)mi_reallocn(p, malloc_size, 1);
    result &= check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });

  CHECK_BODY("malloc_aligned-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_malloc_aligned(malloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });
  CHECK_BODY("malloc_aligned-large", {
    size_t malloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_malloc_aligned(malloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });

  CHECK_BODY("realloc_aligned-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_malloc_aligned(malloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_debug_fill_uninit(p, malloc_size);
    malloc_size *= 3;
    p = (uint8_t*)mi_realloc_aligned(p, malloc_size, MI_MAX_ALIGN_SIZE * 2);
    result &= check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });
  CHECK_BODY("realloc_aligned-large", {
    size_t malloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_malloc_aligned(malloc_size, MI_MAX_ALIGN_SIZE * 2);
    result = check_debug_fill_uninit(p, malloc_size);
    malloc_size *= 3;
    p = (uint8_t*)mi_realloc_aligned(p, malloc_size, MI_MAX_ALIGN_SIZE * 2);
    result &= check_debug_fill_uninit(p, malloc_size);
    mi_free(p);
  });


  CHECK_BODY("fill-freed-small", {
    size_t malloc_size = MI_SMALL_SIZE_MAX / 2;
    uint8_t* p = (uint8_t*)mi_malloc(malloc_size);
    mi_free(p);
    // First sizeof(void*) bytes will contain housekeeping data, skip these
    result = check_debug_fill_freed(p + sizeof(void*), malloc_size - sizeof(void*));
  });
  CHECK_BODY("fill-freed-large", {
    size_t malloc_size = MI_SMALL_SIZE_MAX * 2;
    uint8_t* p = (uint8_t*)mi_malloc(malloc_size);
    mi_free(p);
    // First sizeof(void*) bytes will contain housekeeping data, skip these
    result = check_debug_fill_freed(p + sizeof(void*), malloc_size - sizeof(void*));
  });
#endif


  // ---------------------------------------------------
  // Extended
  // ---------------------------------------------------  
  CHECK_BODY("posix_memalign1", {
    void* p = &p;
    int err = mi_posix_memalign(&p, sizeof(void*), 32);
    result = ((err==0 && (uintptr_t)p % sizeof(void*) == 0) || p==&p);
    mi_free(p);
  });
  CHECK_BODY("posix_memalign_no_align", {
    void* p = &p;
    int err = mi_posix_memalign(&p, 3, 32);
    result = (err==EINVAL && p==&p);
  });
  CHECK_BODY("posix_memalign_zero", {
    void* p = &p;
    int err = mi_posix_memalign(&p, sizeof(void*), 0);
    mi_free(p);
    result = (err==0);
  });
  CHECK_BODY("posix_memalign_nopow2", {
    void* p = &p;
    int err = mi_posix_memalign(&p, 3*sizeof(void*), 32);
    result = (err==EINVAL && p==&p);
  });
  CHECK_BODY("posix_memalign_nomem", {
    void* p = &p;
    int err = mi_posix_memalign(&p, sizeof(void*), SIZE_MAX);
    result = (err==ENOMEM && p==&p);
  });

  // ---------------------------------------------------
  // Aligned API
  // ---------------------------------------------------
  CHECK_BODY("malloc-aligned1", {
    void* p = mi_malloc_aligned(32,32); result = (p != NULL && (uintptr_t)(p) % 32 == 0); mi_free(p);
  });
  CHECK_BODY("malloc-aligned2", {
    void* p = mi_malloc_aligned(48,32); result = (p != NULL && (uintptr_t)(p) % 32 == 0); mi_free(p);
  });
  CHECK_BODY("malloc-aligned3", {
    void* p1 = mi_malloc_aligned(48,32); bool result1 = (p1 != NULL && (uintptr_t)(p1) % 32 == 0); 
    void* p2 = mi_malloc_aligned(48,32); bool result2 = (p2 != NULL && (uintptr_t)(p2) % 32 == 0);
    mi_free(p2);
    mi_free(p1);
    result = (result1&&result2);
  });
  CHECK_BODY("malloc-aligned4", {
    void* p;
    bool ok = true;
    for (int i = 0; i < 8 && ok; i++) {
      p = mi_malloc_aligned(8, 16);
      ok = (p != NULL && (uintptr_t)(p) % 16 == 0); mi_free(p);
    }
    result = ok;
  });
  CHECK_BODY("malloc-aligned5", {
    void* p = mi_malloc_aligned(4097,4096); size_t usable = mi_usable_size(p); result = usable >= 4097 && usable < 10000; mi_free(p);
  });
  CHECK_BODY("malloc-aligned6", {
    bool ok = true;
    for (size_t align = 1; align <= MI_ALIGNMENT_MAX && ok; align *= 2) {
      void* ps[8];
      for (int i = 0; i < 8 && ok; i++) {
        ps[i] = mi_malloc_aligned(align*13 /*size*/, align);
        if (ps[i] == NULL || (uintptr_t)(ps[i]) % align != 0) {
          ok = false;
        }
      }
      for (int i = 0; i < 8 && ok; i++) {
        mi_free(ps[i]);
      }
    }
    result = ok;
  });
  CHECK_BODY("malloc-aligned7", {
    void* p = mi_malloc_aligned(1024,MI_ALIGNMENT_MAX); mi_free(p);
    });
  CHECK_BODY("malloc-aligned8", {
    void* p = mi_malloc_aligned(1024,2*MI_ALIGNMENT_MAX); mi_free(p);
  });
  CHECK_BODY("malloc-aligned-at1", {
    void* p = mi_malloc_aligned_at(48,32,0); result = (p != NULL && ((uintptr_t)(p) + 0) % 32 == 0); mi_free(p);
  });
  CHECK_BODY("malloc-aligned-at2", {
    void* p = mi_malloc_aligned_at(50,32,8); result = (p != NULL && ((uintptr_t)(p) + 8) % 32 == 0); mi_free(p);
  });  
  CHECK_BODY("memalign1", {
    void* p;
    bool ok = true;
    for (int i = 0; i < 8 && ok; i++) {
      p = mi_memalign(16,8);
      ok = (p != NULL && (uintptr_t)(p) % 16 == 0); mi_free(p);
    }
    result = ok;
  });
  
  // ---------------------------------------------------
  // Heaps
  // ---------------------------------------------------
  CHECK("heap_destroy", test_heap1());
  CHECK("heap_delete", test_heap2());

  //mi_stats_print(NULL);

  // ---------------------------------------------------
  // various
  // ---------------------------------------------------
  CHECK_BODY("realpath", {
    char* s = mi_realpath( ".", NULL );
    // printf("realpath: %s\n",s);
    mi_free(s);
  });

  CHECK("stl_allocator1", test_stl_allocator1());
  CHECK("stl_allocator2", test_stl_allocator2());

  // ---------------------------------------------------
  // Done
  // ---------------------------------------------------[]
  fprintf(stderr,"\n\n---------------------------------------------\n"
                 "succeeded: %i\n"
                 "failed   : %i\n\n", ok, failed);
  return failed;
}

// ---------------------------------------------------
// Larger test functions
// ---------------------------------------------------

bool test_heap1() {
  mi_heap_t* heap = mi_heap_new();
  int* p1 = mi_heap_malloc_tp(heap,int);
  int* p2 = mi_heap_malloc_tp(heap,int);
  *p1 = *p2 = 43;
  mi_heap_destroy(heap);
  return true;
}

bool test_heap2() {
  mi_heap_t* heap = mi_heap_new();
  int* p1 = mi_heap_malloc_tp(heap,int);
  int* p2 = mi_heap_malloc_tp(heap,int);
  mi_heap_delete(heap);
  *p1 = 42;
  mi_free(p1);
  mi_free(p2);
  return true;
}

bool test_stl_allocator1() {
#ifdef __cplusplus
  std::vector<int, mi_stl_allocator<int> > vec;
  vec.push_back(1);
  vec.pop_back();
  return vec.size() == 0;
#else
  return true;
#endif
}

struct some_struct  { int i; int j; double z; };

bool test_stl_allocator2() {
#ifdef __cplusplus
  std::vector<some_struct, mi_stl_allocator<some_struct> > vec;
  vec.push_back(some_struct());
  vec.pop_back();
  return vec.size() == 0;
#else
  return true;
#endif
}

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

bool check_zero_init(uint8_t* p, size_t size) {
  if(!p)
    return false;
  bool result = true;
  for (size_t i = 0; i < size; ++i) {
    result &= p[i] == 0;
  }
  return result;
}

#if MI_DEBUG >= 2
bool check_debug_fill_uninit(uint8_t* p, size_t size) {
  if(!p)
    return false;

  bool result = true;
  for (size_t i = 0; i < size; ++i) {
    result &= p[i] == MI_DEBUG_UNINIT;
  }
  return result;
}

bool check_debug_fill_freed(uint8_t* p, size_t size) {
  if(!p)
    return false;

  bool result = true;
  for (size_t i = 0; i < size; ++i) {
    result &= p[i] == MI_DEBUG_FREED;
  }
  return result;
}
#endif
