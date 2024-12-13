/* ----------------------------------------------------------------------------
Copyright (c) 2019-2024, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
  We have a special "mini" allocator just for allocation of meta-data like
  the heap (`mi_heap_t`) or thread-local data (`mi_tld_t`).

  We reuse the bitmap of the arena's for allocation of 64b blocks inside
  an arena slice (64KiB).
  We always ensure that meta data is zero'd (we zero on `free`)
-----------------------------------------------------------------------------*/

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "bitmap.h"

/* -----------------------------------------------------------
  Meta data allocation
----------------------------------------------------------- */

#define MI_META_PAGE_SIZE         MI_ARENA_SLICE_SIZE
#define MI_META_PAGE_ALIGN        MI_ARENA_SLICE_ALIGN

#define MI_META_BLOCK_SIZE        (64)
#define MI_META_BLOCK_ALIGN       MI_META_BLOCK_SIZE
#define MI_META_BLOCKS_PER_PAGE   (MI_ARENA_SLICE_SIZE / MI_META_BLOCK_SIZE)  // 1024
#define MI_META_MAX_SIZE          (MI_BCHUNK_SIZE * MI_META_BLOCK_SIZE)

typedef struct mi_meta_page_s  {
  _Atomic(struct mi_meta_page_s*)  next;    // a linked list of meta-data pages (never released)
  mi_memid_t                       memid;   // provenance of the meta-page memory itself
  mi_bitmap_t                      blocks_free;  // a small bitmap with 1 bit per block.
} mi_meta_page_t;

static mi_decl_cache_align _Atomic(mi_meta_page_t*)  mi_meta_pages = MI_ATOMIC_VAR_INIT(NULL);


#if MI_DEBUG > 1
static mi_meta_page_t* mi_meta_page_of_ptr(void* p, size_t* block_idx) {
  mi_meta_page_t* mpage = (mi_meta_page_t*)mi_align_down_ptr(p,MI_META_PAGE_ALIGN);
  if (block_idx != NULL) {
    *block_idx = ((uint8_t*)p - (uint8_t*)mpage) / MI_META_BLOCK_SIZE;
  }
  return mpage;
}
#endif 

static mi_meta_page_t* mi_meta_page_next( mi_meta_page_t* mpage ) {
  return mi_atomic_load_ptr_acquire(mi_meta_page_t, &mpage->next);
}

static void* mi_meta_block_start( mi_meta_page_t* mpage, size_t block_idx ) {
  mi_assert_internal(_mi_is_aligned(mpage,MI_META_PAGE_ALIGN));
  mi_assert_internal(block_idx < MI_META_BLOCKS_PER_PAGE);
  void* p = ((uint8_t*)mpage + (block_idx * MI_META_BLOCK_SIZE));
  mi_assert_internal(mpage == mi_meta_page_of_ptr(p,NULL));
  return p;
}

// allocate a fresh meta page and add it to the global list.
static mi_meta_page_t* mi_meta_page_zalloc(void) {
  // allocate a fresh arena slice
  mi_memid_t memid;
  mi_meta_page_t* mpage = (mi_meta_page_t*)_mi_arena_alloc_aligned(MI_ARENA_SLICE_SIZE, MI_ARENA_SLICE_ALIGN, 0,
                                                                   true /* commit*/, true /* allow large */,
                                                                   _mi_arena_id_none(), 0 /* tseq */, &memid );
  if (mpage == NULL) return NULL;
  mi_assert_internal(_mi_is_aligned(mpage,MI_META_PAGE_ALIGN));
  if (!memid.initially_zero) {
    _mi_memzero_aligned(mpage, MI_ARENA_SLICE_SIZE);
  }

  // initialize the page
  mpage->memid = memid;
  mi_bitmap_init(&mpage->blocks_free, MI_META_BLOCKS_PER_PAGE, true /* already_zero */);
  const size_t mpage_size  = offsetof(mi_meta_page_t,blocks_free) + mi_bitmap_size(MI_META_BLOCKS_PER_PAGE, NULL);
  const size_t info_blocks = _mi_divide_up(mpage_size,MI_META_BLOCK_SIZE);
  mi_assert_internal(info_blocks < MI_META_BLOCKS_PER_PAGE);
  mi_bitmap_unsafe_setN(&mpage->blocks_free, info_blocks, MI_META_BLOCKS_PER_PAGE - info_blocks);

  // push atomically in front of the meta page list
  // (note: there is no ABA issue since we never free meta-pages)
  mi_meta_page_t* old = mi_atomic_load_ptr_acquire(mi_meta_page_t,&mi_meta_pages);
  do {
    mi_atomic_store_ptr_release(mi_meta_page_t, &mpage->next, old);
  } while(!mi_atomic_cas_ptr_weak_acq_rel(mi_meta_page_t,&mi_meta_pages,&old,mpage));
  return mpage;
}


// allocate meta-data
void* _mi_meta_zalloc( size_t size, mi_memid_t* pmemid )
{
  mi_assert_internal(pmemid != NULL);
  size = _mi_align_up(size,MI_META_BLOCK_SIZE);
  if (size == 0 || size > MI_META_MAX_SIZE) return NULL;
  const size_t block_count = _mi_divide_up(size,MI_META_BLOCK_SIZE);
  mi_assert_internal(block_count > 0 && block_count < MI_BCHUNK_BITS);
  mi_meta_page_t* mpage0 = mi_atomic_load_ptr_acquire(mi_meta_page_t,&mi_meta_pages);
  mi_meta_page_t* mpage = mpage0;
  while (mpage != NULL) {
    size_t block_idx;
    if (mi_bitmap_try_find_and_clearN(&mpage->blocks_free, block_count, 0, &block_idx)) {
      // found and claimed `block_count` blocks
      *pmemid = _mi_memid_create_meta(mpage, block_idx, block_count);
      return mi_meta_block_start(mpage,block_idx);
    }
    else {
      mpage = mi_meta_page_next(mpage);
    }
  }
  // failed to find space in existing pages
  if (mi_atomic_load_ptr_acquire(mi_meta_page_t,&mi_meta_pages) != mpage0) {
    // the page list was updated by another thread in the meantime, retry
    return _mi_meta_zalloc(size,pmemid);
  }
  // otherwise, allocate a fresh metapage and try once more
  mpage = mi_meta_page_zalloc();
  if (mpage != NULL) {
    size_t block_idx;
    if (mi_bitmap_try_find_and_clearN(&mpage->blocks_free, block_count, 0, &block_idx)) {
      // found and claimed `block_count` blocks
      *pmemid = _mi_memid_create_meta(mpage, block_idx, block_count);
      return mi_meta_block_start(mpage,block_idx);
    }
  }
  // if all this failed, allocate from the OS
  return _mi_os_alloc(size, pmemid);
}

// free meta-data
void _mi_meta_free(void* p, size_t size, mi_memid_t memid) {
  if (p==NULL) return;
  if (memid.memkind == MI_MEM_META) {
    mi_assert_internal(_mi_divide_up(size, MI_META_BLOCK_SIZE) == memid.mem.meta.block_count);
    const size_t block_count = memid.mem.meta.block_count;
    const size_t block_idx   = memid.mem.meta.block_index;
    mi_meta_page_t* mpage = (mi_meta_page_t*)memid.mem.meta.meta_page; 
    mi_assert_internal(mi_meta_page_of_ptr(p,NULL) == mpage);
    mi_assert_internal(block_idx + block_count < MI_META_BLOCKS_PER_PAGE);
    mi_assert_internal(mi_bitmap_is_clearN(&mpage->blocks_free, block_idx, block_count));
    // we zero on free (and on the initial page allocation) so we don't need a "dirty" map
    _mi_memzero_aligned(mi_meta_block_start(mpage, block_idx), block_count*MI_META_BLOCK_SIZE);
    mi_bitmap_setN(&mpage->blocks_free, block_idx, block_count,NULL);
  }
  else if (mi_memid_is_os(memid)) {
    _mi_os_free(p, size, memid);    
  }
  else {
    mi_assert_internal(mi_memid_needs_no_free(memid));
  }
}

bool _mi_meta_is_meta_page(void* p) 
{
  mi_meta_page_t* mpage0 = mi_atomic_load_ptr_acquire(mi_meta_page_t, &mi_meta_pages);
  mi_meta_page_t* mpage = mpage0;
  while (mpage != NULL) {
    if ((void*)mpage == p) return true;
    mpage = mi_meta_page_next(mpage);    
  }
  return false;
}