#include <mimalloc.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_ALLOC (1024 * 512)
#define MAX_COUNT 32
#define ALLOCATION_POINTERS 1024

#define DEBUG 0
#define debug_print(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

typedef enum {
  CALLOC = 0,
  FREE,
  MALLOC,
  MALLOCN,
  REALLOC,
  REALLOCF,
  REALLOCN,
  ZALLOC,
  LAST_NOP,
} allocation_op_t;

typedef struct {
  uint32_t count;
  uint32_t size;
} arg_t;

typedef struct {
  // The index of the pointer to apply this operation too.
  uint32_t index;
  // The arguments to use in the operation.
  arg_t any_arg;
  // The type of operation to apply.
  allocation_op_t type;
} fuzzing_operation_t;

void debug_print_operation(fuzzing_operation_t *operation) {
  const char *names[] = {"CALLOC", "FREE", "MALLOC", "MALLOCN", "REALLOC", "REALLOCF", "REALLOCN", "ZALLOC", "LAST_NOP"};
  debug_print("index: %d, arg.count: %d, arg.size: %d, type: %s\n", operation->index, operation->any_arg.count, operation->any_arg.size, names[operation->type]);
}

#define FUZZING_OPERATION_DATA_SIZE sizeof(fuzzing_operation_t)

int init_fuzzing_operation(fuzzing_operation_t* out, const uint8_t* fuzzed_data, size_t len) {
  fuzzing_operation_t result = {0, {0,0},FREE};

  // Return a free operation if we don't have enough data to construct
  // a full operation.
  if(sizeof(fuzzing_operation_t) > len) {
    *out = result;
    return 0;
  }

  // Randomly populate operation using fuzzed data.
  memcpy(&result, fuzzed_data, sizeof(fuzzing_operation_t));

  // Fix up bounds for args and indicies. Randomly copying fuzzed data may result
  // in out of bounds indicies or the fuzzer trying to allocate way too much data.
  result.index %= ALLOCATION_POINTERS;
  result.any_arg.count %= MAX_COUNT;
  result.any_arg.size %= MAX_ALLOC;
  result.type = (uint8_t)result.type % (uint8_t)LAST_NOP;
  *out = result;

  return sizeof(fuzzing_operation_t);
}


int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  mi_heap_t * heap = mi_heap_new();
  void* allocation_ptrs[ALLOCATION_POINTERS] = {NULL};

  for(size_t i = 0; i < size; i = i + FUZZING_OPERATION_DATA_SIZE) {
    fuzzing_operation_t operation = {0, {0,0}, FREE};
    init_fuzzing_operation(&operation, data + i, size - i);

    debug_print_operation(&operation);

    switch(operation.type) {
      case CALLOC: 
        if(allocation_ptrs[operation.index] == NULL) {
          debug_print("%s\n","CALLOC");
          allocation_ptrs[operation.index] = mi_heap_calloc(heap, operation.any_arg.count, operation.any_arg.size);
        } else {
          debug_print("%s\n","CALLOC conditions not met");
        }
        break;
      case FREE:
        // Can be ptr or be NULL so we don't need to check first.
        mi_free(allocation_ptrs[operation.index]);
        allocation_ptrs[operation.index] = NULL;
        break;
      case MALLOC:
        if(allocation_ptrs[operation.index] == NULL){
          debug_print("%s\n","MALLOC");
          allocation_ptrs[operation.index] = mi_heap_malloc(heap, operation.any_arg.size);
        } else {
          debug_print("%s\n","MALLOC conditions not met");
        }
        break;
      case MALLOCN:
        if(allocation_ptrs[operation.index] == NULL){
          debug_print("%s\n","MALLOCN");
        allocation_ptrs[operation.index] = mi_heap_mallocn(heap, operation.any_arg.count, operation.any_arg.size);
        } else {
          debug_print("%s\n","MALLOCN conditions not met");
        }
        break;
      case REALLOC:
        if(allocation_ptrs[operation.index] != NULL){
          debug_print("%s\n","REALLOC");
          allocation_ptrs[operation.index] = mi_heap_realloc(heap, allocation_ptrs[operation.index], operation.any_arg.size);
        } else {
          debug_print("%s\n","REALLOC conditions not met");
        }
        break;
      case REALLOCN:
        if(allocation_ptrs[operation.index] != NULL){
          debug_print("%s\n","REALLOCN");
          allocation_ptrs[operation.index] = mi_heap_reallocn(heap, allocation_ptrs[operation.index], operation.any_arg.count, operation.any_arg.size);
        } else {
          debug_print("%s\n","REALLOCN conditions not met");
        }
        break;
      case REALLOCF:
        if(allocation_ptrs[operation.index] != NULL){
          debug_print("%s\n","REALLOCF");
          allocation_ptrs[operation.index] = mi_heap_reallocf(heap, allocation_ptrs[operation.index], operation.any_arg.size);
        } else {
          debug_print("%s\n","REALLOCF conditions not met");
        }        
        break;
      case ZALLOC:
        if(allocation_ptrs[operation.index] == NULL){
          debug_print("%s\n","ZALLOC");
         allocation_ptrs[operation.index] = mi_heap_zalloc(heap, operation.any_arg.size);
        } else {
          debug_print("%s\n","ZALLOC conditions not met");
        }
        break;
      case LAST_NOP:
        // No-op
        break;
      default:
        mi_heap_destroy(heap);
        exit(1);
        break;
    }
  }
  mi_heap_destroy(heap);
  return 0;
}
