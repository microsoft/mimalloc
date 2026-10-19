#include "../src/static.c"
#include "testhelper.h"
#include <string.h>

#define BLOCKS 2048
static void* blocks[BLOCKS];
static size_t sizes[BLOCKS];
static size_t block_count;
static mi_heap_t* disposable;
static int mode;
static _Atomic(size_t) owner_ready;
static _Atomic(size_t) owner_exit;
static bool trace_first_test = true;

#if MI_TEST_CRASH_INFO
#include <dbghelp.h>

static volatile size_t allocation_index;
static const char* volatile allocation_phase = "not started";
static void* volatile allocation_result;

static LONG WINAPI report_crash(EXCEPTION_POINTERS* exception) {
  static volatile LONG reporting;
  if (InterlockedCompareExchange(&reporting, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* record = exception->ExceptionRecord;
  fprintf(stderr, "\n  free-batch: exception 0x%08lx at %p; allocation %zu, phase %s, result %p\n",
          record->ExceptionCode, record->ExceptionAddress, allocation_index, allocation_phase, allocation_result);
  if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
    fprintf(stderr, "  free-batch: access type %zu (0=read, 1=write, 8=execute), address %p\n",
            (size_t)record->ExceptionInformation[0], (void*)record->ExceptionInformation[1]);
  }
  fflush(stderr);

  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
  if (length == 0 || length >= sizeof(path)) {
    fprintf(stderr, "  free-batch: cannot locate executable for symbols (error %lu)\n", GetLastError());
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  char* separator = strrchr(path, '\\');
  if (separator == NULL) {
    fprintf(stderr, "  free-batch: executable path has no directory\n");
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  *separator = 0;
  const HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH);
  if (SymInitialize(process, path, TRUE)) {
    struct {
      SYMBOL_INFO info;
      char name[256];
    } symbol = { 0 };
    symbol.info.SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol.info.MaxNameLen = sizeof(symbol.name);
    const DWORD64 address = (DWORD64)(uintptr_t)record->ExceptionAddress;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, address, &displacement, &symbol.info)) {
      fprintf(stderr, "  free-batch: %s + 0x%llx\n", symbol.info.Name, (unsigned long long)displacement);
    }
    else {
      fprintf(stderr, "  free-batch: symbol lookup failed (error %lu)\n", GetLastError());
    }
    IMAGEHLP_LINE64 line = { 0 };
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement = 0;
    if (SymGetLineFromAddr64(process, address, &line_displacement, &line)) {
      fprintf(stderr, "  free-batch: %s:%lu\n", line.FileName, line.LineNumber);
    }
    else {
      fprintf(stderr, "  free-batch: source lookup failed (error %lu)\n", GetLastError());
    }
    if (!SymCleanup(process)) {
      fprintf(stderr, "  free-batch: symbol cleanup failed (error %lu)\n", GetLastError());
    }
  }
  else {
    fprintf(stderr, "  free-batch: symbol initialization failed (error %lu)\n", GetLastError());
  }
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void trace_phase(const char* phase) {
  if (trace_first_test) {
    fprintf(stderr, "\n  free-batch: %s\n", phase);
    fflush(stderr);
  }
}

static bool find_live(const mi_heap_t* heap, const mi_heap_area_t* area,
                      void* block, size_t size, void* arg) {
  MI_UNUSED(heap); MI_UNUSED(area); MI_UNUSED(size);
  if (block != NULL) {
    for (size_t i = 0; i < block_count; i++) {
      if (block == blocks[i]) { *(bool*)arg = true; break; }
    }
  }
  return true;
}

static bool all_freed(void) {
  bool live = false;
  trace_phase("owner collect");
  mi_collect(true);
  trace_phase("visit heap blocks");
  if (!mi_heap_visit_blocks(mi_heap_main(), true, find_live, &live)) return false;
  trace_phase("visit abandoned blocks");
  if (!mi_heap_visit_abandoned_blocks(mi_heap_main(), true, find_live, &live)) return false;
  trace_phase("heap visits complete");
  return !live;
}

static bool allocate_blocks(void) {
  trace_phase("allocate blocks");
  for (size_t i = 0; i < block_count; i++) {
    sizes[i] = (mode == 3 ? 8 + (i * 197) % 16000 : 64);
    #if MI_TEST_CRASH_INFO
    if (trace_first_test) {
      allocation_index = i;
      allocation_result = NULL;
      allocation_phase = "allocate";
    }
    #endif
    blocks[i] = (disposable == NULL ? mi_malloc(sizes[i]) : mi_heap_malloc(disposable, sizes[i]));
    if (blocks[i] == NULL) return false;
    #if MI_TEST_CRASH_INFO
    if (trace_first_test) {
      allocation_result = blocks[i];
      allocation_phase = "memset";
    }
    #endif
    memset(blocks[i], (int)(i % 251), sizes[i]);
  }
  #if MI_TEST_CRASH_INFO
  if (trace_first_test) { allocation_phase = "allocations complete"; }
  #endif
  trace_phase("allocations complete");
  return true;
}

static bool free_blocks(void) {
  trace_phase("worker thread init");
  mi_thread_init();
  trace_phase("worker initial collect");
  mi_collect(true);
  trace_phase("worker heap lookup");
  mi_heap_t* const heap = (disposable == NULL ? mi_heap_main() : disposable);
  mi_theap_t* theap = _mi_heap_theap_peek(heap);
  if (disposable != NULL && theap != NULL) return false;
  bool pending = false;
  trace_phase("worker free loop");
  for (size_t i = 0; i < block_count; i++) {
    const unsigned char* p = (const unsigned char*)blocks[i];
    if (p[0] != i % 251 || p[sizes[i]-1] != i % 251) return false;
    mi_free(blocks[i]);
    if (i == 0) trace_phase("first free complete");
    theap = _mi_heap_theap_peek(heap);
    if (theap == NULL || theap->free_batch.batched_total >= MI_FREE_BATCH_BYTES) return false;
    if (theap->free_batch.batched_total != 0) pending = true;
  }
  if (disposable != NULL) {
    if (!pending) return false;
    mi_heap_destroy(disposable);
    disposable = NULL;
    mi_collect(true);
    return true;
  }
  if (!pending) return false;
  if (mode == 0) {
    trace_phase("worker batch collect");
    mi_collect(true);
    trace_phase("worker returning");
    return theap->free_batch.batched_total == 0;
  }
  if (mode == 1) {
    mi_thread_done();
    if (_mi_thread_is_initialized()) return false;
    mi_thread_init();
    return _mi_theap_main()->free_batch.batched_total == 0;
  }
  return true;  // automatic thread cleanup must publish the remaining blocks
}

static bool test_remote_free(int test_mode) {
  mode = test_mode;
  block_count = (mode == 3 ? BLOCKS : 128);
  if (!allocate_blocks()) return false;
  trace_phase("start and join worker");
  if (!mi_run_on_thread(free_blocks)) return false;
  trace_phase("worker joined");
  return all_freed();
}

static bool free_with_partial_publication(void) {
  mi_thread_init();
  mi_collect(true);
  mi_theap_t* const theap = _mi_theap_main();
  mi_free(blocks[0]);
  const size_t budget = theap->free_batch.batched_total;
  if (budget == 0) return false;
  bool published = false;
  for (size_t i = 0; i < MI_FREE_BATCH_COUNT; i++) {
    mi_free_batch_entry_t* const entry = &theap->free_batch.entries[i];
    if (entry->page != NULL) {
      mi_free_batch_publish(theap, entry);
      published = true;
      break;
    }
  }
  if (!published || theap->free_batch.batched_total != budget) return false;
  mi_free(blocks[1]);
  if (theap->free_batch.batched_total != 2 * budget) return false;
  mi_collect(true);
  if (theap->free_batch.batched_total != 0) return false;
  for (size_t i = 2; i < block_count; i++) mi_free(blocks[i]);
  mi_collect(true);
  return theap->free_batch.batched_total == 0;
}

static bool test_batch_budget(void) {
  mode = 0;
  block_count = 128;
  return allocate_blocks() && mi_run_on_thread(free_with_partial_publication) && all_freed();
}

static bool free_without_thread_state(void) {
  mi_thread_done();
  if (_mi_thread_is_initialized()) return false;
  for (size_t i = 0; i < block_count; i++) mi_free(blocks[i]);
  return !_mi_thread_is_initialized();
}

static bool test_uninitialized_free(void) {
  mode = 0;
  block_count = 128;
  return allocate_blocks() && mi_run_on_thread(free_without_thread_state) && all_freed();
}

static bool test_abandoned_owner(void) {
  mode = 0;
  block_count = 128;
  if (!mi_run_on_thread(allocate_blocks)) return false;
  for (size_t i = 0; i < block_count; i++) mi_free(blocks[i]);
  return all_freed();
}

static bool test_destroyed_heap(void) {
  mode = 0;
  block_count = 128;
  disposable = mi_heap_new();
  return disposable != NULL && allocate_blocks() && mi_run_on_thread(free_blocks);
}

static mi_heap_t* separate_heaps[2];
static void* separate_blocks[2][33];
static mi_theap_t* consumer_theap;

static bool free_separate_heaps(void) {
  mi_thread_init();
  for (size_t h = 0; h < 2; h++) {
    if (_mi_heap_theap_peek(separate_heaps[h]) != NULL) return false;
  }
  for (size_t i = 0; i < 32; i++) {
    for (size_t h = 0; h < 2; h++) mi_free(separate_blocks[h][i]);
  }
  mi_theap_t* first = _mi_heap_theap_peek(separate_heaps[0]);
  mi_theap_t* second = _mi_heap_theap_peek(separate_heaps[1]);
  if (first == NULL || second == NULL || first == second ||
      first->free_batch.batched_total == 0 || second->free_batch.batched_total == 0) return false;

  mi_heap_collect(separate_heaps[0], true);
  if (first->free_batch.batched_total != 0 || second->free_batch.batched_total == 0) return false;
  mi_theap_delete(second);
  if (_mi_heap_theap_peek(separate_heaps[1]) != NULL) return false;
  for (size_t h = 0; h < 2; h++) mi_free(separate_blocks[h][32]);
  second = _mi_heap_theap_peek(separate_heaps[1]);
  if (second == NULL || first->free_batch.batched_total == 0 ||
      second->free_batch.batched_total == 0) return false;
  mi_theap_t* const previous = mi_theap_set_default(first);
  mi_collect(true);
  const bool collected_default = (first->free_batch.batched_total == 0 &&
                                  second->free_batch.batched_total != 0);
  mi_theap_set_default(previous);
  mi_heap_collect(separate_heaps[1], true);
  return collected_default && second->free_batch.batched_total == 0;
}

static bool test_separate_heaps(void) {
  for (size_t h = 0; h < 2; h++) {
    separate_heaps[h] = mi_heap_new();
    if (separate_heaps[h] == NULL) return false;
    for (size_t i = 0; i < 33; i++) {
      separate_blocks[h][i] = mi_heap_malloc(separate_heaps[h], 64);
      if (separate_blocks[h][i] == NULL) return false;
    }
  }
  const bool result = mi_run_on_thread(free_separate_heaps);
  for (size_t h = 0; h < 2; h++) mi_heap_destroy(separate_heaps[h]);
  return result;
}

static bool pending_heap_consumer(void) {
  mi_thread_init();
  for (size_t i = 0; i + 1 < block_count; i++) mi_free(blocks[i]);
  mi_theap_t* const theap = _mi_heap_theap_peek(disposable);
  consumer_theap = theap;
  const bool pending = (theap != NULL && theap->free_batch.batched_total != 0);
  mi_atomic_store_release(&owner_ready, pending ? 1 : 2);
  while (mi_atomic_load_acquire(&owner_exit) == 0) _mi_prim_thread_yield();
  mi_collect(true);
  return pending;
}

typedef enum { HEAP_DELETE, HEAP_DESTROY, THEAP_ABANDON } heap_end_t;

static bool test_heap_end_with_pending(heap_end_t action) {
  mode = 0;
  block_count = 128;
  disposable = mi_heap_new();
  if (disposable == NULL || !allocate_blocks()) return false;
  mi_atomic_store_relaxed(&owner_ready, 0);
  mi_atomic_store_relaxed(&owner_exit, 0);
  mi_thread_fun_args_t arg = { pending_heap_consumer, false };
  #ifdef _WIN32
  HANDLE thread = CreateThread(NULL, 0, mi_win_thread_entry, &arg, 0, NULL);
  if (thread == NULL) return false;
  #else
  pthread_t thread;
  if (pthread_create(&thread, NULL, mi_pthread_entry, &arg) != 0) return false;
  #endif
  while (mi_atomic_load_acquire(&owner_ready) == 0) _mi_prim_thread_yield();
  bool collected = true;
  if (action == HEAP_DESTROY) mi_heap_destroy(disposable);
  else if (action == HEAP_DELETE) mi_heap_delete(disposable);
  else {
    mi_theap_quiescent_abandon(consumer_theap);
    collected = (consumer_theap != NULL && consumer_theap->free_batch.batched_total == 0);
  }
  mi_atomic_store_release(&owner_exit, 1);
  #ifdef _WIN32
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
  #else
  if (pthread_join(thread, NULL) != 0) return false;
  #endif
  if (action == THEAP_ABANDON) mi_heap_destroy(disposable);
  disposable = NULL;
  if (!arg.result || !collected) return false;
  if (action == HEAP_DELETE) {
    const size_t last = block_count - 1;
    if (mi_heap_of(blocks[last]) != mi_heap_main() ||
        ((unsigned char*)blocks[last])[0] != last % 251) return false;
    mi_free(blocks[last]);
    return all_freed();
  }
  return true;
}

static bool free_while_creation_disabled(void) {
  mi_thread_init();
  mi_tld_t* const tld = _mi_theap_main()->tld;
  tld->free_batch_disabled = true;
  for (size_t i = 0; i < block_count; i++) mi_free(blocks[i]);
  const bool not_created = (_mi_heap_theap_peek(disposable) == NULL);
  tld->free_batch_disabled = false;
  return not_created;
}

static bool test_creation_disabled(void) {
  mode = 0;
  block_count = 128;
  disposable = mi_heap_new();
  if (disposable == NULL || !allocate_blocks()) return false;
  const bool result = mi_run_on_thread(free_while_creation_disabled);
  mi_heap_destroy(disposable);
  disposable = NULL;
  return result;
}

static void* metadata_block;
static mi_memid_t metadata_memid;

static bool free_detached_metadata(void) {
  mi_thread_init();
  mi_collect(true);
  _mi_meta_free(_mi_subproc_main(), metadata_block, metadata_memid);
  return _mi_theap_main()->free_batch.batched_total == 0;
}

static bool test_detached_metadata(void) {
  metadata_block = _mi_meta_zalloc(_mi_subproc_main(), 64, &metadata_memid);
  if (metadata_block == NULL || mi_page_thread_id(mi_ptr_page(metadata_block)) != MI_THREADID_DETACHED) return false;
  return mi_run_on_thread(free_detached_metadata);
}

static bool departing_owner(void) {
  const bool allocated = allocate_blocks();
  mi_atomic_store_release(&owner_ready, allocated ? 1 : 2);
  while (allocated && mi_atomic_load_acquire(&owner_exit) == 0) _mi_prim_thread_yield();
  return allocated;
}

static bool test_concurrent_owner_exit(void) {
  mode = 3;
  block_count = BLOCKS;
  for (size_t round = 0; round < 8; round++) {
    mi_atomic_store_relaxed(&owner_ready, 0);
    mi_atomic_store_relaxed(&owner_exit, 0);
    mi_thread_fun_args_t arg = { departing_owner, false };
    #ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, mi_win_thread_entry, &arg, 0, NULL);
    if (thread == NULL) return false;
    #else
    pthread_t thread;
    if (pthread_create(&thread, NULL, mi_pthread_entry, &arg) != 0) return false;
    #endif
    size_t ready;
    while ((ready = mi_atomic_load_acquire(&owner_ready)) == 0) _mi_prim_thread_yield();
    mi_atomic_store_release(&owner_exit, 1);
    if (ready == 1) {
      for (size_t i = 0; i < block_count; i++) mi_free(blocks[i]);
    }
    #ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    #else
    if (pthread_join(thread, NULL) != 0) return false;
    #endif
    if (!arg.result || !all_freed()) return false;
  }
  return true;
}

int main(void) {
  #if MI_TEST_CRASH_INFO
  const LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = SetUnhandledExceptionFilter(report_crash);
  #endif
  mi_option_set(mi_option_guarded_sample_rate, 0);
  CHECK("remote free explicit collect", test_remote_free(0));
  #if MI_TEST_CRASH_INFO
  SetUnhandledExceptionFilter(previous_filter);
  #endif
  trace_first_test = false;
  CHECK("remote free thread reinitialization", test_remote_free(1));
  CHECK("remote free automatic thread exit", test_remote_free(2));
  CHECK("remote free bounded cache", test_remote_free(3));
  CHECK("batch budget survives partial publication", test_batch_budget());
  CHECK("free without thread initialization", test_uninitialized_free());
  CHECK("remote free abandoned owner", test_abandoned_owner());
  CHECK("destroy heap after remote frees", test_destroyed_heap());
  CHECK("separate heap batches and theap deletion", test_separate_heaps());
  CHECK("delete heap with a live consumer batch", test_heap_end_with_pending(HEAP_DELETE));
  CHECK("destroy heap with a live consumer batch", test_heap_end_with_pending(HEAP_DESTROY));
  CHECK("abandon a quiescent foreign theap batch", test_heap_end_with_pending(THEAP_ABANDON));
  CHECK("free while theap creation is disabled", test_creation_disabled());
  CHECK("detached metadata is not batched", test_detached_metadata());
  CHECK("remote free concurrent owner exit", test_concurrent_owner_exit());
  return print_test_summary();
}
