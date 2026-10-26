/* ----------------------------------------------------------------------------
Copyright (c) 2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_MEMTAG_H
#define MI_MEMTAG_H

#include "decl.h"
#include "bits.h"

/* ----------------------------------------------------------------------------
Memory tagging (ARM MTE)

Every 16-byte granule of tagged memory has a 4-bit tag, and each access must use a
pointer with a matching tag in the top bits (56-59). Blocks in tagged pages get a
random tag where blocks at even and odd indices use disjoint tag sets, such that
adjacent blocks always have different tags. Tag 0 is never used for blocks and is
reserved for meta data and free arena memory. Tag 15 is reserved as well.

Memory tagging is only used when built with `MI_MTE=1` and when it is enabled
at runtime (see `mi_memtag_is_enabled`); otherwise all operations are no-ops and
the hardware instructions are never executed (as these trap on CPU's without MTE).
-----------------------------------------------------------------------------*/

#define MI_MEMTAG_GRANULE_SIZE    (16)
#define MI_MEMTAG_META            (0)         // tag used for meta data (and free arena memory)
#define MI_MEMTAG_LINE_SIZE       (64)        // cache line size tagged by `dc gva` (checked at initialization)
#define MI_MEMTAG_ZERO_MAX        (1024)      // blocks up to this size are zeroed on free (larger ones are only retagged)

// Tag exclusion masks (bit `i` set excludes tag `i`); blocks never use tag 0 or 15.
#define MI_MEMTAG_EXCLUDE         (0x8001)
#define MI_MEMTAG_EXCLUDE_EVEN    (MI_MEMTAG_EXCLUDE | 0x7F00)   // even blocks use tags 1 to 7
#define MI_MEMTAG_EXCLUDE_ODD     (MI_MEMTAG_EXCLUDE | 0x00FE)   // odd blocks use tags 8 to 14
#define MI_MEMTAG_ODD_BIT         (MI_AU(8) << MI_PTR_TAG_SHIFT)  // tag bit that is set for odd blocks

// Set once at process initialization (in `prim/os.c`)
extern mi_decl_hidden bool   _mi_memtag_enabled;    // is memory tagging used?
extern mi_decl_hidden size_t _mi_memtag_max_size;   // maximal block size that is tagged

static inline bool mi_memtag_is_enabled(void) {
  #if MI_MTE
  return _mi_memtag_enabled;
  #else
  return false;
  #endif
}

// Should blocks of size `bsize` in memory that supports tagging (`memid.is_memtag`) be tagged?
static inline bool mi_memtag_use_for(bool is_memtag, size_t bsize) {
  return (mi_memtag_is_enabled() && is_memtag && bsize <= _mi_memtag_max_size && (bsize % MI_MEMTAG_GRANULE_SIZE) == 0);
}


/* ----------------------------------------------------------------------------
  Pointer tags
-----------------------------------------------------------------------------*/

static inline size_t mi_ptr_tag(const void* p) {
  return (size_t)((mi_addr_tagged(p) & MI_PTR_TAG_MASK) >> MI_PTR_TAG_SHIFT);
}

static inline void* mi_ptr_with_tag(const void* p, size_t tag) {
  return mi_ptr_with_addr_tagged(p, mi_addr(p) | ((MI_AU(tag) << MI_PTR_TAG_SHIFT) & MI_PTR_TAG_MASK));
}

// Pointer to meta data (or free arena memory) derived from a (possibly tagged) pointer `p`
static inline void* mi_ptr_meta(const void* p) {
  #if MI_MTE
  return mi_ptr_with_tag(p, MI_MEMTAG_META);
  #else
  return (void*)p;
  #endif
}


/* ----------------------------------------------------------------------------
  Memory tag instructions (only valid to call if `mi_memtag_is_enabled()`).
  We use inline assembly so we do not need to compile with `-march=armv8.5-a+memtag`;
  this way the code is not changed otherwise and still runs on CPU's without MTE.
-----------------------------------------------------------------------------*/
#if MI_MTE
#define MI_MEMTAG_ASM   ".arch_extension memtag\n\t"

// Return `p` with a random tag that is not in the `exclude` mask (`irg`)
static inline void* mi_memtag_random(void* p, size_t exclude) {
  void* q;
  __asm__ __volatile__ (MI_MEMTAG_ASM "irg %0, %1, %2" : "=r"(q) : "r"(p), "r"(exclude));
  return q;
}

// Return `p` with the tag of the memory granule at `p` (`ldg`)
static inline void* mi_memtag_load(void* p) {
  void* q = p;
  __asm__ __volatile__ (MI_MEMTAG_ASM "ldg %0, [%1]" : "+r"(q) : "r"(p) : "memory");
  return q;
}

// Size of the cache line tagged by `dc gva` (as given by `dczid_el0`), or 0 if not permitted
static inline size_t mi_memtag_line_size(void) {
  uint64_t dczid;
  __asm__ ("mrs %0, dczid_el0" : "=r"(dczid));
  return ((dczid & 0x10) != 0 ? 0 : (MI_ZU(4) << (dczid & 0x0F)));
}

// Set the tag of the granule at `p` (or two granules, or the cache line) to the tag of `p`, and zero it if `zero` is set
static inline void mi_memtag_store_granule(uint8_t* p, bool zero) {
  if (zero) { __asm__ __volatile__ (MI_MEMTAG_ASM "stzg %0, [%0]" : : "r"(p) : "memory"); }
       else { __asm__ __volatile__ (MI_MEMTAG_ASM "stg %0, [%0]"  : : "r"(p) : "memory"); }
}
static inline void mi_memtag_store_granule2(uint8_t* p, bool zero) {
  if (zero) { __asm__ __volatile__ (MI_MEMTAG_ASM "stz2g %0, [%0]" : : "r"(p) : "memory"); }
       else { __asm__ __volatile__ (MI_MEMTAG_ASM "st2g %0, [%0]"  : : "r"(p) : "memory"); }
}
static inline void mi_memtag_store_line(uint8_t* p, bool zero) {
  if (zero) { __asm__ __volatile__ (MI_MEMTAG_ASM "dc gzva, %0" : : "r"(p) : "memory"); }
       else { __asm__ __volatile__ (MI_MEMTAG_ASM "dc gva, %0"  : : "r"(p) : "memory"); }
}

// Set the memory tags of `[p,p+size)` to the tag of `p`, and zero it if `zero` is set.
// Small ranges are tagged per granule; for larger ranges only the (unaligned) edges are tagged per granule
// while the aligned middle is tagged per cache line (which is much faster).
static inline void mi_memtag_store_ex(void* p, size_t size, bool zero) {
  mi_assert_internal(((uintptr_t)p % MI_MEMTAG_GRANULE_SIZE) == 0 && (size % MI_MEMTAG_GRANULE_SIZE) == 0 && size > 0);
  uint8_t* q = (uint8_t*)p;
  uint8_t* const end = q + size;
  if (size <= 2*MI_MEMTAG_LINE_SIZE) {
    if ((size % (2*MI_MEMTAG_GRANULE_SIZE)) != 0) { mi_memtag_store_granule(q, zero); q += MI_MEMTAG_GRANULE_SIZE; }
    for (; q < end; q += 2*MI_MEMTAG_GRANULE_SIZE) { mi_memtag_store_granule2(q, zero); }
  }
  else {
    mi_memtag_store_granule(q, zero);
    mi_memtag_store_granule(end - MI_MEMTAG_GRANULE_SIZE, zero);
    mi_memtag_store_granule2((uint8_t*)(((uintptr_t)q + 31) & ~(uintptr_t)31), zero);
    mi_memtag_store_granule2((uint8_t*)((uintptr_t)end & ~(uintptr_t)31) - 32, zero);
    uint8_t* const line_end = (uint8_t*)((uintptr_t)end & ~(uintptr_t)(MI_MEMTAG_LINE_SIZE-1));
    for (q = (uint8_t*)(((uintptr_t)q + MI_MEMTAG_LINE_SIZE-1) & ~(uintptr_t)(MI_MEMTAG_LINE_SIZE-1)); q < line_end; q += MI_MEMTAG_LINE_SIZE) {
      mi_memtag_store_line(q, zero);
    }
  }
}

// Set the memory tags of `[p,p+size)` to the tag of `p`
static inline void mi_memtag_store(void* p, size_t size) {
  mi_memtag_store_ex(p, size, false);
}

// Zero `[p,p+size)` and set its memory tags to the tag of `p`
static inline void mi_memtag_store_zero(void* p, size_t size) {
  mi_memtag_store_ex(p, size, true);
}

// Return `p` with a fresh random tag of the same class (odd or even) that differs from its current tag (`gmi`, `irg`)
static inline void* mi_memtag_random_next(void* p) {
  const size_t class_exclude = ((mi_addr_tagged(p) & MI_MEMTAG_ODD_BIT) == 0 ? MI_MEMTAG_EXCLUDE_EVEN : MI_MEMTAG_EXCLUDE_ODD);
  size_t exclude;
  __asm__ (MI_MEMTAG_ASM "gmi %0, %1, %2" : "=r"(exclude) : "r"(p), "r"(class_exclude));
  return mi_memtag_random(p, exclude);
}

#else
static inline size_t mi_memtag_line_size(void)                 { return 0; }
static inline void* mi_memtag_random(void* p, size_t exclude)  { MI_UNUSED(exclude); return p; }
static inline void* mi_memtag_load(void* p)                    { return p; }
static inline void  mi_memtag_store(void* p, size_t size)      { MI_UNUSED(p); MI_UNUSED(size); }
static inline void  mi_memtag_store_zero(void* p, size_t size) { MI_UNUSED(p); MI_UNUSED(size); }
static inline void* mi_memtag_random_next(void* p)             { return p; }
#endif


/* ----------------------------------------------------------------------------
  Tagging blocks
-----------------------------------------------------------------------------*/

// Tag `count` fresh blocks of size `bsize` at `start` where the first block has index `idx` in its page.
static inline void mi_memtag_blocks_init(void* start, size_t bsize, size_t idx, size_t count) {
  uint8_t* p = (uint8_t*)start;
  for (size_t i = idx; i < idx + count; i++, p += bsize) {
    void* const tp = mi_memtag_random(p, ((i&1)==0 ? MI_MEMTAG_EXCLUDE_EVEN : MI_MEMTAG_EXCLUDE_ODD));
    mi_memtag_store(tp, bsize);
  }
}

// Retag a block on free with a fresh tag (of the same class) and zero it if it is small; returns the block with its new tag.
// The first read uses the current tag of `block` so a stale pointer (e.g. a double free) faults.
static inline void* mi_memtag_block_free(void* block, size_t bsize) {
  #if MI_MTE
  (void)(*(volatile uint8_t*)block);
  #endif
  void* const tblock = mi_memtag_random_next(block);
  if (bsize <= MI_MEMTAG_ZERO_MAX) { mi_memtag_store_zero(tblock, bsize); }
                              else { mi_memtag_store(tblock, bsize); }
  return tblock;
}

// Reset the tags of `[p,p+size)` to the meta data tag
static inline void mi_memtag_clear(void* p, size_t size) {
  mi_memtag_store(mi_ptr_meta(p), size);
}

#endif // MI_MEMTAG_H
