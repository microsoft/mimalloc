/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "mimalloc/prim.h"
#include "mimalloc/prim-tls.h"

#include <string.h>  // memcpy, memset
#include <stdlib.h>  // atexit


#define MI_MEMID_INIT(kind)   {{{NULL,0}}, kind, true /* pinned */, true /* committed */, false /* zero */ }
#define MI_MEMID_STATIC       MI_MEMID_INIT(MI_MEM_STATIC)

// Empty page used to initialize the small free pages array
static const mi_page_t mi_page_empty = {
  MI_ATOMIC_VAR_INIT(0),  // xthread_id
  NULL,                   // free
  0,                      // used
  0,                      // capacity
  0,                      // reserved capacity
  0,                      // retire_expire
  false,                  // is_zero
  NULL,                   // local_free
  MI_ATOMIC_VAR_INIT(0),  // xthread_free
  0,                      // block_size
  0,                      // page_woffset
  MI_ARENA_SLICE_SIZE,    // page_committed
  #if (MI_PADDING || MI_ENCODE_FREELIST)
  { 0, 0 },               // keys
  #endif
  NULL,                   // theap
  NULL,                   // heap
  NULL, NULL,             // next, prev
  MI_MEMID_STATIC         // memid
};

#define MI_PAGE_EMPTY() ((mi_page_t*)&mi_page_empty)

#if (MI_PADDING>0) && (MI_INTPTR_SIZE >= 8)
#define MI_SMALL_PAGES_EMPTY  { MI_INIT128(MI_PAGE_EMPTY), MI_PAGE_EMPTY(), MI_PAGE_EMPTY() }
#elif (MI_PADDING>0)
#define MI_SMALL_PAGES_EMPTY  { MI_INIT128(MI_PAGE_EMPTY), MI_PAGE_EMPTY(), MI_PAGE_EMPTY(), MI_PAGE_EMPTY() }
#else
#define MI_SMALL_PAGES_EMPTY  { MI_INIT128(MI_PAGE_EMPTY), MI_PAGE_EMPTY() }
#endif


// Empty page queues for every bin
#define QNULL(sz)  { NULL, NULL, 0, (sz)*sizeof(uintptr_t) }
#define MI_PAGE_QUEUES_EMPTY \
  { QNULL(1), \
    QNULL(     1), QNULL(     2), QNULL(     3), QNULL(     4), QNULL(     5), QNULL(     6), QNULL(     7), QNULL(     8), /* 8 */ \
    QNULL(    10), QNULL(    12), QNULL(    14), QNULL(    16), QNULL(    20), QNULL(    24), QNULL(    28), QNULL(    32), /* 16 */ \
    QNULL(    40), QNULL(    48), QNULL(    56), QNULL(    64), QNULL(    80), QNULL(    96), QNULL(   112), QNULL(   128), /* 24 */ \
    QNULL(   160), QNULL(   192), QNULL(   224), QNULL(   256), QNULL(   320), QNULL(   384), QNULL(   448), QNULL(   512), /* 32 */ \
    QNULL(   640), QNULL(   768), QNULL(   896), QNULL(  1024), QNULL(  1280), QNULL(  1536), QNULL(  1792), QNULL(  2048), /* 40 */ \
    QNULL(  2560), QNULL(  3072), QNULL(  3584), QNULL(  4096), QNULL(  5120), QNULL(  6144), QNULL(  7168), QNULL(  8192), /* 48 */ \
    QNULL( 10240), QNULL( 12288), QNULL( 14336), QNULL( 16384), QNULL( 20480), QNULL( 24576), QNULL( 28672), QNULL( 32768), /* 56 */ \
    QNULL( 40960), QNULL( 49152), QNULL( 57344), QNULL( 65536), QNULL( 81920), QNULL( 98304), QNULL(114688), QNULL(131072), /* 64 */ \
    QNULL(163840), QNULL(196608), QNULL(229376), QNULL(262144), QNULL(327680), QNULL(393216), QNULL(458752), QNULL(524288), /* 72 */ \
    QNULL(MI_LARGE_MAX_OBJ_WSIZE + 1  /* 655360, Huge queue */), \
    QNULL(MI_LARGE_MAX_OBJ_WSIZE + 2) /* Full queue */ }

#define MI_STAT_COUNT_NULL()  {0,0,0}

// Empty statistics
#define MI_STAT_COUNT(stat)     {0,0,0},
#define MI_STAT_COUNTER(stat)   {0},

#define MI_STATS_FIELDS_NULL  \
  MI_STAT_FIELDS()                    /* regular stat fields */ \
  { MI_INIT4(MI_STAT_COUNT_NULL) },   /* stat reserved */ \
  { { 0 }, { 0 }, { 0 }, { 0 } },     /* stat counter reserved */ \
  { MI_INIT74(MI_STAT_COUNT_NULL) },  /* malloc_bins */ \
  { MI_INIT74(MI_STAT_COUNT_NULL) },  /* page bins   */ \
  { MI_INIT5(MI_STAT_COUNT_NULL) }    /* chunk bins  */

#define MI_STATS_NULL \
  { sizeof(mi_stats_t), MI_STAT_VERSION, MI_STATS_FIELDS_NULL }

// --------------------------------------------------------
// Statically allocate an empty theap as the initial
// thread local value for the default theap,
// and statically allocate the backing theap for the main
// thread so it can function without doing any allocation
// itself (as accessing a thread local for the first time
// may lead to allocation itself on some platforms)
// --------------------------------------------------------

static mi_decl_cache_align mi_tld_t mi_tld_detached = { 
  MI_THREADID_DETACHED,   // thread_id
  0,                      // thread_seq
  0,                      // default numa node
  NULL,                   // subproc
  NULL,                   // theaps list
  MI_LOCK_INITIALIZER,    // theaps lock
  false,                  // recurse
  false,                  // is_in_threadpool
  MI_MEMID_STATIC         // memid
};

mi_decl_hidden mi_decl_cache_align const mi_theap_t _mi_theap_empty = {
  &mi_tld_detached,       // tld
  MI_ATOMIC_VAR_INIT(NULL), // heap
  MI_ATOMIC_VAR_INIT(NULL), // subproc
  MI_ATOMIC_VAR_INIT(1),  // refcount
  MI_ATOMIC_VAR_INIT(0),  // freed
  0,                      // heartbeat
  0,                      // cookie
  { {0}, {0}, 0, true },  // random
  0,                      // page count
  MI_BIN_FULL, 0,         // page retired min/max
  0,                      // pages_full_size
  0, 0,                   // generic count
  NULL, NULL,             // tnext, tprev
  NULL, NULL,             // hnext, hprev
  0,                      // full page retain
  false,                  // allow reclaim
  true,                   // allow abandon
  true,                   // is_detached
  #if MI_GUARDED
  0, 0, 0, 1,             // sample count is 1 so we never write to it (see `internal.h:mi_theap_malloc_use_guarded`)
  #endif
  MI_SMALL_PAGES_EMPTY,
  MI_PAGE_QUEUES_EMPTY,
  MI_MEMID_STATIC,
  MI_STATS_NULL,          // stats
};

// pre-allocate the process subprocess, heap, and meta-data theap
static mi_decl_cache_align mi_subproc_t mi_process_subproc_main = mi_init_struct_zero;
static mi_decl_cache_align mi_heap_t    mi_process_heap_main  = mi_init_struct_zero;
static mi_decl_cache_align mi_theap_t   mi_process_theap_meta = mi_init_struct_zero;

// pre-allocate the initial tld and theap for the main thread (this is not strictly needed but nice for stats)
static mi_decl_cache_align mi_tld_t     mi_process_tld_main   = mi_init_struct_zero;
static mi_decl_cache_align mi_theap_t   mi_process_theap_main = mi_init_struct_zero;

mi_decl_hidden mi_decl_cache_align mi_theap_t _mi_theap_empty_wrong = mi_init_struct_zero;  // used for error paths
mi_decl_hidden mi_decl_thread void* __mi_thread_id_helper = NULL;
mi_decl_hidden bool                 _mi_process_is_initialized = false;  // set to `true` in `mi_process_init`.

static mi_subproc_t* mi_subprocs = NULL;
static mi_lock_t     mi_subprocs_lock = MI_LOCK_INITIALIZER;

#if MI_TLS_MODEL_LOCAL
// the thread-local main theap for allocation
mi_decl_hidden mi_decl_thread mi_theap_t* __mi_theap_default = (mi_theap_t*)&_mi_theap_empty;
// the last used non-main theap
mi_decl_hidden mi_decl_thread mi_theap_t* __mi_theap_cached = (mi_theap_t*)&_mi_theap_empty;
#endif

#undef MI_STAT_COUNT
#undef MI_STAT_COUNTER

mi_threadid_t _mi_thread_id(void) mi_attr_noexcept {
  const mi_threadid_t tid = _mi_prim_thread_id();
  mi_assert_internal( (tid & MI_PAGE_FLAG_MASK) == 0 ); // mimalloc reserves the bottom 2 bits
  return tid;
}

/* -----------------------------------------------------------
  Initialization
  Note: on some platforms lock_init or just a thread local access
  can cause allocation and induce recursion during initialization.
----------------------------------------------------------- */

// Initialize main heap
static mi_tld_t* mi_tld_init(mi_tld_t* tld, size_t tseq, mi_subproc_t* subproc);
static mi_subproc_t* mi_subproc_init(mi_subproc_t* subproc, mi_subproc_t* parent);

static void mi_heap_main_init(void) {
  if mi_unlikely(mi_process_heap_main.subproc == NULL) {
    mi_memid_t memid_static = _mi_memid_create(MI_MEM_STATIC);
    mi_lock_init(&mi_subprocs_lock);
    _mi_memcpy(&_mi_theap_empty_wrong,&_mi_theap_empty,sizeof(_mi_theap_empty_wrong));
    
    // main subprocess
    mi_process_subproc_main.memid = memid_static;
    mi_subproc_init(&mi_process_subproc_main,NULL);

    // detached tld for mi_theap_empty (and theap_meta)
    mi_tld_detached.memid = memid_static;
    mi_tld_init(&mi_tld_detached, 0, &mi_process_subproc_main);
        
    // main process heap
    mi_process_heap_main.memid = memid_static;
    mi_atomic_store_ptr_release(mi_heap_t,&mi_process_subproc_main.heap_main,&mi_process_heap_main);
    _mi_heap_init(&mi_process_heap_main,mi_thread_local_key_fast,&mi_process_subproc_main,0);
    
    // detached theap for allocating meta-data (we can allocate on this without having an initialized thread)
    mi_process_theap_meta.memid = memid_static;
    _mi_theap_init(&mi_process_theap_meta,&mi_process_heap_main,&mi_tld_detached);
    mi_process_theap_meta.allow_page_abandon = false;  // for security, don't share with other threads
    mi_process_theap_meta.page_full_retain = 2;
    mi_process_subproc_main.theap_meta = &mi_process_theap_meta;

    // mi_heap_theap_set(&mi_process_heap_main,&mi_process_theap_main); // set in `mi_thread_init(_theap_default)`
  }
}

void* _mi_meta_zalloc( mi_subproc_t* subproc, size_t size, mi_memid_t* memid ) {
  mi_assert_internal(subproc->theap_meta != NULL);
  void* p;
  mi_lock(&subproc->theap_meta_lock) {
    p = mi_theap_zalloc(subproc->theap_meta, size);
    if (memid != NULL) { *memid = (p==NULL ? _mi_memid_none() : _mi_memid_create_malloc(p,size,true) ); }
  }
  return p;
}

void* _mi_meta_zalloc_aligned( mi_subproc_t* subproc, size_t size, size_t aligned, mi_memid_t* memid ) {
  mi_assert_internal(subproc->theap_meta != NULL);
  void* p;
  mi_lock(&subproc->theap_meta_lock) {
    p = mi_theap_zalloc_aligned(subproc->theap_meta, size, aligned);
    if (memid != NULL) { *memid = (p==NULL ? _mi_memid_none() : _mi_memid_create_malloc(p,size,true) ); }
  }
  return p;
}

void _mi_meta_free(mi_subproc_t* subproc, void* p, mi_memid_t memid) {
  if (p==NULL || mi_memid_needs_no_free(memid)) return;
  if (memid.memkind == MI_MEM_MALLOC) {
    mi_free(p);
  }
  else {
    mi_assert_internal(subproc!=NULL);  
    _mi_arenas_free(subproc, p, _mi_memid_size(memid), memid);
  }
}

bool _mi_meta_is_meta_page(const mi_subproc_t* subproc, const mi_page_t* page) {
  if (page==NULL) return false;
  mi_theap_t* theap = page->theap;
  return (theap != NULL && theap == subproc->theap_meta);
}


/* -----------------------------------------------------------
  Thread local data
----------------------------------------------------------- */

static mi_tld_t* mi_tld_init(mi_tld_t* tld, size_t tseq, mi_subproc_t* subproc) {
  tld->subproc = subproc;
  tld->theaps = NULL;
  mi_lock_init(&tld->theaps_lock);
  if (tld->thread_id == MI_THREADID_DETACHED) {
    tld->numa_node = -1;
  }
  else {
    tld->numa_node = _mi_os_numa_node();
    tld->thread_id = _mi_prim_thread_id();
    tld->is_in_threadpool = _mi_prim_thread_is_in_threadpool();
    tld->thread_seq = tseq;
    mi_atomic_increment_relaxed(&tld->subproc->thread_count);
  }
  return tld;
}

// Allocate fresh tld
static mi_tld_t* mi_tld_create(mi_subproc_t* subproc) {
  // allocate tld meta-data
  // note: we need to be careful to not access the tld from `_mi_meta_zalloc`
  // (and in turn from `_mi_arena_alloc_aligned` and `_mi_os_alloc_aligned`).
  mi_assert_internal(subproc->theap_meta != NULL); // should be initialized on the main thread before other threads allocate
  const size_t tseq = mi_atomic_increment_relaxed(&subproc->thread_total_count);

  mi_memid_t memid;
  mi_tld_t* tld;
  if (subproc == &mi_process_subproc_main && tseq==0 /* first tld */) {
    tld = &mi_process_tld_main;
    memid = _mi_memid_create_static(tld,sizeof(*tld));
  }
  else {
    tld  = (mi_tld_t*)_mi_meta_zalloc(subproc, sizeof(mi_tld_t), &memid);
  }
  if (tld==NULL) {
    _mi_error_message(ENOMEM, "unable to allocate memory for thread local data\n");
    return NULL;
  }
  tld->memid = memid;  
  return mi_tld_init(tld,tseq,subproc);
}

#define MI_TLD_INVALID      ((mi_tld_t*)1)
#define MI_THREADID_INVALID ((mi_threadid_t)(~0))

mi_decl_noinline static void mi_tld_free(mi_tld_t* tld) {
  if (tld==NULL || tld==MI_TLD_INVALID) return;
  mi_atomic_decrement_relaxed(&tld->subproc->thread_count);
  tld->thread_id = MI_THREADID_INVALID;              // note: not 0 as that would re-initialize tld_main
                                                     // we also need to set an invalid tid for tld_main as sometimes the same thread-id
                                                     // is reused by the OS after a thread has terminated. (see issue #1287)
  mi_lock_done(&tld->theaps_lock);
  _mi_meta_free(tld->subproc, tld, tld->memid);  // note: safe for static tld_main  
}


mi_subproc_t* _mi_subproc_main(void) {
  return &mi_process_subproc_main;
}

mi_subproc_t* _mi_subproc(void) {
  // should work without doing initialization (as it may be called from `_mi_tld -> mi_tld_alloc ... -> os_alloc -> _mi_subproc()`
  // todo: this will still fail on OS systems where the first access to a thread-local causes allocation.
  //       on such systems we can check for this with the _mi_prim_get_default_theap as those are protected (by being
  //       stored in a TLS slot for example)
  mi_theap_t* theap = _mi_theap_default();
  if (theap == NULL || theap->tld == NULL) {  // see issue #1289
    return _mi_subproc_main();
  }
  else {
    return theap->tld->subproc;  // avoid using thread local storage (`thread_tld`)
  }
}

mi_heap_t* _mi_subproc_heap_main(mi_subproc_t* subproc) {
  mi_heap_t* heap = mi_atomic_load_ptr_acquire(mi_heap_t,&subproc->heap_main);
  if mi_likely(heap!=NULL) {
    return heap;
  }
  else if (subproc==_mi_subproc_main()) {
    mi_heap_main_init();
    mi_assert_internal(mi_atomic_load_ptr_acquire(mi_heap_t,&subproc->heap_main) != NULL);
    return mi_atomic_load_ptr_acquire(mi_heap_t,&subproc->heap_main);
  }
  else {
    mi_assert_internal(false);
    return &mi_process_heap_main;
  }
}

mi_heap_t* mi_heap_main(void) {
  return _mi_subproc_heap_main(_mi_subproc()); // don't use mi_theap_main_init_get() so this call works during process_init
}

bool _mi_is_process_heap_main(const mi_heap_t* heap) {
  return (heap == NULL || heap == &mi_process_heap_main);
}

bool _mi_is_theap_main(const mi_theap_t* theap) {
  return (mi_theap_is_initialized(theap) && _mi_is_heap_main(_mi_theap_heap(theap)));
}

mi_page_t* _mi_page_empty_get(void) {
  return (mi_page_t*)&mi_page_empty;
}


/* -----------------------------------------------------------
  Sub process
----------------------------------------------------------- */


mi_subproc_t* _mi_subproc_from_id(mi_subproc_id_t subproc_id) {
  return (mi_subproc_t*)(subproc_id._mi_subproc_id);
}

mi_subproc_id_t _mi_subproc_to_id(mi_subproc_t* subproc) {
  mi_subproc_id_t id = { subproc };
  return id;
}

mi_subproc_id_t mi_subproc_main(void) {
  return _mi_subproc_to_id(_mi_subproc_main());
}

mi_subproc_id_t mi_subproc_current(void) {
  return _mi_subproc_to_id(_mi_subproc());
}

static mi_subproc_t* mi_subproc_init(mi_subproc_t* subproc, mi_subproc_t* parent) {
  static _Atomic(size_t) subproc_total_count;
  subproc->parent = parent;
  subproc->subproc_seq = mi_atomic_increment_relaxed(&subproc_total_count) + 1;
  mi_stats_header_init(&subproc->stats);
  mi_lock_init(&subproc->arena_reserve_lock);
  mi_lock_init(&subproc->heaps_lock);
  mi_lock_init(&subproc->theap_meta_lock);
  mi_lock(&mi_subprocs_lock) {
    // push on subproc list
    subproc->next = mi_subprocs;
    if (mi_subprocs!=NULL) { mi_subprocs->prev = subproc; }
    mi_subprocs = subproc;
  }
  return subproc;
}

mi_subproc_id_t mi_subproc_new(void) {
  mi_subproc_t* const parent = _mi_subproc();
  mi_memid_t memid;
  mi_subproc_t* const subproc = (mi_subproc_t*)_mi_meta_zalloc(parent, sizeof(mi_subproc_t), &memid);
  if (subproc == NULL) { return _mi_subproc_to_id(NULL); }
  subproc->memid  = memid;  
  
  mi_memid_t theap_memid;
  mi_theap_t* const theap_meta = (mi_theap_t*)_mi_meta_zalloc(parent, sizeof(mi_theap_t), &theap_memid);
  if (theap_meta==NULL) { 
    _mi_meta_free(parent, subproc, memid); 
    return _mi_subproc_to_id(NULL); 
  }
  theap_meta->memid = memid;
  
  // init subproc
  mi_subproc_init(subproc,parent);
  
  // init main heap
  mi_heap_t* heap_main = _mi_heap_new_for_subproc(subproc,0,true);
  if (heap_main==NULL) {
    _mi_meta_free(parent, theap_meta, theap_meta->memid);
    mi_subproc_destroy(_mi_subproc_to_id(subproc));
    return _mi_subproc_to_id(NULL);
  }
  mi_assert_internal(subproc->heap_main == heap_main);

  // init meta theap
  _mi_theap_init(theap_meta,heap_main,&mi_tld_detached);
  subproc->theap_meta = theap_meta;

  return _mi_subproc_to_id(subproc);
}

// destroy all subproc resources including arena's, heap's etc.
static void mi_subproc_unsafe_destroy(mi_subproc_t* subproc, bool acquire_subprocs_lock)
{
  if (subproc==NULL) return;

  // remove from the subproc list
  mi_lock_maybe(&mi_subprocs_lock, acquire_subprocs_lock) {
    if (subproc->next!=NULL) { subproc->next->prev = subproc->prev;  }
    if (subproc->prev!=NULL) { subproc->prev->next = subproc->next;  }
                        else { mi_assert_internal(mi_subprocs==subproc);  mi_subprocs = subproc->next; }
  }

  // destroy all subproc heaps
  mi_lock(&subproc->heaps_lock) {
    mi_heap_t* heap = subproc->heaps;
    while (heap != NULL) {
      mi_heap_t* next = heap->next;
      if (heap!=subproc->heap_main) { mi_heap_destroy(heap); }
      heap = next;
    }
    mi_assert_internal(subproc->heap_main==NULL || subproc->heaps == subproc->heap_main);
    if (subproc->heap_main!=NULL) {
      _mi_heap_force_destroy(subproc->heap_main);  // no warning if destroying the main heap
    }
  }

  // merge theap stats
  mi_lock(&subproc->theap_meta_lock) {
    if (subproc->theap_meta != NULL) { 
      _mi_stats_merge_into(&subproc->stats, &subproc->theap_meta->stats);
    }
    subproc->theap_meta = NULL;
  }

  // merge stats back into the main subproc?
  if (subproc!=&mi_process_subproc_main) {
    _mi_stats_merge_into(&mi_process_subproc_main.stats, &subproc->stats);
  }

  // remove associated arenas
  _mi_arenas_unsafe_destroy_all(subproc);

  // show stats of the main process (at process end) before releasing the heaps lock
  if (subproc==&mi_process_subproc_main) {
    if (mi_option_is_enabled(mi_option_show_stats) || mi_option_is_enabled(mi_option_verbose)) {
      mi_subproc_stats_print_out(mi_subproc_main(), NULL, NULL);
    }
  }

  // todo: should we refcount subprocesses?
  mi_lock_done(&subproc->arena_reserve_lock);
  mi_lock_done(&subproc->heaps_lock);
  mi_lock_done(&subproc->theap_meta_lock);  
  _mi_meta_free( subproc->parent, subproc, subproc->memid);
  // for the main subproc, also release the global page map
  if (subproc==&mi_process_subproc_main) {
    _mi_page_map_unsafe_destroy();
  }
}

void mi_subproc_destroy(mi_subproc_id_t subproc_id) {
  mi_subproc_t* subproc = _mi_subproc_from_id(subproc_id);
  if (subproc==NULL || subproc==&mi_process_subproc_main) return;
  mi_subproc_unsafe_destroy(subproc, true /* take lock */);
}

static void mi_subprocs_unsafe_destroy_all(void) {
  mi_lock(&mi_subprocs_lock) {
    mi_subproc_t* subproc = mi_subprocs;
    while (subproc!=NULL) {
      mi_subproc_t* next = subproc->next;
      if (subproc!=&mi_process_subproc_main) {
        mi_subproc_unsafe_destroy(subproc, false /* take mi_subprocs lock */);
      }
      subproc = next;
    }
  }
  mi_subproc_unsafe_destroy(&mi_process_subproc_main, true /* take mi_subprocs lock */);
}

static mi_theap_t* mi_thread_init_ex(mi_heap_t* heap_main) mi_attr_noexcept;

void mi_subproc_add_current_thread(mi_subproc_id_t subproc_id) {
  mi_subproc_t* subproc = _mi_subproc_from_id(subproc_id);
  mi_assert_internal(subproc!=NULL);
  if (subproc==NULL) return;
  mi_assert_internal(subproc->heap_main!=NULL);
  if (subproc->heap_main==NULL) return;
  mi_theap_t* theap = _mi_theap_default();
  if (mi_theap_is_initialized(theap)) {
    if (theap->tld!=NULL && theap->tld->subproc != subproc) {
      _mi_warning_message("unable to add thread to the subprocess as it was already in another subprocess (at %p)\n", theap->tld->subproc);
    }
    return;
  }

  // initialize this thread tld & theap
  mi_thread_init_ex(subproc->heap_main);
}


bool mi_subproc_visit_heaps(mi_subproc_id_t subproc_id, mi_heap_visit_fun* visitor, void* arg) {
  mi_subproc_t* subproc = _mi_subproc_from_id(subproc_id);
  if (subproc==NULL) return false;
  bool ok = true;
  mi_lock(&subproc->heaps_lock) {
    for (mi_heap_t* heap = subproc->heaps; heap!=NULL && ok; heap = heap->next) {
      ok = (*visitor)(heap, arg);
    }
  }
  return ok;
}


/* -----------------------------------------------------------
  Thread Init
----------------------------------------------------------- */

#if MI_DEBUG || defined(MI_TLS_RECURSE_GUARD)
static mi_theap_t* mi_heap_check_for_existing_theap(mi_heap_t* heap) {
  const mi_threadid_t tid = _mi_thread_id();
  mi_theap_t* thread_theap = NULL;
  mi_lock(&heap->theaps_lock) {
    for(mi_theap_t* theap = heap->theaps; theap != NULL; theap = theap->hnext ) {
      if (theap->tld->thread_id == tid) {
        thread_theap = theap;
        break;
      }
    }
  }
  return thread_theap;
}
#endif

// Initialize thread
static mi_theap_t* mi_thread_init_ex(mi_heap_t* heap_main) mi_attr_noexcept
{
  // ensure our process has started already
  mi_process_init();

  // if the theap_default is already set we have already initialized
  mi_theap_t* theap = _mi_theap_default();
  if (mi_theap_is_initialized(theap)) return theap;

  // initialize the default theap
  // note: we cannot access thread-locals yet as that can cause (recursive) allocation
  // (on macOS <= 14 for example where the loader allocates thread-local data on demand).
  if (heap_main==NULL) {
    heap_main = mi_heap_main();
    mi_assert_internal(heap_main == &mi_process_heap_main);
  }
  mi_assert_internal(heap_main!=NULL);

  #if MI_DEBUG || defined(MI_TLS_RECURSE_GUARD)
  theap = mi_heap_check_for_existing_theap(heap_main);  // recursion check
  #if !defined(MI_TLS_RECURSE_GUARD)
  mi_assert_internal(theap==NULL);
  #endif
  #else
  theap = NULL;
  #endif
  
  if (theap==NULL) {
    // allocated the tld
    mi_tld_t* tld = mi_tld_create(heap_main->subproc);
    if (tld==NULL) return NULL;    // out-of-memory on tld allocation
    // allocate and initialize the theap for the main heap
    if (tld==&mi_process_tld_main) {
      theap = &mi_process_theap_main;          // initial theap is pre-allocated
      theap->memid = _mi_memid_create_static(theap,sizeof(*theap));      
    }
    else {
      theap = _mi_theap_alloc(heap_main,tld);  // otherwise meta allocate
      if (theap==NULL) { mi_tld_free(tld); return NULL; } // out-of-memory on theap allocation
    }
    _mi_theap_init(theap,heap_main,tld);    
  }
  
  // now initialize the thread
  _mi_theap_default_set(theap);
  // and only then set the heap_theap field as that accesses thread locals
  _mi_heap_theap_set(heap_main, theap);  // todo: can fail!

  mi_assert_internal(mi_theap_is_initialized(theap));
  mi_theap_t* const heap_theap = (heap_main==NULL ? NULL : (mi_theap_t*)_mi_thread_local_get(heap_main->theap));
  mi_assert_internal(heap_main==NULL || heap_theap == theap); MI_UNUSED_RELEASE(heap_theap);
  
  mi_subproc_stat_increase(_mi_theap_subproc(theap), threads, 1);  // or theap stats and wait for merge?
  // _mi_verbose_message("thread init: 0x%zx\n", _mi_thread_id());
  return theap;
}

mi_theap_t* _mi_thread_init(void) {
  return mi_thread_init_ex(NULL);
}

void mi_decl_noinline mi_thread_init(void) mi_attr_noexcept {
  _mi_thread_init();
}



/* -----------------------------------------------------------
  Theaps done
----------------------------------------------------------- */

// Free the thread local theaps
static void mi_thread_theaps_done(mi_tld_t* tld)
{
  // abandon the pages of all theaps in this thread
  mi_lock(&tld->theaps_lock) {
    mi_theap_t* theap = tld->theaps;
    while (theap != NULL) {
      mi_theap_t* next = theap->tnext;
      // never destroy theaps; if a dll is linked statically with mimalloc,
      // there may still be delete/free calls after the mi_fls_done is called. Issue #207
      _mi_theap_collect_abandon(theap);
      mi_assert_internal(theap->page_count==0);
      theap = next;
    }
  }

  // reset the thread local theaps
  // note: do this after abandon as page->heap may be NULL and mi_heap_main should return the heap
  // belonging to the right subprocess
  _mi_theap_default_set((mi_theap_t*)&_mi_theap_empty);
  _mi_theap_cached_set((mi_theap_t*)&_mi_theap_empty);

  // free the theaps of this thread.
  // This can run concurrently with a `mi_heap_free_theaps` and we need to ensure we free theaps atomically.
  // We do this in a loop where we release the theaps_lock at every potential re-iteration to unblock
  // potential concurrent `mi_heap_free_theaps` which tries to remove the theap from our theaps list.
  bool all_freed;
  do {
    all_freed = true;
    mi_lock(&tld->theaps_lock) {
      mi_theap_t* theap = tld->theaps;
      while (theap != NULL) {
        mi_theap_t* next = theap->tnext;
        mi_assert_internal(theap->page_count==0);
        if (!_mi_theap_free(theap, true /* acquire heap->theaps_lock */, false /* dont re-acquire the tld->theaps_lock*/ )) {
          all_freed = false;
        }
        theap = next;
      }
    }
    if (!all_freed) {
      mi_subproc_stat_counter_increase(tld->subproc,heaps_delete_wait,1);
      _mi_prim_thread_yield();
    }
    else {
      mi_assert_internal(tld->theaps==NULL);
    }
  } while (!all_freed);

  mi_assert(_mi_theap_default()==(mi_theap_t*)&_mi_theap_empty); // careful to not re-initialize the default theap during theap_delete
  mi_assert(!mi_theap_is_initialized(_mi_theap_default()));
}


// --------------------------------------------------------
// Try to run `mi_thread_done()` automatically so any memory
// owned by the thread but not yet released can be abandoned
// and re-owned by another thread.
//
// 1. windows dynamic library:
//     call from DllMain on DLL_THREAD_DETACH
// 2. windows static library:
//     use special linker section to call a destructor when the thread is done
// 3. unix, pthreads:
//     use a pthread key to call a destructor when a pthread is done
//
// In the last two cases we also need to call `mi_process_init`
// to set up the thread local keys.
// --------------------------------------------------------

// Set up handlers so `mi_thread_done` is called automatically
static void mi_process_setup_auto_thread_done(void) {
  mi_atomic_do_once {
    _mi_prim_thread_init_auto_done();    
  }
}

void mi_thread_done(void) mi_attr_noexcept {
  _mi_thread_done(NULL);
}

void _mi_thread_done(mi_theap_t* _theap_main)
{
  // NULL can be passed on some platforms
  if (_theap_main==NULL) {
    _theap_main = _mi_theap_default();
  }

  // prevent re-entrancy through theap_done/theap_set_default_direct (issue #699)
  if (!mi_theap_is_initialized(_theap_main)) {
    return;
  }

  // note: we store the tld as we should avoid reading `thread_tld` at this point (to avoid reinitializing the thread local storage)
  mi_tld_t* const tld = _theap_main->tld;

  // release dynamic thread_local's
  _mi_thread_locals_thread_done();

  // adjust stats
  mi_subproc_stat_decrease(tld->subproc, threads, 1);  // todo: or `_theap_main->heap`?

  // check thread-id as on Windows shutdown with FLS the main (exit) thread may call this on thread-local theaps...
  if (tld->thread_id != _mi_prim_thread_id()) return;

  // delete the thread local theaps
  mi_thread_theaps_done(tld);

  // free thread local data
  mi_tld_free(tld);
}


mi_decl_cold mi_decl_noinline mi_theap_t* _mi_theap_empty_get(void) {
  return (mi_theap_t*)&_mi_theap_empty;
}

bool _mi_is_empty_theap(const mi_theap_t* theap) {
  return (theap == &_mi_theap_empty);
}


#if MI_TLS_MODEL_WIN32

// If we can, we use one of the 64 direct TLS slots (but fall back to expansion slots if needed)
// See <https://en.wikipedia.org/wiki/Win32_Thread_Information_Block> for the offsets.
#if MI_SIZE_SIZE==4
#define MI_TLS_DIRECT_FIRST             (0x0E10 / MI_INTPTR_SIZE)
#else
#define MI_TLS_DIRECT_FIRST             (0x1480 / MI_INTPTR_SIZE)
#endif
#define MI_TLS_DIRECT_SLOTS             (64)
#define MI_TLS_EXPANSION_SLOTS          (1024)

// We initially use the last of the expansion slots as the default NULL.
// note: this will fail if the program allocates exactly 1024+64 slots with TlsAlloc 
// before we are initialized :-( (but this seems quite unlikely).
// (todo: another approach could be to use slot 7 (EnvironmentPointer) as the initial slot as that seems to be always NULL)
#define MI_TLS_INITIAL_SLOT             MI_TLS_EXPANSION_SLOT
#define MI_TLS_INITIAL_EXPANSION_SLOT   (MI_TLS_EXPANSION_SLOTS-1)

// in case of errors assign fixed slots (but since we use EFAULT the program should fail anyways)
#define MI_TLS_ERROR_SLOT               (5)   // arbitrary user pointer
#define MI_TLS_ERROR_EXPANSION_SLOT     (7)   // environment pointer (only used for OS/2 emulation)


mi_decl_hidden mi_decl_cache_align size_t _mi_theap_default_slot = MI_TLS_INITIAL_SLOT;
mi_decl_hidden size_t _mi_theap_default_expansion_slot = MI_TLS_INITIAL_EXPANSION_SLOT;
mi_decl_hidden size_t _mi_theap_cached_slot            = MI_TLS_INITIAL_SLOT;
mi_decl_hidden size_t _mi_theap_cached_expansion_slot  = MI_TLS_INITIAL_EXPANSION_SLOT;

static DWORD mi_tls_raw_index_default = TLS_OUT_OF_INDEXES;
static DWORD mi_tls_raw_index_cached  = TLS_OUT_OF_INDEXES;

static bool mi_win_tls_slot_alloc(size_t* slot, size_t* extended, DWORD* raw_index) {
  const DWORD index = TlsAlloc();
  *raw_index = index;
  if (index==TLS_OUT_OF_INDEXES) {
    *extended = MI_TLS_ERROR_EXPANSION_SLOT;
    *slot = MI_TLS_ERROR_SLOT;
    return false;
  }
  else if (index<MI_TLS_DIRECT_SLOTS) {
    *extended = 0;
    *slot = index + MI_TLS_DIRECT_FIRST;
    return true;
  }
  #if !MI_WIN_DIRECT_TLS
  else if (index < MI_TLS_DIRECT_SLOTS + MI_TLS_EXPANSION_SLOTS - 1) { // check maximum number of expansion slots - 1 (as we use the last one as the default)
    *extended = index - MI_TLS_DIRECT_SLOTS;
    *slot = MI_TLS_EXPANSION_SLOT;
    return true;
  }
  #endif
  else {
    // to high an index for us
    _mi_error_message(EFAULT, "returned TLS index was too high (%u)\n", index);
    TlsFree(index);
    *raw_index = TLS_OUT_OF_INDEXES;
    *extended = MI_TLS_ERROR_EXPANSION_SLOT;
    *slot = MI_TLS_ERROR_SLOT;
    return false;
  }
}

static void mi_win_tls_slot_free(DWORD* raw_index) {
  if (*raw_index != TLS_OUT_OF_INDEXES) {
    TlsFree(*raw_index);
    *raw_index = TLS_OUT_OF_INDEXES;
  }
}

static void mi_tls_slots_init(void) {
  mi_atomic_do_once {
    bool ok = mi_win_tls_slot_alloc(&_mi_theap_default_slot, &_mi_theap_default_expansion_slot, &mi_tls_raw_index_default);
    if (ok) {
      ok = mi_win_tls_slot_alloc(&_mi_theap_cached_slot, &_mi_theap_cached_expansion_slot, &mi_tls_raw_index_cached);
    }
    if (!ok) {
      _mi_error_message(EFAULT, "unable to allocate a fast TLS user slot.\n");
    }
  }
}

static void mi_tls_slots_done(void) {
  mi_win_tls_slot_free(&mi_tls_raw_index_default);
  mi_win_tls_slot_free(&mi_tls_raw_index_cached );
}

static void mi_win_tls_slot_set(size_t slot, size_t extended_slot, void* value) {
  mi_assert_internal((slot >= MI_TLS_DIRECT_FIRST && slot < MI_TLS_DIRECT_FIRST + MI_TLS_DIRECT_SLOTS) || slot == MI_TLS_EXPANSION_SLOT);
  if (slot < MI_TLS_DIRECT_FIRST + MI_TLS_DIRECT_SLOTS) {
    mi_prim_tls_slot_set(slot, value);
  }
  else {
    mi_assert_internal(extended_slot < MI_TLS_EXPANSION_SLOTS);
    TlsSetValue((DWORD)(extended_slot + MI_TLS_DIRECT_SLOTS), value);  // use TlsSetValue to initialize the TlsExpansion array if needed
  }
}

#elif MI_TLS_MODEL_PTHREADS

// only for pthreads for now
mi_decl_hidden pthread_key_t _mi_theap_default_key = MI_PTHREAD_KEY_INVALID;
mi_decl_hidden pthread_key_t _mi_theap_cached_key = MI_PTHREAD_KEY_INVALID;

static void mi_theap_cached_key_destroy(void* theapv) {
  mi_theap_t* theap = (mi_theap_t*)theapv;
  if (theap!=NULL) {
    _mi_theap_decref(theap);
  }
}

static void mi_tls_slots_init(void) {
  mi_atomic_do_once {
    _mi_pthread_key_create(&_mi_theap_default_key,NULL,NULL);
    _mi_pthread_key_create(&_mi_theap_cached_key,&mi_theap_cached_key_destroy,NULL);
  }  
}

static void mi_tls_slots_done(void) {
  mi_pthread_key_delete(&_mi_theap_default_key);
  mi_pthread_key_delete(&_mi_theap_cached_key);
}

#elif MI_TLS_MODEL_FIXED 

static void mi_tls_slots_init(void) {
  mi_atomic_do_once {
    mi_theap_t* theap = _mi_theap_default();
    if (theap!=NULL) {
      _mi_error_message(EINVAL,"fixed TLS slot is already in use (slot %d = %p)", MI_TLS_MODEL_FIXED_DEFAULT, theap);
    }
    theap = _mi_theap_cached();
    if (theap!=NULL) {
      _mi_error_message(EINVAL,"fixed TLS slot is already in use (slot %d = %p)", MI_TLS_MODEL_FIXED_CACHED, theap);
    }
  }
}

static void mi_tls_slots_done(void) {
  // nothing
}


#else

static void mi_tls_slots_init(void) {
  // nothing
}

static void mi_tls_slots_done(void) {
  // nothing
}

#endif

void _mi_theap_cached_set(mi_theap_t* theap) {
  mi_theap_t* prev = _mi_theap_cached();
  if (prev==theap) return;
  // set
  mi_tls_slots_init();
  #if MI_TLS_MODEL_LOCAL
    __mi_theap_cached = theap;
  #elif MI_TLS_MODEL_FIXED
    mi_prim_tls_slot_set(MI_TLS_MODEL_FIXED_CACHED, theap);
  #elif MI_TLS_MODEL_WIN32
    mi_win_tls_slot_set(_mi_theap_cached_slot, _mi_theap_cached_expansion_slot, theap);
  #elif MI_TLS_MODEL_PTHREADS
    mi_pthread_key_set(&_mi_theap_cached_key, theap);
  #endif
  // update refcounts (so cached theap memory keeps available until no longer cached)
  _mi_theap_incref(theap);
  _mi_theap_decref(prev);
}

void _mi_theap_default_set(mi_theap_t* theap)  {
  mi_assert_internal(theap != NULL);
  mi_assert_internal(theap->tld != NULL);
  mi_assert_internal(mi_theap_matches_thread(theap));
  mi_tls_slots_init();
  #if MI_TLS_MODEL_LOCAL
    __mi_theap_default = theap;
  #elif MI_TLS_MODEL_FIXED
    mi_prim_tls_slot_set(MI_TLS_MODEL_FIXED_DEFAULT, theap);
  #elif MI_TLS_MODEL_WIN32
    mi_win_tls_slot_set(_mi_theap_default_slot, _mi_theap_default_expansion_slot, theap);
  #elif MI_TLS_MODEL_PTHREADS
    mi_pthread_key_set(&_mi_theap_default_key, theap);
  #endif

  // set theap main if needed
  if (mi_theap_is_initialized(theap)) {
    // ensure the default theap is passed to `_mi_thread_done` as on some platforms we cannot access TLS at thread termination (as it would allocate again)
    _mi_prim_thread_associate_default_theap(theap);
  }
}

void mi_thread_set_in_threadpool(void) mi_attr_noexcept {
  mi_theap_t* theap = mi_theap_get_default();
  theap->tld->is_in_threadpool = true;
}


// --------------------------------------------------------
// Run functions on process init/done, and thread init/done
// --------------------------------------------------------
static bool os_preloading = true;    // true until this module is initialized

// Returns true if this module has not been initialized; Don't use C runtime routines until it returns false.
bool mi_decl_noinline _mi_preloading(void) {
  return os_preloading;
}

// Returns true if mimalloc was redirected
mi_decl_nodiscard bool mi_is_redirected(void) mi_attr_noexcept {
  return _mi_is_redirected();
}

// Called once by the process loader from `src/prim/prim.c`
void _mi_auto_process_init(void) {
  // mi_heap_main_init();
  // #if defined(__APPLE__) || defined(MI_TLS_RECURSE_GUARD)
  // volatile mi_theap_t* dummy = __mi_theap_default; // access TLS to allocate it before setting tls_initialized to true;
  // if (dummy == NULL) return;                       // use dummy or otherwise the access may get optimized away (issue #697)
  // #endif

  os_preloading = false;
  // mi_assert_internal(_mi_is_main_thread());

  mi_process_init();
  mi_tls_slots_init();
  mi_process_setup_auto_thread_done();
  _mi_thread_locals_init();

  _mi_options_post_init();  // now we can print to stderr
  if (_mi_is_redirected()) _mi_verbose_message("malloc is redirected.\n");

  // show message from the redirector (if present)
  const char* msg = NULL;
  _mi_allocator_init(&msg);
  if (msg != NULL && (mi_option_is_enabled(mi_option_verbose) || mi_option_is_enabled(mi_option_show_errors))) {
    _mi_fputs(NULL,NULL,NULL,msg);
  }

  // reseed random
  // _mi_random_reinit_if_weak(&mi_process_theap_main.random);
}

// CPU features
mi_decl_cache_align size_t _mi_cpu_movsb_max = 0;  // for size <= max, rep movsb is fast
mi_decl_cache_align size_t _mi_cpu_stosb_max = 0;  // for size <= max, rep stosb is fast
mi_decl_cache_align bool _mi_cpu_has_popcnt = false;

#if (MI_ARCH_X64 || MI_ARCH_X86)
#if defined(__GNUC__)
// #include <cpuid.h>
static bool mi_cpuid(uint32_t* regs4, uint32_t level, uint32_t sublevel) {
  // note: use explicit assembly instead of __get_cpuid as we need the sublevel (in ecx)
  // (on Ubuntu 22 with WSL the __get_cpuid does not clear ecx for level 7 which is incorrect).
  uint32_t eax, ebx, ecx, edx;
  __asm __volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(level), "c"(sublevel) : );
  regs4[0] = eax;
  regs4[1] = ebx;
  regs4[2] = ecx;
  regs4[3] = edx;
  return true;
}

#elif defined(_MSC_VER)
static bool mi_cpuid(uint32_t* regs4, uint32_t level, uint32_t sublevel) {
  __cpuidex((int32_t*)regs4, (int32_t)level, (int32_t)sublevel);
  return true;
}
#else
static bool mi_cpuid(uint32_t* regs4, uint32_t level, uint32_t sublevel) {
  MI_UNUSED(regs4); MI_UNUSED(level); MI_UNUSED(sublevel);
  return false;
}
#endif

static void mi_detect_cpu_features(void) {
  // FSRM for fast short rep movsb support (AMD Zen3+ (~2020) or Intel Ice Lake+ (~2017))
  // EMRS for fast enhanced rep movsb/stosb support (not used at the moment, memcpy always seems faster?)
  // FSRS for fast short rep stosb
  bool amd = false;
  bool fsrm = false;
  // bool erms = false;
  bool fsrs = false;
  uint32_t cpu_info[4];
  if (mi_cpuid(cpu_info, 0, 0)) {
    amd = (cpu_info[2]==0x444d4163); // (Auth enti cAMD)
  }
  if (mi_cpuid(cpu_info, 7, 0)) {
    fsrm = ((cpu_info[3] & (1 << 4)) != 0); // bit 4 of EDX : see <https://en.wikipedia.org/wiki/CPUID#EAX=7,_ECX=0:_Extended_Features>
    // erms = ((cpu_info[1] & (1 << 9)) != 0); // bit 9 of EBX : see <https://en.wikipedia.org/wiki/CPUID#EAX=7,_ECX=0:_Extended_Features>
  }
  if (mi_cpuid(cpu_info, 7, 1)) {
    fsrs = ((cpu_info[1] & (1 << 11)) != 0); // bit 11 of EBX: see <https://en.wikipedia.org/wiki/CPUID#EAX=7,_ECX=1:_Extended_Features>
  }
  if (mi_cpuid(cpu_info, 1, 0)) {
    _mi_cpu_has_popcnt = ((cpu_info[2] & (1 << 23)) != 0); // bit 23 of ECX : see <https://en.wikipedia.org/wiki/CPUID#EAX=1:_Processor_Info_and_Feature_Bits>
  }

  if (fsrm) {
    _mi_cpu_movsb_max = 127;
  }
  if (fsrs || (amd && fsrm)) {  // fsrm on amd implies fsrs, see: https://marc.info/?l=git-commits-head&m=168186277717803
    _mi_cpu_stosb_max = 127;
  }
}

#else
static void mi_detect_cpu_features(void) {
  #if MI_ARCH_ARM64
  _mi_cpu_has_popcnt = true;
  #endif
}
#endif


// Initialize the process; called by thread_init or the process loader
static void mi_process_init_once(void) mi_attr_noexcept {
  _mi_verbose_message("process init: 0x%zx\n", _mi_thread_id());

  mi_detect_cpu_features();
  _mi_options_init();
  _mi_stats_init();    // start timer
  _mi_os_init();
  // the following can potentially allocate (on freeBSD for pthread keys)
  // todo: do 2-phase so we can use stats at first, then later init the keys?
  mi_heap_main_init(); // before page_map_init so stats are working
  _mi_page_map_init(); // todo: this could fail.. should we abort in that case?
  mi_thread_init();
  _mi_process_is_initialized = true;

  #if defined(_WIN32) && defined(MI_WIN_USE_FLS)
  // On windows, when building as a static lib the FLS cleanup happens to early for the main thread.
  // To avoid this, set the FLS value for the main thread to NULL so the fls cleanup
  // will not call _mi_thread_done on the (still executing) main thread. See issue #508.
  _mi_prim_thread_associate_default_theap(NULL);
  #endif

  // mi_stats_reset();  // only call stat reset *after* thread init (or the theap tld == NULL)
  mi_track_init();
  if (mi_option_is_enabled(mi_option_reserve_huge_os_pages)) {
    size_t pages = mi_option_get_clamp(mi_option_reserve_huge_os_pages, 0, 128*1024);
    int reserve_at  = (int)mi_option_get_clamp(mi_option_reserve_huge_os_pages_at, -1, INT_MAX);
    if (reserve_at != -1) {
      mi_reserve_huge_os_pages_at(pages, reserve_at, pages*500);
    } else {
      mi_reserve_huge_os_pages_interleave(pages, 0, pages*500);
    }
  }
  if (mi_option_is_enabled(mi_option_reserve_os_memory)) {
    long ksize = mi_option_get(mi_option_reserve_os_memory);
    if (ksize > 0) {
      mi_reserve_os_memory((size_t)ksize*MI_KiB, true, true);
    }
  }
}

// Initialize the process; called by thread_init or the process loader
void mi_process_init(void) mi_attr_noexcept {
  // #if _MSC_VER < 1920
	// mi_heap_main_init(); // vs2017 can dynamically re-initialize _mi_heap_main
	// #endif
  mi_atomic_do_once {
    mi_process_init_once();
  }
}


// Called when the process is done
static void mi_process_done_once(void) {
  // only shutdown if we were initialized
  if (!_mi_process_is_initialized) return;
  // ensure we are called once
  static bool process_done = false;
  if (process_done) return;
  process_done = true;

  // free dynamic thread locals (if used at all)
  _mi_thread_locals_done();

  // release any thread specific resources and ensure _mi_thread_done is called on all but the main thread
  _mi_prim_thread_done_auto_done();

  #ifndef MI_SKIP_COLLECT_ON_EXIT
    #if (MI_DEBUG || !defined(MI_SHARED_LIB))
    // free all memory if possible on process exit. This is not needed for a stand-alone process
    // but should be done if mimalloc is statically linked into another shared library which
    // is repeatedly loaded/unloaded, see issue #281.
    mi_theap_collect(_mi_theap_default(), true /* force */);
    #endif
  #endif

  // done with tracking tools
  mi_track_done();

  // Forcefully release all retained memory; this can be dangerous in general if overriding regular malloc/free
  // since after process_done there might still be other code running that calls `free` (like at_exit routines,
  // or C-runtime termination code.
  if (mi_option_is_enabled(mi_option_destroy_on_exit)) {
    mi_subprocs_unsafe_destroy_all(); // destroys all mi_subprocs, arenas, and the page_map!
  }
  else if (mi_process_subproc_main.heap_main != NULL) {
    if (mi_option_is_enabled(mi_option_show_stats) || mi_option_is_enabled(mi_option_verbose)) {
      _mi_theap_merge_stats(mi_process_subproc_main.theap_meta);
      mi_heap_stats_merge_to_subproc(mi_process_subproc_main.heap_main);    
      mi_subproc_stats_print_out(mi_subproc_main(), NULL, NULL);
    } 
  }
  
  mi_lock_done(&mi_subprocs_lock);  
  mi_tls_slots_done();
  _mi_allocator_done();
  _mi_verbose_message("process done\n"); // : 0x%zx\n", mi_process_tld_main.thread_id);
  os_preloading = true; // don't call the C runtime anymore
}


// Called when the process is done (cdecl as it is used with `at_exit` on some platforms)
void mi_cdecl mi_process_done(void) mi_attr_noexcept {
  mi_atomic_do_once {
    mi_process_done_once();
  }
}

void mi_cdecl _mi_auto_process_done(void) mi_attr_noexcept {
  if (_mi_option_get_fast(mi_option_destroy_on_exit)>1) return;
  mi_process_done();
}
