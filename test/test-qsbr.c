/* ----------------------------------------------------------------------------
Copyright (c) 2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>

struct qsbr_domain_s;
struct mi_page_qsbr_s {
  uint64_t goal;
};
struct mi_heap_qsbr_s {
  struct qsbr_domain_s* domain;
};
#define MI_CPYTHON 1
#undef MI_MALLOC_OVERRIDE
#include "../src/static.c"
#include "testhelper.h"

typedef struct qsbr_domain_s {
  _Atomic(uintptr_t) sequence;
  _Atomic(uintptr_t) completed;
} qsbr_domain_t;

static qsbr_domain_t domain;
static _Atomic(size_t) last_page_bytes;
static mi_decl_thread qsbr_domain_t* current_domain = &domain;
static mi_decl_thread size_t heap_init_count;

void mi_heap_qsbr_init(mi_heap_qsbr_t* heap) {
  mi_assert_internal(current_domain != NULL);
  heap->domain = current_domain;
  heap_init_count++;
}

void mi_page_qsbr_clear(mi_page_qsbr_t* page) {
  page->goal = 0;
}

bool mi_page_qsbr_is_clear(const mi_page_qsbr_t* page) {
  return page->goal == 0;
}

bool mi_page_qsbr_should_delay(mi_page_qsbr_t* page, mi_heap_qsbr_t* heap, size_t page_bytes) {
  qsbr_domain_t* d = heap->domain;
  mi_assert_internal(d != NULL);
  if (page->goal == 0) {
    mi_atomic_store_relaxed(&last_page_bytes, page_bytes);
    page->goal = mi_atomic_add_acq_rel(&d->sequence, 1) + 1;
  }
  return page->goal > mi_atomic_load_acquire(&d->completed);
}

#define require(expr) do { if (!(expr)) { \
  fprintf(stderr, "\n%s:%d: %s\n", __FILE__, __LINE__, #expr); return false; \
} } while (0)

static void qsbr_complete(void) {
  mi_atomic_store_release(&domain.completed, mi_atomic_load_acquire(&domain.sequence));
}

static size_t qsbr_count(mi_theap_t* theap) {
  size_t count = 0;
  for (mi_page_t* page = theap->qsbr_first; page != NULL; page = page->qsbr_next) { count++; }
  return count;
}

static bool count_area(const mi_heap_t* heap, const mi_heap_area_t* area, void* block, size_t size, void* arg) {
  MI_UNUSED(heap); MI_UNUSED(area); MI_UNUSED(block); MI_UNUSED(size);
  (*(size_t*)arg)++;
  return true;
}

static size_t page_count(mi_heap_t* heap) {
  size_t count = 0;
  mi_heap_visit_blocks(heap, false, &count_area, &count);
  return count;
}

static bool test_unprotected_heap(void) {
  const size_t init_count = heap_init_count;
  mi_heap_t* heap = mi_heap_new();
  require(heap != NULL);
  require(heap_init_count == init_count);
  heap->qsbr.domain = &domain; // runtime metadata alone must not enable QSBR
  void* p = mi_heap_malloc(heap, 64);
  require(p != NULL);
  mi_free(p);
  mi_heap_collect(heap, true);
  require(page_count(heap) == 0 && qsbr_count(mi_heap_theap(heap)) == 0);
  mi_heap_delete(heap);
  require(heap_init_count == init_count);
  return true;
}

static bool test_origin_reclaim(long mode) {
  const long previous = mi_option_get(mi_option_page_reclaim_on_free);
  mi_option_set(mi_option_page_reclaim_on_free, mode);
  mi_heap_t* heap = mi_heap_new_qsbr();
  mi_option_set(mi_option_page_reclaim_on_free, previous);
  require(heap != NULL);
  void* p = mi_heap_malloc(heap, 64);
  void* q = mi_heap_malloc(heap, 64);
  require(p != NULL && q != NULL);
  mi_page_t* page = mi_ptr_page(p);
  require(mi_ptr_page(q) == page);
  _mi_page_abandon(page, mi_page_queue_of(page));
  require(mi_page_is_abandoned(page));
  mi_free(p);
  require(mi_page_is_abandoned(page) == (mode < 0));
  mi_free(q);
  mi_heap_destroy(heap);
  return true;
}

static bool test_delayed_prefix(void) {
  mi_heap_t* heap = mi_heap_new_qsbr();
  require(heap != NULL);
  void* blocks[1024];
  size_t count = 0;
  size_t pages = 0;
  mi_page_t* last = NULL;
  while (pages < 6 && count < 1024) {
    void* p = mi_heap_malloc(heap, 8192);
    require(p != NULL);
    blocks[count++] = p;
    if (mi_ptr_page(p) != last) { last = mi_ptr_page(p); pages++; }
  }
  require(pages == 6);
  for (size_t i = 0; i < count; i++) { mi_free(blocks[i]); }
  mi_heap_collect(heap, true);
  mi_theap_t* theap = mi_heap_theap(heap);
  require(page_count(heap) == 6 && qsbr_count(theap) == 6);
  mi_page_t* second = theap->qsbr_first->qsbr_next;
  mi_atomic_store_release(&domain.completed, second->qsbr.goal);
  _mi_theap_qsbr_collect_delayed(theap);
  require(page_count(heap) == 4 && qsbr_count(theap) == 4);
  qsbr_complete();
  _mi_theap_qsbr_collect_delayed(theap);
  require(page_count(heap) == 0 && theap->qsbr_first == NULL && theap->qsbr_last == NULL);
  mi_heap_destroy(heap);
  return true;
}

static bool test_abandon_queued_page(void) {
  mi_heap_t* heap = mi_heap_new_qsbr();
  require(heap != NULL);
  mi_theap_t* theap = mi_heap_theap(heap);
  mi_page_t* pages[3];
  for (size_t i = 0; i < 3; i++) {
    void* p = mi_heap_malloc(heap, (size_t)64 << i);
    require(p != NULL);
    pages[i] = mi_ptr_page(p);
    mi_free(p);
    mi_heap_collect(heap, true);
  }
  require(qsbr_count(theap) == 3 && theap->qsbr_first == pages[0] && theap->qsbr_last == pages[2]);
  const uint64_t goal = pages[1]->qsbr.goal;
  _mi_page_abandon(pages[1], mi_page_queue_of(pages[1]));
  require(mi_page_is_abandoned(pages[1]) && pages[1]->qsbr.goal == goal);
  require(!pages[1]->qsbr_in_queue && pages[1]->qsbr_prev == NULL && pages[1]->qsbr_next == NULL);
  require(qsbr_count(theap) == 2 && pages[0]->qsbr_next == pages[2] && pages[2]->qsbr_prev == pages[0]);
  qsbr_complete();
  _mi_theap_qsbr_collect_delayed(theap);
  require(page_count(heap) == 1 && theap->qsbr_first == NULL && theap->qsbr_last == NULL);
  _mi_heap_collect_abandoned(heap);
  require(page_count(heap) == 0);
  mi_heap_destroy(heap);
  return true;
}

static bool test_separate_delayed_lists(void) {
  const size_t init_count = heap_init_count;
  mi_heap_t* first = mi_heap_new_qsbr();
  require(first != NULL && heap_init_count == init_count + 1);
  qsbr_domain_t second_domain = mi_init_struct_zero;
  mi_atomic_store_relaxed(&second_domain.sequence, mi_atomic_load_relaxed(&domain.sequence));
  current_domain = &second_domain;
  mi_heap_t* second = mi_heap_new_qsbr();
  current_domain = &domain;
  require(second != NULL && heap_init_count == init_count + 2);
  void* p = mi_heap_malloc(first, 64);
  void* q = mi_heap_malloc(second, 128);
  require(p != NULL && q != NULL);
  mi_free(p);
  mi_free(q);
  mi_heap_collect(first, true);
  mi_heap_collect(second, true);
  mi_theap_t* first_theap = mi_heap_theap(first);
  mi_theap_t* second_theap = mi_heap_theap(second);
  require(page_count(first) == 1 && page_count(second) == 1);
  require(qsbr_count(first_theap) == 1 && qsbr_count(second_theap) == 1);

  qsbr_complete();
  _mi_theap_qsbr_collect_delayed(second_theap);
  require(page_count(first) == 1 && page_count(second) == 1);
  _mi_theap_qsbr_collect_delayed(first_theap);
  require(page_count(first) == 0 && page_count(second) == 1);
  require(qsbr_count(first_theap) == 0 && qsbr_count(second_theap) == 1);
  mi_atomic_store_release(&second_domain.completed, mi_atomic_load_acquire(&second_domain.sequence));
  _mi_theap_qsbr_collect_delayed(first_theap);
  require(page_count(second) == 1 && qsbr_count(second_theap) == 1);
  _mi_theap_qsbr_collect_delayed(second_theap);
  require(page_count(second) == 0 && qsbr_count(second_theap) == 0);
  mi_heap_destroy(first);
  mi_heap_destroy(second);
  require(heap_init_count == init_count + 2);
  return true;
}

static void* remote_block;
static bool remote_free(void) {
  require(!_mi_thread_is_initialized());
  mi_free(remote_block);
  require(!_mi_thread_is_initialized());
  return true;
}

static bool remote_free_abandoned(void) {
  require(!_mi_thread_is_initialized());
  mi_page_t* page = mi_ptr_page(remote_block);
  require(mi_page_is_abandoned(page));
  mi_free(remote_block);
  require(_mi_thread_is_initialized());
  mi_theap_t* theap = _mi_page_associated_theap_peek(page);
  require(theap != NULL && !mi_page_is_abandoned(page));
  require(mi_page_theap(page) == theap && mi_page_all_free(page));
  require(page->qsbr.goal != 0 && page->qsbr_in_queue && qsbr_count(theap) == 1);
  return true;
}

static void* full_page_blocks[1024];
static size_t full_page_count;

static bool remote_free_full_page(void) {
  for (size_t i = 0; i + 1 < full_page_count; i++) { mi_free(full_page_blocks[i]); }
  remote_block = full_page_blocks[full_page_count - 1];
  return (mi_page_is_abandoned(mi_ptr_page(remote_block)) ? remote_free_abandoned() : remote_free());
}

static bool test_page_free(size_t size, bool abandon) {
  mi_heap_t* heap = mi_heap_new_qsbr();
  require(heap != NULL);
  const long previous = mi_option_get(mi_option_page_full_retain);
  mi_option_set(mi_option_page_full_retain, abandon ? 0 : -1);
  mi_theap_t* theap = mi_heap_theap(heap);
  mi_option_set(mi_option_page_full_retain, previous);
  require(theap != NULL && theap->allow_page_abandon == abandon);
  full_page_blocks[0] = mi_heap_malloc(heap, size);
  require(full_page_blocks[0] != NULL);
  mi_page_t* page = mi_ptr_page(full_page_blocks[0]);
  full_page_count = page->reserved;
  require(full_page_count <= sizeof(full_page_blocks)/sizeof(full_page_blocks[0]));
  for (size_t i = 1; i < full_page_count; i++) {
    full_page_blocks[i] = mi_heap_malloc(heap, size);
    require(full_page_blocks[i] != NULL && mi_ptr_page(full_page_blocks[i]) == page);
  }
  require(mi_page_is_full(page));
  if (!mi_page_is_abandoned(page) && !mi_page_is_in_full(page)) {
    _mi_page_to_full(page, mi_page_queue_of(page));
  }
  require(mi_page_is_abandoned(page) == abandon);
  require(mi_page_is_in_full(page) == !abandon);
  require(mi_run_on_thread(&remote_free_full_page));
  if (abandon) {
    require(mi_page_is_abandoned(page) && !mi_page_is_owned(page));
    require(mi_page_all_free(page) && !page->qsbr_in_queue && qsbr_count(theap) == 0);
    const uint64_t goal = page->qsbr.goal;
    require(goal != 0);
    require(mi_atomic_load_relaxed(&last_page_bytes) == page->capacity * mi_page_block_size(page));
    _mi_heap_collect_abandoned(heap);
    require(page_count(heap) == 1 && page->qsbr.goal == goal);
    qsbr_complete();
    _mi_heap_collect_abandoned(heap);
  }
  else {
    _mi_page_free_collect(page, true);
    require(mi_page_all_free(page) && mi_page_is_in_full(page));
    require(!_mi_page_free(page, mi_page_queue_of(page)));
    require(mi_atomic_load_relaxed(&last_page_bytes) == page->capacity * mi_page_block_size(page));
    require(!mi_page_is_in_full(page) && page->retire_expire == 0);
    require(theap->qsbr_first == page && theap->qsbr_last == page && page_count(heap) == 1);
    qsbr_complete();
    require(_mi_page_free(page, mi_page_queue_of(page)));
  }
  require(theap->qsbr_first == NULL && theap->qsbr_last == NULL && page_count(heap) == 0);
  mi_heap_destroy(heap);
  return true;
}

static bool test_reuse(bool remote) {
  mi_heap_t* heap = mi_heap_new_qsbr();
  require(heap != NULL);
  void* p = mi_heap_malloc(heap, 64);
  require(p != NULL);
  mi_page_t* page = mi_ptr_page(p);
  mi_free(p);
  mi_heap_collect(heap, true);
  const uint64_t old_goal = page->qsbr.goal;
  require(old_goal != 0);
  require(mi_atomic_load_relaxed(&last_page_bytes) == page->capacity * mi_page_block_size(page));
  p = mi_heap_malloc(heap, 64);
  require(p != NULL && mi_ptr_page(p) == page);
  // Exercise the fast allocation path, which deliberately leaves an old goal in place.
  require(page->qsbr.goal == old_goal);
  page->retire_expire = 2;
  if (remote) {
    remote_block = p;
    require(mi_run_on_thread(&remote_free));
  }
  else {
    mi_free(p);
    require(_mi_page_qsbr_is_clear(page));
  }
  mi_atomic_store_release(&domain.completed, old_goal);
  mi_heap_collect(heap, true);
  require(page_count(heap) == 1 && page->qsbr.goal > old_goal);
  qsbr_complete();
  mi_heap_collect(heap, true);
  require(page_count(heap) == 0);
  mi_heap_destroy(heap);
  return true;
}

static mi_heap_t* worker_heap;
static size_t worker_size;
static bool worker_empty;
static bool worker_delete;
static void* worker_blocks[2];
static mi_page_t* worker_page;
static uint64_t worker_goal;

static bool allocate_and_exit(void) {
  worker_blocks[0] = mi_heap_malloc(worker_heap, worker_size);
  require(worker_blocks[0] != NULL);
  worker_page = mi_ptr_page(worker_blocks[0]);
  worker_blocks[1] = NULL;
  if (!mi_page_is_singleton(worker_page)) {
    worker_blocks[1] = mi_heap_malloc(worker_heap, worker_size);
    require(worker_blocks[1] != NULL && mi_ptr_page(worker_blocks[1]) == worker_page);
  }
  if (worker_empty) {
    mi_free(worker_blocks[0]);
    mi_free(worker_blocks[1]);
    mi_heap_collect(worker_heap, true);
  }
  worker_goal = worker_page->qsbr.goal;
  if (worker_delete) {
    mi_theap_delete(mi_heap_theap(worker_heap));
    require(!worker_page->qsbr_in_queue && worker_page->qsbr_next == NULL && worker_page->qsbr_prev == NULL);
  }
  return true;
}

static bool test_reclaim(void) {
  worker_heap = mi_heap_new_qsbr();
  require(worker_heap != NULL);
  mi_theap_t* theap = mi_heap_theap(worker_heap);
  worker_size = 64;
  worker_empty = true;
  worker_delete = true;
  require(mi_run_on_thread(&allocate_and_exit));
  require(worker_goal != 0 && worker_page->qsbr.goal == worker_goal);
  require(mi_page_is_abandoned(worker_page) && !worker_page->qsbr_in_queue);
  // Give the receiving list a newer goal before reclaiming the old page.
  void* newer = mi_heap_malloc(worker_heap, 256);
  require(newer != NULL);
  mi_page_t* newer_page = mi_ptr_page(newer);
  mi_free(newer);
  mi_heap_collect(worker_heap, true);
  require(theap->qsbr_first == newer_page && newer_page->qsbr.goal > worker_goal);
  mi_page_t* reclaimed = _mi_page_fresh(theap, mi_page_queue(theap, worker_size + MI_PADDING_SIZE));
  require(reclaimed == worker_page && reclaimed->qsbr.goal == worker_goal);
  require(theap->qsbr_first == newer_page && theap->qsbr_last == reclaimed && qsbr_count(theap) == 2);
  mi_atomic_store_release(&domain.completed, worker_goal);
  _mi_theap_qsbr_collect_delayed(theap);
  require(page_count(worker_heap) == 2 && qsbr_count(theap) == 2);
  require(reclaimed->qsbr.goal == worker_goal);
  qsbr_complete();
  _mi_theap_qsbr_collect_delayed(theap);
  require(page_count(worker_heap) == 0 && theap->qsbr_first == NULL && theap->qsbr_last == NULL);
  mi_heap_destroy(worker_heap);
  return true;
}

static bool test_reclaim_delayed_empty(size_t size, bool os) {
  const long previous = mi_option_get(mi_option_page_reclaim_on_free);
  mi_option_set(mi_option_page_reclaim_on_free, -1);
  worker_heap = mi_heap_new_qsbr();
  mi_option_set(mi_option_page_reclaim_on_free, previous);
  require(worker_heap != NULL);
  mi_theap_t* theap = mi_heap_theap(worker_heap);
  worker_size = size;
  worker_empty = false;
  worker_delete = false;
  mi_option_set(mi_option_disallow_arena_alloc, os ? 1 : 0);
  require(mi_run_on_thread(&allocate_and_exit));
  mi_option_set(mi_option_disallow_arena_alloc, 0);
  require((worker_page->memid.memkind != MI_MEM_ARENA) == os);
  if (worker_blocks[1] != NULL) {
    mi_free(worker_blocks[1]);
    require(mi_page_is_abandoned(worker_page));
    require(mi_page_claim_ownership(worker_page));
    worker_page->retire_expire = 2;
    require(!mi_abandoned_page_unown(worker_page, NULL));
  }
  mi_free(worker_blocks[0]);
  require(!mi_page_is_abandoned(worker_page) && mi_page_theap(worker_page) == theap);
  require(worker_page->heap == worker_heap && mi_page_all_free(worker_page));
  require(worker_page->retire_expire == 0 && !mi_page_is_in_full(worker_page));
  require(qsbr_count(theap) == 1 && page_count(worker_heap) == 1);
  const uint64_t goal = worker_page->qsbr.goal;
  require(goal != 0);
  mi_heap_collect(worker_heap, true);
  require(page_count(worker_heap) == 1 && worker_page->qsbr.goal == goal);
  qsbr_complete();
  mi_heap_collect(worker_heap, true);
  require(page_count(worker_heap) == 0 && qsbr_count(theap) == 0);
  mi_heap_destroy(worker_heap);
  return true;
}

static bool test_reclaim_ready_pages(void) {
  mi_heap_t* heap = mi_heap_new_qsbr();
  require(heap != NULL);
  mi_theap_t* theap = mi_heap_theap(heap);
  void* blocks[128];
  size_t count = 0;
  size_t pages = 0;
  mi_page_t* last = NULL;
  while (pages < 3 && count < 128) {
    void* p = mi_heap_malloc(heap, 8192);
    require(p != NULL);
    blocks[count++] = p;
    if (mi_ptr_page(p) != last) { last = mi_ptr_page(p); pages++; }
  }
  require(pages == 3);
  for (size_t i = 0; i < count; i++) { mi_free(blocks[i]); }
  _mi_theap_collect_abandon(theap);
  require(page_count(heap) == 3 && theap->page_count == 0);
  qsbr_complete();
  mi_page_t* page = _mi_page_fresh(theap, mi_page_queue(theap, 8192 + MI_PADDING_SIZE));
  require(page != NULL && mi_page_immediate_available(page));
  require(page_count(heap) == 1 && _mi_page_qsbr_is_clear(page));
  mi_heap_destroy(heap);
  return true;
}

static bool test_orphan(size_t size, bool os, bool empty) {
  worker_heap = mi_heap_new_qsbr();
  require(worker_heap != NULL);
  // Establish a distinct receiving theap before the originating theap can be freed.
  mi_theap_t* theap = mi_heap_theap(worker_heap);
  mi_option_set(mi_option_disallow_arena_alloc, os ? 1 : 0);
  worker_size = size;
  worker_empty = empty;
  worker_delete = false;
  require(mi_run_on_thread(&allocate_and_exit));
  mi_option_set(mi_option_disallow_arena_alloc, 0);
  require((worker_page->memid.memkind != MI_MEM_ARENA) == os);
  require(mi_page_is_abandoned(worker_page) && !worker_page->qsbr_in_queue);
  require(worker_page->qsbr_next == NULL && worker_page->qsbr_prev == NULL);
  if (!empty) {
    require(worker_goal == 0);
    if (worker_blocks[1] != NULL) {
      mi_free(worker_blocks[1]);
      // Origin-only reclaim must reject a different theap, even with an empty size queue.
      require(mi_page_is_abandoned(worker_page) && theap->page_count == 0);
    }
    remote_block = worker_blocks[0];
    require(mi_run_on_thread(&remote_free_abandoned));
    require(worker_page->qsbr.goal != 0 && mi_page_all_free(worker_page));
    require(mi_page_is_abandoned(worker_page) && !mi_page_is_owned(worker_page) && !worker_page->qsbr_in_queue);
  }
  _mi_heap_collect_abandoned(worker_heap);
  require(page_count(worker_heap) == 1);
  qsbr_complete();
  _mi_heap_collect_abandoned(worker_heap);
  require(page_count(worker_heap) == 0);
  mi_heap_destroy(worker_heap);
  return true;
}

static bool destroy_from_other_thread(void) {
  mi_heap_destroy(worker_heap);
  return true;
}

static bool test_terminal_destroy(bool remote) {
  mi_heap_t* heap = mi_heap_new_qsbr();
  require(heap != NULL);
  void* p = mi_heap_malloc(heap, 128);
  require(p != NULL);
  mi_free(p);
  mi_heap_collect(heap, true);
  mi_theap_t* theap = mi_heap_theap(heap);
  require(qsbr_count(theap) == 1);
  mi_heap_delete(heap);
  require(page_count(heap) == 1 && qsbr_count(theap) == 1);
  if (remote) {
    worker_heap = heap;
    require(mi_run_on_thread(&destroy_from_other_thread));
  }
  else {
    mi_heap_destroy(heap);
  }
  return true;
}

static bool test_free_during_unown(bool from_free) {
  worker_heap = mi_heap_new_qsbr();
  require(worker_heap != NULL);
  worker_size = 64;
  worker_empty = false;
  worker_delete = false;
  require(mi_run_on_thread(&allocate_and_exit));
  require(mi_page_claim_ownership(worker_page));
  // Queue the last frees while ownership is held, then exercise the unown retry.
  mi_free(worker_blocks[0]);
  mi_free(worker_blocks[1]);
  if (from_free) {
    mi_abandoned_page_unown_from_free(worker_page, NULL);
  }
  else {
    require(!mi_abandoned_page_unown(worker_page, NULL));
  }
  require(mi_page_all_free(worker_page) && worker_page->qsbr.goal != 0);
  if (from_free) {
    require(!mi_page_is_abandoned(worker_page) && mi_page_is_owned(worker_page));
    require(_mi_page_associated_theap_peek(worker_page) == mi_page_theap(worker_page));
    require(worker_page->qsbr_in_queue);
  }
  else {
    require(!mi_page_is_owned(worker_page) && mi_page_is_abandoned_mapped(worker_page));
    require(!worker_page->qsbr_in_queue);
  }
  qsbr_complete();
  if (from_free) { mi_heap_collect(worker_heap, true); }
           else { _mi_heap_collect_abandoned(worker_heap); }
  require(page_count(worker_heap) == 0);
  mi_heap_destroy(worker_heap);
  return true;
}

static bool test_full_page_free_during_unown(void) {
  mi_heap_t* heap = mi_heap_new_qsbr();
  require(heap != NULL);
  mi_theap_t* theap = mi_heap_theap(heap);
  const size_t size = 8192;
  full_page_blocks[0] = mi_heap_malloc(heap, size);
  require(full_page_blocks[0] != NULL);
  mi_page_t* page = mi_ptr_page(full_page_blocks[0]);
  require(page->memid.memkind == MI_MEM_ARENA && !mi_page_is_singleton(page));
  full_page_count = page->reserved;
  require(full_page_count <= sizeof(full_page_blocks)/sizeof(full_page_blocks[0]));
  for (size_t i = 1; i < full_page_count; i++) {
    full_page_blocks[i] = mi_heap_malloc(heap, size);
    require(full_page_blocks[i] != NULL && mi_ptr_page(full_page_blocks[i]) == page);
  }
  require(mi_page_is_full(page));
  if (!mi_page_is_abandoned(page)) { _mi_page_abandon(page, mi_page_queue_of(page)); }
  require(mi_page_is_abandoned(page) && !mi_page_is_abandoned_mapped(page));
  require(mi_page_claim_ownership(page));
  // Keep the frees pending until the unmapped page is unowned.
  for (size_t i = 0; i < full_page_count; i++) { mi_free(full_page_blocks[i]); }
  require(mi_page_is_full(page));
  require(!mi_abandoned_page_unown(page, theap));
  require(mi_page_all_free(page) && !mi_page_is_owned(page) && mi_page_is_abandoned_mapped(page));
  const uint64_t goal = page->qsbr.goal;
  require(goal != 0 && !page->qsbr_in_queue);
  const size_t bin = _mi_bin(mi_page_block_size(page));
  require(mi_atomic_load_relaxed(&heap->abandoned_count[bin]) == 1);
  mi_page_t* reclaimed = _mi_page_fresh(theap, mi_page_queue(theap, size + MI_PADDING_SIZE));
  require(reclaimed == page && page->qsbr.goal == goal && page->qsbr_in_queue);
  require(mi_atomic_load_relaxed(&heap->abandoned_count[bin]) == 0);
  void* p = mi_heap_malloc(heap, size);
  require(p != NULL && mi_ptr_page(p) == page);
  mi_free(p);
  mi_heap_collect(heap, true);
  qsbr_complete();
  _mi_theap_qsbr_collect_delayed(theap);
  require(page_count(heap) == 0 && qsbr_count(theap) == 0);
  mi_heap_destroy(heap);
  return true;
}

static bool test_destroy_empty_orphan(void) {
  worker_heap = mi_heap_new_qsbr();
  require(worker_heap != NULL);
  worker_size = 64;
  worker_empty = true;
  worker_delete = false;
  require(mi_run_on_thread(&allocate_and_exit));
  require(mi_page_all_free(worker_page) && worker_goal != 0 && worker_page->qsbr.goal == worker_goal);
  require(mi_page_is_abandoned(worker_page) && !mi_page_is_owned(worker_page) && !worker_page->qsbr_in_queue);
  mi_heap_destroy(worker_heap);
  return true;
}

int main(void) {
  mi_option_set(mi_option_guarded_sample_rate, 0);
  mi_option_set(mi_option_page_reclaim_on_free, 1);
  CHECK("ordinary heap with runtime metadata stays unprotected", test_unprotected_heap());
  CHECK("QSBR cap preserves originating-theap reclaim", test_origin_reclaim(1));
  CHECK("QSBR cap preserves disabled reclaim", test_origin_reclaim(-1));
  CHECK("delayed prefix beyond retirement cache", test_delayed_prefix());
  CHECK("abandon unlinks a middle queue entry before losing its owner", test_abandon_queued_page());
  CHECK("delayed lists are per theap with independent heap contexts", test_separate_delayed_lists());
  CHECK("full queue retains, unfills, and releases a small page", test_page_free(MI_SMALL_MAX_OBJ_SIZE/2, false));
  CHECK("full queue retains, unfills, and releases an automatically full page", test_page_free(MI_SMALL_MAX_OBJ_SIZE+1, false));
  CHECK("full small page is abandoned, reclaimed on last free, and released after exit", test_page_free(MI_SMALL_MAX_OBJ_SIZE/2, true));
  CHECK("automatically full page is abandoned, reclaimed on last free, and released after exit", test_page_free(MI_SMALL_MAX_OBJ_SIZE+1, true));
  CHECK("local reuse starts a new grace period", test_reuse(false));
  CHECK("remote reuse starts a new grace period", test_reuse(true));
  CHECK("reclaim preserves goal, appends at tail, and stops behind an unfinished head", test_reclaim());
  CHECK("delayed empty arena page is reclaimed despite disabled reclaim", test_reclaim_delayed_empty(64, false));
  CHECK("delayed empty OS page is reclaimed despite disabled reclaim", test_reclaim_delayed_empty(64, true));
  CHECK("delayed empty arena singleton is reclaimed", test_reclaim_delayed_empty(2*MI_MiB, false));
  CHECK("delayed empty OS singleton is reclaimed", test_reclaim_delayed_empty(2*MI_MiB, true));
  CHECK("allocation retries when reclaim releases ready empty pages", test_reclaim_ready_pages());
  CHECK("live orphan, origin-only reclaim, last free initializes a receiving thread", test_orphan(64, false, false));
  CHECK("empty abandoned arena singleton", test_orphan(2*MI_MiB, false, true));
  CHECK("live abandoned arena singleton", test_orphan(2*MI_MiB, false, false));
  CHECK("empty abandoned OS singleton", test_orphan(2*MI_MiB, true, true));
  CHECK("live abandoned OS singleton", test_orphan(2*MI_MiB, true, false));
  CHECK("last frees during abandonment unown", test_free_during_unown(false));
  CHECK("last frees during free-side unown", test_free_during_unown(true));
  CHECK("full arena page becomes mapped on delayed last frees during unown", test_full_page_free_during_unown());
  CHECK("terminal destroy of an empty orphan without a prior sweep", test_destroy_empty_orphan());
  CHECK("protected delete rejected, terminal destroy unlinks", test_terminal_destroy(false));
  CHECK("cross-thread terminal destroy unlinks before freeing the owner", test_terminal_destroy(true));
  return print_test_summary();
}
