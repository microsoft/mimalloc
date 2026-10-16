#include "../src/arena/arena.h"
#include "mimalloc/alloc/page.h"
#include "mimalloc/heap/theap.h"
#include "mimalloc/prim.h"
#include "testhelper.h"

static mi_subproc_t test_subproc;
static mi_heap_t test_heap;
static mi_arena_t test_arenas[6];

typedef struct arena_visit_s {
  size_t indexes[6];
  size_t count;
  size_t stop_after;
} arena_visit_t;

typedef struct arena_visit_info_s {
  arena_visit_t* visit;
} arena_visit_info_t;

static void arena_setup(size_t count) {
  test_heap.subproc = &test_subproc;
  test_heap.heap_seq = 0;
  mi_atomic_store_relaxed(&test_subproc.heap_count, 1);
  mi_atomic_store_relaxed(&test_subproc.arena_count, count);
  for (size_t i = 0; i < 6; i++) {
    test_arenas[i].arena_idx = i;
    test_arenas[i].parent = NULL;
    test_arenas[i].is_exclusive = false;
    test_arenas[i].memid.is_pinned = false;
    test_arenas[i].numa_node = 0;
    mi_atomic_store_ptr_release(mi_arena_t, &test_subproc.arenas[i], &test_arenas[i]);
  }
}

static bool arena_record(mi_arena_t* arena, const void* arg, void** result) {
  MI_UNUSED(result);
  const arena_visit_info_t* info = (const arena_visit_info_t*)arg;
  arena_visit_t* visit = info->visit;
  if (visit->count >= 6) return false;
  visit->indexes[visit->count++] = arena->arena_idx;
  return (visit->stop_after == 0 || visit->count < visit->stop_after);
}

static bool arena_matches(const arena_visit_t* visit, const size_t* expected, size_t count) {
  if (visit->count != count) return false;
  for (size_t i = 0; i < count; i++) {
    if (visit->indexes[i] != expected[i]) return false;
  }
  return true;
}

static bool test_arena_order(void) {
  for (size_t count = 0; count <= 6; count++) {
    arena_setup(count);
    for (size_t tseq = 0; tseq < 8; tseq++) {
      arena_visit_t visit = { { 0 }, 0, 0 };
      const arena_visit_info_t info = { &visit };
      if (!mi_forall_arenas(&test_heap, NULL, tseq, &arena_record, &info, NULL)) return false;
      if (visit.count != count) return false;
      for (size_t i = 0; i < count; i++) {
        const size_t expected = (i + 1 == count ? i : (tseq + i) % (count - 1));
        if (visit.indexes[i] != expected) return false;
      }
    }
  }
  arena_setup(6);
  test_heap.heap_seq = 1;
  mi_atomic_store_relaxed(&test_subproc.heap_count, 2);
  arena_visit_t visit = { { 0 }, 0, 0 };
  const arena_visit_info_t info = { &visit };
  const size_t expected[] = { 3, 4, 0, 1, 2, 5 };
  if (!mi_forall_arenas(&test_heap, NULL, 1, &arena_record, &info, NULL)) return false;
  return arena_matches(&visit, expected, 6);
}

static bool test_arena_filter(void) {
  arena_setup(6);
  mi_atomic_store_ptr_release(mi_arena_t, &test_subproc.arenas[1], NULL);
  test_arenas[2].memid.is_pinned = true;
  test_arenas[2].numa_node = 1;
  test_arenas[3].is_exclusive = true;
  test_arenas[4].numa_node = -1;
  test_arenas[5].numa_node = 1;

  arena_visit_t visit = { { 0 }, 0, 0 };
  const arena_visit_info_t info = { &visit };
  const size_t all[] = { 0, 2, 3, 4, 5 };
  if (!mi_forall_arenas(&test_heap, NULL, 0, &arena_record, &info, NULL) || !arena_matches(&visit, all, 5)) return false;

  visit.count = 0;
  const size_t local[] = { 0, 4 };
  if (!mi_forall_suitable_arenas(&test_heap, NULL, 0, true, 0, false, &arena_record, &info, NULL) ||
      !arena_matches(&visit, local, 2)) return false;

  visit.count = 0;
  const size_t remote[] = { 2, 5 };
  if (!mi_forall_suitable_arenas(&test_heap, NULL, 0, false, 0, true, &arena_record, &info, NULL) ||
      !arena_matches(&visit, remote, 2)) return false;

  visit.count = 0;
  const size_t any[] = { 0, 2, 4, 5 };
  if (!mi_forall_suitable_arenas(&test_heap, NULL, 0, true, -1, true, &arena_record, &info, NULL) ||
      !arena_matches(&visit, any, 4)) return false;

  visit.count = 0;
  if (!mi_forall_suitable_arenas(&test_heap, NULL, 0, false, -1, true, &arena_record, &info, NULL) || visit.count != 0) return false;

  test_arenas[4].parent = &test_arenas[3];
  test_arenas[5].parent = &test_arenas[3];
  test_arenas[4].is_exclusive = true;
  test_arenas[5].is_exclusive = true;
  const size_t family[] = { 3, 4, 5 };
  if (!mi_forall_suitable_arenas(&test_heap, &test_arenas[3], 0, false, 99, false, &arena_record, &info, NULL) ||
      !arena_matches(&visit, family, 3)) return false;

  visit.count = 0;
  test_arenas[3].memid.is_pinned = true;
  const size_t children[] = { 4, 5 };
  if (!mi_forall_suitable_arenas(&test_heap, &test_arenas[3], 0, true, 99, false, &arena_record, &info, NULL) ||
      !arena_matches(&visit, children, 2)) return false;

  visit.count = 0;
  test_arenas[5].parent = NULL;
  const size_t last[] = { 5 };
  if (!mi_forall_arenas(&test_heap, &test_arenas[5], 0, &arena_record, &info, NULL)) return false;
  return arena_matches(&visit, last, 1);
}

static bool test_arena_stop(void) {
  arena_setup(6);
  for (size_t stop = 1; stop <= 6; stop++) {
    arena_visit_t visit = { { 0 }, 0, stop };
    const arena_visit_info_t info = { &visit };
    if (mi_forall_arenas(&test_heap, NULL, 0, &arena_record, &info, NULL) || visit.count != stop) return false;
  }
  test_arenas[0].is_exclusive = true;
  test_arenas[1].memid.is_pinned = true;
  arena_visit_t visit = { { 0 }, 0, 2 };
  const arena_visit_info_t info = { &visit };
  const size_t expected[] = { 2, 3 };
  if (mi_forall_suitable_arenas(&test_heap, NULL, 0, true, -1, false, &arena_record, &info, NULL)) return false;
  return arena_matches(&visit, expected, 2);
}

static bool arena_publish(mi_arena_t* arena, const void* arg, void** result) {
  mi_atomic_store_relaxed(&test_subproc.arena_count, 6);
  return arena_record(arena, arg, result);
}

static bool test_arena_snapshot(void) {
  arena_setup(3);
  arena_visit_t visit = { { 0 }, 0, 0 };
  const arena_visit_info_t info = { &visit };
  const size_t expected[] = { 0, 1, 2 };
  if (!mi_forall_arenas(&test_heap, NULL, 0, &arena_publish, &info, NULL)) return false;
  return arena_matches(&visit, expected, 3);
}

static bool arena_find(mi_arena_t* arena, const void* arg, void** result) {
  const size_t* index = (const size_t*)arg;
  if (arena->arena_idx != *index) return true; // continue
  if (result != NULL) *result = arena;
  return false; // break
}

static bool test_arena_result(void) {
  arena_setup(6);
  const size_t index = 3;
  void* found = NULL;
  if (mi_forall_arenas(&test_heap, NULL, 0, &arena_find, &index, &found) || found != &test_arenas[3]) return false;
  found = NULL;
  if (mi_forall_suitable_arenas(&test_heap, NULL, 0, true, -1, false, &arena_find, &index, &found) ||
      found != &test_arenas[3]) return false;
  if (mi_forall_suitable_arenas(&test_heap, NULL, 0, true, -1, false, &arena_find, &index, NULL)) return false;

  test_arenas[3].is_exclusive = true;
  if (!mi_forall_suitable_arenas(&test_heap, NULL, 0, true, -1, false, &arena_find, &index, &found) || found != NULL) return false;
  const size_t absent = 6;
  found = &test_arenas[0];
  if (!mi_forall_arenas(&test_heap, NULL, 0, &arena_find, &absent, &found) || found != NULL) return false;
  arena_setup(0);
  found = &test_arenas[0];
  if (!mi_forall_arenas(&test_heap, NULL, 0, &arena_find, &index, &found) || found != NULL) return false;
  found = &test_arenas[0];
  return (mi_forall_suitable_arenas(&test_heap, NULL, 0, true, -1, false, &arena_find, &index, &found) && found == NULL);
}

static mi_heap_t* visit_heap;

static bool allocate_pages(void) {
  if (mi_heap_malloc(visit_heap, 64) == NULL ||
      mi_heap_malloc(visit_heap, 256) == NULL ||
      mi_heap_malloc(visit_heap, 4096) == NULL) return false;
  const long disallow = mi_option_get(mi_option_disallow_arena_alloc);
  mi_option_enable(mi_option_disallow_arena_alloc);
  void* p = mi_heap_malloc(visit_heap, 2 * MI_MiB);
  mi_option_set(mi_option_disallow_arena_alloc, disallow);
  return (p != NULL);
}

typedef struct block_visit_s {
  size_t areas;
  size_t blocks;
  bool stop;
  bool stop_on_block;
} block_visit_t;

static bool block_record(const mi_heap_t* heap, const mi_heap_area_t* area, void* block, size_t block_size, void* arg) {
  MI_UNUSED(heap);
  MI_UNUSED(area);
  MI_UNUSED(block_size);
  block_visit_t* visit = (block_visit_t*)arg;
  if (block == NULL) { visit->areas++; }
  else { visit->blocks++; }
  return (!visit->stop || (visit->stop_on_block && block == NULL));
}

static bool test_heap_visit_stop(void) {
  visit_heap = mi_heap_new();
  if (visit_heap == NULL) return false;
  bool result = mi_run_on_thread(&allocate_pages);
  for (size_t abandoned = 0; result && abandoned < 2; abandoned++) {
    for (size_t mode = 0; result && mode < 3; mode++) {
      block_visit_t visit = { 0, 0, mode != 0, mode == 2 };
      const bool completed = (abandoned == 0
          ? mi_heap_visit_blocks(visit_heap, true, &block_record, &visit)
          : mi_heap_visit_abandoned_blocks(visit_heap, true, &block_record, &visit));
      if (mode == 0) {
        result = (completed && visit.areas == 4 && visit.blocks == 4);
      }
      else {
        result = (!completed && visit.areas == 1 && visit.blocks == (mode == 2 ? 1 : 0));
      }
    }
  }
  mi_heap_destroy(visit_heap);
  visit_heap = NULL;
  return result;
}

#define VISIT_OWNED_BLOCKS 256
static void* owned_blocks[VISIT_OWNED_BLOCKS + 1];

typedef struct owned_visit_s {
  size_t used;
  size_t blocks;
  size_t os_areas;
  size_t arena_areas;
  bool seen[VISIT_OWNED_BLOCKS + 1];
} owned_visit_t;

static bool owned_record(const mi_heap_t* heap, const mi_heap_area_t* area, void* block, size_t block_size, void* arg) {
  MI_UNUSED(block_size);
  owned_visit_t* visit = (owned_visit_t*)arg;
  if (heap != visit_heap) return false;
  if (block == NULL) {
    visit->used += area->used;
    const mi_page_t* page = (const mi_page_t*)area->reserved1;
    if (page->memid.memkind == MI_MEM_ARENA) { visit->arena_areas++; }
    else { visit->os_areas++; }
    return true;
  }
  for (size_t i = 0; i <= VISIT_OWNED_BLOCKS; i++) {
    if (block == owned_blocks[i]) {
      if (visit->seen[i]) return false;
      visit->seen[i] = true;
      visit->blocks++;
      return true;
    }
  }
  return false;
}

static bool free_owned_blocks(void) {
  for (size_t i = 0; i < VISIT_OWNED_BLOCKS / 4; i++) {
    mi_free(owned_blocks[i]);
    owned_blocks[i] = NULL;
  }
  return true;
}

static bool test_heap_visit_owned_os(bool mixed) {
  const long disallow = mi_option_get(mi_option_disallow_arena_alloc);
  const long retain = mi_option_get(mi_option_page_full_retain);
  const long guarded = mi_option_get(mi_option_guarded_sample_rate);
  mi_option_set(mi_option_page_full_retain, -1);
  mi_option_set(mi_option_guarded_sample_rate, 0);
  mi_option_disable(mi_option_disallow_arena_alloc);
  visit_heap = mi_heap_new();
  bool result = (visit_heap != NULL);
  owned_blocks[VISIT_OWNED_BLOCKS] = (mixed && result ? mi_heap_malloc(visit_heap, 64) : NULL);
  if (mixed && owned_blocks[VISIT_OWNED_BLOCKS] == NULL) result = false;
  mi_option_enable(mi_option_disallow_arena_alloc);
  for (size_t i = 0; result && i < VISIT_OWNED_BLOCKS; i++) {
    owned_blocks[i] = mi_heap_malloc(visit_heap, 4096);
    result = (owned_blocks[i] != NULL);
  }
  if (result) {
    result = (mi_heap_theap(visit_heap)->pages[MI_BIN_FULL].first != NULL);
  }
  for (size_t i = 0; result && i < VISIT_OWNED_BLOCKS; i++) {
    result = (mi_thread_id_of(owned_blocks[i]) == mi_heap_theap(visit_heap)->tld->thread_id);
  }
  if (result) result = mi_run_on_thread(&free_owned_blocks);
  const size_t expected = VISIT_OWNED_BLOCKS * 3 / 4 + (mixed ? 1 : 0);
  for (size_t mode = 0; result && mode < 2; mode++) {
    owned_visit_t visit = { 0, 0, 0, 0, { false } };
    result = (mi_heap_visit_blocks(visit_heap, mode != 0, &owned_record, &visit) &&
              visit.used == expected && visit.blocks == (mode != 0 ? expected : 0) &&
              visit.os_areas > 0 && visit.os_areas == mi_heap_theap(visit_heap)->page_count_os &&
              visit.arena_areas == (mixed ? 1 : 0));
  }
  if (result) {
    block_visit_t visit = { 0, 0, false, false };
    result = (mi_heap_visit_abandoned_blocks(visit_heap, true, &block_record, &visit) &&
              visit.areas == 0 && visit.blocks == 0);
  }
  for (size_t mode = 1; result && mode <= 2; mode++) {
    block_visit_t visit = { 0, 0, true, mode == 2 };
    result = (!mi_heap_visit_blocks(visit_heap, true, &block_record, &visit) &&
              visit.areas == 1 && visit.blocks == (mode == 2 ? 1 : 0));
  }
  mi_option_set(mi_option_disallow_arena_alloc, disallow);
  if (result) result = mi_run_on_thread(&allocate_pages);
  if (result) {
    block_visit_t all = { 0, 0, false, false };
    block_visit_t abandoned = { 0, 0, false, false };
    result = (mi_heap_visit_blocks(visit_heap, true, &block_record, &all) && all.blocks == expected + 4 &&
              mi_heap_visit_abandoned_blocks(visit_heap, true, &block_record, &abandoned) && abandoned.blocks == 4);
  }
  mi_heap_destroy(visit_heap);
  visit_heap = NULL;
  mi_option_set(mi_option_page_full_retain, retain);
  mi_option_set(mi_option_guarded_sample_rate, guarded);
  return result;
}

static bool test_heap_page_count_os(void) {
  const long disallow = mi_option_get(mi_option_disallow_arena_alloc);
  const long guarded = mi_option_get(mi_option_guarded_sample_rate);
  const long reclaim = mi_option_get(mi_option_page_reclaim_on_free);
  mi_option_enable(mi_option_disallow_arena_alloc);
  mi_option_set(mi_option_guarded_sample_rate, 0);
  mi_option_set(mi_option_page_reclaim_on_free, 0);
  mi_heap_t* heap = mi_heap_new();
  mi_theap_t* theap = (heap == NULL ? NULL : mi_heap_theap(heap));
  bool result = (theap != NULL && theap->page_count_os == 0);
  if (result) {
    void* p = mi_heap_malloc(heap, 64);
    void* q = mi_heap_malloc(heap, 64);
    void* r = mi_heap_malloc(heap, 256);
    result = (p != NULL && q != NULL && r != NULL);
    if (result) {
      result = (theap->page_count_os == 2);
      mi_free(r);
      mi_heap_collect(heap, true);
      result = (theap->page_count_os == 1 && result);
      _mi_theap_collect_abandon(theap);
      result = (theap->page_count_os == 0 && result);
      mi_free(p); // reclaim the page containing q into the originating theap
      result = (theap->page_count_os == 1 && result);
      block_visit_t visit = { 0, 0, false, false };
      result = (mi_heap_visit_blocks(heap, true, &block_record, &visit) && visit.blocks == 1 && result);
      mi_free(q);
      mi_heap_collect(heap, true);
      result = (theap->page_count_os == 0 && result);
    }
  }
  mi_heap_destroy(heap);
  mi_option_set(mi_option_disallow_arena_alloc, disallow);
  mi_option_set(mi_option_guarded_sample_rate, guarded);
  mi_option_set(mi_option_page_reclaim_on_free, reclaim);
  return result;
}

static _Atomic(size_t) visit_exit_state;

static bool visit_exit_owner(void) {
  const bool result = allocate_pages();
  mi_atomic_store_release(&visit_exit_state, 1);
  while (mi_atomic_load_acquire(&visit_exit_state) < 2) { _mi_prim_thread_yield(); }
  mi_atomic_store_release(&visit_exit_state, 3);
  return result;
}

static bool exit_record(const mi_heap_t* heap, const mi_heap_area_t* area, void* block, size_t block_size, void* arg) {
  if (mi_atomic_load_acquire(&visit_exit_state) == 1) {
    mi_atomic_store_release(&visit_exit_state, 2);
    while (mi_atomic_load_acquire(&visit_exit_state) < 3) { _mi_prim_thread_yield(); }
    for (size_t i = 0; i < 64; i++) { _mi_prim_thread_yield(); }
  }
  return block_record(heap, area, block, block_size, arg);
}

static bool test_heap_visit_thread_exit(void) {
  const long disallow = mi_option_get(mi_option_disallow_arena_alloc);
  const long guarded = mi_option_get(mi_option_guarded_sample_rate);
  mi_option_set(mi_option_guarded_sample_rate, 0);
  bool result = true;
  for (size_t i = 0; result && i < 32; i++) {
    mi_option_set(mi_option_disallow_arena_alloc, (long)(i % 2));
    visit_heap = mi_heap_new();
    if (visit_heap == NULL) { result = false; break; }
    if (mi_heap_malloc(visit_heap, 128) == NULL) {
      mi_heap_destroy(visit_heap);
      visit_heap = NULL;
      result = false;
      break;
    }
    mi_atomic_store_release(&visit_exit_state, 0);
    mi_thread_fun_args_t args = { &visit_exit_owner, false };
    #ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, &mi_win_thread_entry, &args, 0, NULL);
    const bool started = (thread != NULL);
    #else
    pthread_t thread;
    const bool started = (pthread_create(&thread, NULL, &mi_pthread_entry, &args) == 0);
    #endif
    if (started) {
      while (mi_atomic_load_acquire(&visit_exit_state) == 0) { _mi_prim_thread_yield(); }
      block_visit_t visit = { 0, 0, false, false };
      result = (mi_heap_visit_blocks(visit_heap, true, &exit_record, &visit) && visit.blocks == 5);
      if (mi_atomic_load_acquire(&visit_exit_state) == 1) mi_atomic_store_release(&visit_exit_state, 2);
      #ifdef _WIN32
      WaitForSingleObject(thread, INFINITE);
      CloseHandle(thread);
      #else
      pthread_join(thread, NULL);
      #endif
      visit.blocks = visit.areas = 0;
      result = (result && args.result &&
                mi_heap_visit_abandoned_blocks(visit_heap, true, &block_record, &visit) && visit.blocks == 4);
    }
    else {
      result = false;
    }
    mi_heap_destroy(visit_heap);
    visit_heap = NULL;
  }
  mi_option_set(mi_option_disallow_arena_alloc, disallow);
  mi_option_set(mi_option_guarded_sample_rate, guarded);
  return result;
}

static bool test_theap_collect_detached(void) {
  mi_theap_t* const theap = mi_theap_get_default();
  mi_subproc_t* const subproc = theap->tld->subproc;
  int* p = NULL;
  bool result = false;
  mi_lock(&subproc->theap_meta_lock) {
    p = (int*)mi_theap_malloc(subproc->theap_meta, sizeof(int));
    if (p != NULL) {
      *p = 42;
      mi_theap_collect(subproc->theap_meta, false);
      mi_theap_collect(subproc->theap_meta, true);
      result = (*p == 42 && mi_thread_id_of(p) == 0);
    }
  }
  mi_free(p);
  return result;
}

static mi_heap_t* thread_id_heap;
static void* thread_id_blocks[3];
static size_t thread_id_owner;

static bool read_thread_ids(void) {
  for (size_t i = 0; i < 3; i++) {
    const uint8_t* p = (const uint8_t*)thread_id_blocks[i];
    if (mi_thread_id_of(p) != thread_id_owner ||
        mi_thread_id_of(p + 1) != thread_id_owner ||
        mi_thread_id_of(p + 2 * sizeof(size_t)) != thread_id_owner) return false;
  }
  return (mi_thread_id_of((const uint8_t*)thread_id_blocks[2] + MI_SMALL_SIZE_MAX - 1) == thread_id_owner);
}

static bool allocate_thread_id_blocks(void) {
  thread_id_blocks[0] = mi_heap_malloc(thread_id_heap, 256);
  thread_id_blocks[1] = mi_heap_malloc(thread_id_heap, 256);
  thread_id_blocks[2] = mi_heap_malloc_aligned_at(thread_id_heap, 2 * MI_SMALL_SIZE_MAX, 64, sizeof(size_t));
  if (thread_id_blocks[0] == NULL || thread_id_blocks[1] == NULL || thread_id_blocks[2] == NULL) return false;
  thread_id_owner = mi_heap_theap(thread_id_heap)->tld->thread_id;
  return (read_thread_ids() && mi_run_on_thread(&read_thread_ids));
}

static bool test_thread_id_of(bool os_pages, long reclaim_mode) {
  const long disallow = mi_option_get(mi_option_disallow_arena_alloc);
  const long guarded = mi_option_get(mi_option_guarded_sample_rate);
  const long reclaim = mi_option_get(mi_option_page_reclaim_on_free);
  mi_option_set(mi_option_disallow_arena_alloc, (os_pages ? 1 : 0));
  mi_option_set(mi_option_guarded_sample_rate, 0);
  mi_option_set(mi_option_page_reclaim_on_free, reclaim_mode);
  thread_id_heap = mi_heap_new();
  mi_option_set(mi_option_page_reclaim_on_free, (reclaim_mode == 1 ? -1 : 1));
  mi_theap_t* theap = (thread_id_heap == NULL ? NULL : mi_heap_theap(thread_id_heap));
  bool result = (theap != NULL && mi_thread_id_of(NULL) == 0 &&
                 theap->allow_page_reclaim == (reclaim_mode >= 0));
  for (size_t i = 0; i < 3; i++) { thread_id_blocks[i] = NULL; }
  if (result) result = mi_run_on_thread(&allocate_thread_id_blocks);
  if (result) {
    const mi_page_t* page = mi_ptr_page(thread_id_blocks[0]);
    result = (mi_page_thread_id(page) == (os_pages ? MI_THREADID_ABANDONED : MI_THREADID_ABANDONED_MAPPED));
    for (size_t i = 0; i < 3; i++) {
      result = (mi_thread_id_of(thread_id_blocks[i]) == 0 &&
                mi_thread_id_of((const uint8_t*)thread_id_blocks[i] + 2 * sizeof(size_t)) == 0 && result);
    }
    mi_free(thread_id_blocks[0]); // only the heap's creation-time mode can permit cross-thread reclaim
    thread_id_blocks[0] = NULL;
    const size_t expected_owner = (reclaim_mode == 1 ? theap->tld->thread_id : 0);
    result = (mi_thread_id_of(thread_id_blocks[1]) == expected_owner &&
              mi_thread_id_of(thread_id_blocks[2]) == 0 && result);
  }
  for (size_t i = 0; i < 3; i++) {
    mi_free(thread_id_blocks[i]);
    thread_id_blocks[i] = NULL;
  }
  mi_heap_destroy(thread_id_heap);
  thread_id_heap = NULL;
  mi_option_set(mi_option_disallow_arena_alloc, disallow);
  mi_option_set(mi_option_guarded_sample_rate, guarded);
  mi_option_set(mi_option_page_reclaim_on_free, reclaim);
  return result;
}

static bool test_theap_delete_owner(void) {
  mi_theap_t* const main = mi_theap_get_default();
  mi_tld_t* const tld = main->tld;
  bool result = true;
  for (size_t cached = 0; result && cached < 2; cached++) {
    mi_heap_t* heap = mi_heap_new();
    if (heap == NULL) return false;
    mi_theap_t* theap = mi_heap_theap(heap);
    int* p = (int*)mi_theap_malloc(theap, sizeof(int));
    int* q = (int*)mi_theap_malloc(main, sizeof(int));
    result = (p != NULL && q != NULL);
    if (result) {
      *p = 42;
      *q = 43;
      mi_free(mi_theap_malloc(theap, 4096)); // include an empty retired page
      mi_theap_set_default(theap);
      if (cached == 0) {
        // Reach the statistics update threshold during quiescent abandonment.
        for (size_t i = 0; i < 0x8000; i++) {
          mi_free(mi_theap_malloc(theap, sizeof(int)));
        }
        mi_theap_quiescent_abandon(theap);
        result = (theap->page_count == 0 && theap->tld == tld &&
                  mi_theap_get_default() == theap && mi_heap_theap(heap) == theap &&
                  *p == 42 && mi_thread_id_of(p) == 0 && result);
        mi_free(mi_malloc(4096));
      }
      mi_theap_delete(main);
      result = (mi_theap_get_default() == theap && main->tld == tld &&
                *q == 43 && mi_thread_id_of(q) == 0 && result);
      (void)mi_heap_theap(cached ? heap : mi_heap_main());
      mi_theap_delete(theap);
      result = (mi_theap_get_default() == main && main->tld == tld &&
                heap->theaps == NULL && *p == 42 && mi_heap_of(p) == heap &&
                mi_thread_id_of(p) == 0 && result);
      block_visit_t visit = { 0, 0, false, false };
      result = (mi_heap_visit_abandoned_blocks(heap, true, &block_record, &visit) &&
                visit.blocks == 1 && result);
      theap = mi_heap_theap(heap);
      int* r = (int*)mi_heap_malloc(heap, sizeof(int));
      result = (r != NULL && theap->tld == tld && mi_heap_of(r) == heap && result);
      mi_free(r);
      mi_theap_delete(theap);
    }
    mi_free(p);
    mi_free(q);
    mi_heap_destroy(heap);
  }
  mi_theap_delete(main);
  void* p = mi_malloc(64);
  result = (p != NULL && mi_theap_get_default() == main && main->tld == tld && result);
  mi_free(p);
  return result;
}

#define CLEANUP_BLOCKS 64
static mi_heap_t* cleanup_heap;
static mi_theap_t* cleanup_theap;
static void* cleanup_blocks[CLEANUP_BLOCKS];
static _Atomic(size_t) cleanup_state;

static bool abandon_owner(void) {
  cleanup_theap = mi_heap_theap(cleanup_heap);
  mi_theap_set_default(cleanup_theap);
  bool result = true;
  for (size_t i = 0; i < CLEANUP_BLOCKS; i++) {
    cleanup_blocks[i] = mi_malloc(4096);
    if (cleanup_blocks[i] == NULL) { result = false; }
    else { memset(cleanup_blocks[i], (int)i, 4096); }
  }
  mi_free(mi_malloc(128));
  (void)mi_heap_theap(cleanup_heap);
  mi_atomic_store_release(&cleanup_state, 1);
  while (mi_atomic_load_acquire(&cleanup_state) < 2) { _mi_prim_thread_yield(); }
  result = (cleanup_theap->page_count == 0 && cleanup_theap->page_count_os == 0 &&
            mi_theap_get_default() == cleanup_theap && mi_heap_theap(cleanup_heap) == cleanup_theap && result);
  void* p = mi_malloc(64);
  void* q = mi_theap_malloc(cleanup_theap, 128);
  void* r = mi_heap_malloc(cleanup_heap, 256);
  result = (p != NULL && q != NULL && r != NULL && mi_heap_of(p) == cleanup_heap &&
            mi_heap_of(q) == cleanup_heap && mi_heap_of(r) == cleanup_heap && result);
  mi_free(p);
  mi_free(q);
  mi_free(r);
  return result; // leave the default set to exercise automatic thread cleanup
}

static bool test_theap_abandon_foreign(void) {
  cleanup_heap = mi_heap_new();
  if (cleanup_heap == NULL) return false;
  mi_theap_t* const local = mi_heap_theap(cleanup_heap);
  int* p = (int*)mi_theap_malloc(local, sizeof(int));
  if (p != NULL) { *p = 42; }
  mi_atomic_store_release(&cleanup_state, 0);
  mi_thread_fun_args_t args = { &abandon_owner, false };
  #ifdef _WIN32
  HANDLE thread = CreateThread(NULL, 0, &mi_win_thread_entry, &args, 0, NULL);
  const bool started = (thread != NULL);
  #else
  pthread_t thread;
  const bool started = (pthread_create(&thread, NULL, &mi_pthread_entry, &args) == 0);
  #endif
  bool result = (p != NULL && started);
  if (started) {
    while (mi_atomic_load_acquire(&cleanup_state) < 1) { _mi_prim_thread_yield(); }
    result = (cleanup_theap->pages[MI_BIN_FULL].first != NULL && result);
    mi_free(cleanup_blocks[0]); // collect a pending cross-thread free during abandonment
    cleanup_blocks[0] = NULL;
    mi_theap_quiescent_abandon(cleanup_theap);
    mi_theap_quiescent_abandon(cleanup_theap);
    block_visit_t visit = { 0, 0, false, false };
    result = (mi_heap_visit_blocks(cleanup_heap, true, &block_record, &visit) &&
              visit.blocks == CLEANUP_BLOCKS && mi_heap_theap(cleanup_heap) == local &&
              p != NULL && *p == 42 && mi_thread_id_of(p) == local->tld->thread_id && result);
    for (size_t i = 1; i < CLEANUP_BLOCKS; i++) {
      const uint8_t* block = (const uint8_t*)cleanup_blocks[i];
      result = (block != NULL && block[0] == i && block[4095] == i &&
                mi_thread_id_of(block) == 0 && result);
    }
    mi_atomic_store_release(&cleanup_state, 2);
    #ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    #else
    pthread_join(thread, NULL);
    #endif
    result = (args.result && cleanup_heap->theaps == local && local->hnext == NULL && result);
    for (size_t i = 0; i < CLEANUP_BLOCKS; i++) { mi_free(cleanup_blocks[i]); }
  }
  mi_free(p);
  mi_heap_destroy(cleanup_heap);
  cleanup_heap = NULL;
  cleanup_theap = NULL;
  return result;
}

static bool delete_owner(void) {
  mi_theap_t* const main = mi_theap_get_default();
  mi_theap_t* const theap = mi_heap_theap(cleanup_heap);
  cleanup_blocks[0] = mi_theap_malloc(theap, 64);
  const bool allocated = (cleanup_blocks[0] != NULL);
  mi_theap_set_default(theap);
  (void)mi_heap_theap(mi_heap_main()); // the default is deliberately not protected by the cache
  mi_atomic_store_release(&cleanup_state, 1);
  while (mi_atomic_load_acquire(&cleanup_state) < 2) { _mi_prim_thread_yield(); }
  mi_theap_delete(theap);
  mi_atomic_store_release(&cleanup_state, 3);
  void* p = mi_malloc(32);
  const bool result = (allocated && p != NULL && mi_theap_get_default() == main && mi_heap_of(p) == mi_heap_main());
  mi_free(p);
  return result;
}

static bool test_theap_delete_heap_race(void) {
  bool result = true;
  for (size_t i = 0; result && i < 48; i++) {
    cleanup_heap = mi_heap_new();
    if (cleanup_heap == NULL) return false;
    mi_atomic_store_release(&cleanup_state, 0);
    mi_thread_fun_args_t args = { &delete_owner, false };
    #ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, &mi_win_thread_entry, &args, 0, NULL);
    const bool started = (thread != NULL);
    #else
    pthread_t thread;
    const bool started = (pthread_create(&thread, NULL, &mi_pthread_entry, &args) == 0);
    #endif
    if (started) {
      while (mi_atomic_load_acquire(&cleanup_state) < 1) { _mi_prim_thread_yield(); }
      if (i % 3 != 0) { mi_atomic_store_release(&cleanup_state, 2); }
      if (i % 3 == 2) {
        while (mi_atomic_load_acquire(&cleanup_state) < 3) { _mi_prim_thread_yield(); }
      }
      if (i % 2 == 0) { mi_heap_destroy(cleanup_heap); }
                 else { mi_heap_delete(cleanup_heap); }
      if (i % 3 == 0) { mi_atomic_store_release(&cleanup_state, 2); }
      #ifdef _WIN32
      WaitForSingleObject(thread, INFINITE);
      CloseHandle(thread);
      #else
      pthread_join(thread, NULL);
      #endif
      result = args.result;
      if (i % 2 != 0) { mi_free(cleanup_blocks[0]); }
    }
    else {
      mi_heap_destroy(cleanup_heap);
      result = false;
    }
    cleanup_heap = NULL;
  }
  return result;
}

static bool test_theap_cleanup(bool os_pages) {
  const long disallow = mi_option_get(mi_option_disallow_arena_alloc);
  const long guarded = mi_option_get(mi_option_guarded_sample_rate);
  const long retain = mi_option_get(mi_option_page_full_retain);
  mi_option_set(mi_option_disallow_arena_alloc, (os_pages ? 1 : 0));
  mi_option_set(mi_option_guarded_sample_rate, 0);
  mi_option_set(mi_option_page_full_retain, -1);
  const bool result = (mi_run_on_thread(&test_theap_delete_owner) &&
                       test_theap_abandon_foreign() && test_theap_delete_heap_race());
  mi_option_set(mi_option_disallow_arena_alloc, disallow);
  mi_option_set(mi_option_guarded_sample_rate, guarded);
  mi_option_set(mi_option_page_full_retain, retain);
  return result;
}

int main(void) {
  CHECK("theap-cleanup-arena", test_theap_cleanup(false));
  CHECK("theap-cleanup-os", test_theap_cleanup(true));
  CHECK("theap-collect-detached", test_theap_collect_detached());
  CHECK("theap-collect-detached-thread", mi_run_on_thread(&test_theap_collect_detached));
  CHECK("thread-id-of-arena", test_thread_id_of(false, 1));
  CHECK("thread-id-of-os", test_thread_id_of(true, 1));
  CHECK("reclaim-on-free-snapshot-disabled", test_thread_id_of(false, -1));
  CHECK("reclaim-on-free-snapshot-origin-only", test_thread_id_of(false, 0));
  CHECK("arena-order", test_arena_order());
  CHECK("arena-filter", test_arena_filter());
  CHECK("arena-stop", test_arena_stop());
  CHECK("arena-snapshot", test_arena_snapshot());
  CHECK("arena-result", test_arena_result());
  CHECK("heap-visit-stop", test_heap_visit_stop());
  CHECK("heap-visit-owned-os", test_heap_visit_owned_os(false));
  CHECK("heap-visit-owned-os-mixed", test_heap_visit_owned_os(true));
  CHECK("heap-page-count-os", test_heap_page_count_os());
  CHECK("heap-visit-thread-exit", test_heap_visit_thread_exit());
  return print_test_summary();
}
