#include "../src/arena/arena.h"
#include "mimalloc/alloc/page.h"
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
  if (result) result = mi_run_on_thread(&free_owned_blocks);
  const size_t expected = VISIT_OWNED_BLOCKS * 3 / 4 + (mixed ? 1 : 0);
  for (size_t mode = 0; result && mode < 2; mode++) {
    owned_visit_t visit = { 0, 0, 0, 0, { false } };
    result = (mi_heap_visit_blocks(visit_heap, mode != 0, &owned_record, &visit) &&
              visit.used == expected && visit.blocks == (mode != 0 ? expected : 0) &&
              visit.os_areas > 0 && visit.arena_areas == (mixed ? 1 : 0));
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

int main(void) {
  CHECK("arena-order", test_arena_order());
  CHECK("arena-filter", test_arena_filter());
  CHECK("arena-stop", test_arena_stop());
  CHECK("arena-snapshot", test_arena_snapshot());
  CHECK("arena-result", test_arena_result());
  CHECK("heap-visit-stop", test_heap_visit_stop());
  CHECK("heap-visit-owned-os", test_heap_visit_owned_os(false));
  CHECK("heap-visit-owned-os-mixed", test_heap_visit_owned_os(true));
  CHECK("heap-visit-thread-exit", test_heap_visit_thread_exit());
  return print_test_summary();
}
