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
  mi_collect(true);
  return (mi_heap_visit_blocks(mi_heap_main(), true, find_live, &live) &&
          mi_heap_visit_abandoned_blocks(mi_heap_main(), true, find_live, &live) && !live);
}

static bool allocate_blocks(void) {
  for (size_t i = 0; i < block_count; i++) {
    sizes[i] = (mode == 3 ? 8 + (i * 197) % 16000 : 64);
    blocks[i] = (disposable == NULL ? mi_malloc(sizes[i]) : mi_heap_malloc(disposable, sizes[i]));
    if (blocks[i] == NULL) return false;
    memset(blocks[i], (int)(i % 251), sizes[i]);
  }
  return true;
}

static bool free_blocks(void) {
  mi_thread_init();
  mi_collect(true);
  mi_tld_t* const tld = _mi_theap_default()->tld;
  bool pending = false;
  for (size_t i = 0; i < block_count; i++) {
    const unsigned char* p = (const unsigned char*)blocks[i];
    if (p[0] != i % 251 || p[sizes[i]-1] != i % 251) return false;
    mi_free(blocks[i]);
    if (tld->free_batch_bytes >= MI_FREE_BATCH_BYTES) return false;
    if (tld->free_batch_bytes != 0) pending = true;
  }
  if (disposable != NULL) {
    if (tld->free_batch_bytes != 0) return false;
    mi_heap_destroy(disposable);
    disposable = NULL;
    mi_collect(true);
    return true;
  }
  if (!pending) return false;
  if (mode == 0) {
    mi_collect(true);
    return tld->free_batch_bytes == 0;
  }
  if (mode == 1) {
    mi_thread_done();
    if (_mi_thread_is_initialized()) return false;
    mi_thread_init();
    return _mi_theap_default()->tld->free_batch_bytes == 0;
  }
  return true;  // automatic thread cleanup must publish the remaining blocks
}

static bool test_remote_free(int test_mode) {
  mode = test_mode;
  block_count = (mode == 3 ? BLOCKS : 128);
  return allocate_blocks() && mi_run_on_thread(free_blocks) && all_freed();
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
  mi_option_set(mi_option_guarded_sample_rate, 0);
  CHECK("remote free explicit collect", test_remote_free(0));
  CHECK("remote free thread reinitialization", test_remote_free(1));
  CHECK("remote free automatic thread exit", test_remote_free(2));
  CHECK("remote free bounded cache", test_remote_free(3));
  CHECK("free without thread initialization", test_uninitialized_free());
  CHECK("remote free abandoned owner", test_abandoned_owner());
  CHECK("destroy heap after remote frees", test_destroyed_heap());
  CHECK("remote free concurrent owner exit", test_concurrent_owner_exit());
  return print_test_summary();
}
