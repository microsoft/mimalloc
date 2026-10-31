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
#include "arch.h"

/* ----------------------------------------------------------------------------
Memory tagging
Currently supports ARM MTE but we should be able to support future memory tagging
extensions as well (like x86 ChkTag and LAM)

Every 16-byte granule of tagged memory has a 4-bit tag, and each access must use a
pointer with a matching tag in the top bits (56-59 on ARM, see `MI_PTR_MEMTAG_SHIFT`).
Blocks in tagged pages get a random tag where blocks at even and odd indices use
disjoint tag sets (tags 1 to 7, and 8 to 15), such that adjacent blocks always have
different tags. Tag 0 is never used for blocks and is reserved for free arena memory
and untagged meta data. Most meta data (like the page meta data, the page map, and the
process meta data) gets a random tag as well (see `mi_memtag_meta_init`).

Memory tagging is only used when built with `MI_MTE=1` and when it is enabled
at runtime (see `mi_memtag_is_enabled`); otherwise all operations are no-ops and
the hardware instructions are never executed (as these trap on CPU's without MTE).
-----------------------------------------------------------------------------*/

#define MI_MEMTAG_GRANULE_SIZE    (16)        // memory is tagged per granule
#define MI_MEMTAG_GRANULE_MAX     (128)       // ranges up to this size are always tagged per granule
#define MI_MEMTAG_META            (0)         // tag used for meta data (and free arena memory)
#define MI_MEMTAG_LINE_MIN        (32)        // supported range of the cache line size tagged by `dc gva` (checked at initialization)
#define MI_MEMTAG_LINE_MAX        (2048)
#define MI_MEMTAG_ZERO_MAX        (1024)      // blocks up to this size are zeroed on free (larger ones are only retagged)

// Tag exclusion masks (bit `i` set excludes tag `i`); blocks never use tag 0.
#define MI_MEMTAG_EXCLUDE         (0x0001)
#define MI_MEMTAG_EXCLUDE_EVEN    (MI_MEMTAG_EXCLUDE | 0xFF00)   // even blocks use tags 1 to 7
#define MI_MEMTAG_EXCLUDE_ODD     (MI_MEMTAG_EXCLUDE | 0x00FE)   // odd blocks use tags 8 to 15
#define MI_MEMTAG_ODD_BIT         (8)   // logical tag bit that is set for odd blocks

#if (MI_CACHE_LINE_SIZE % MI_MEMTAG_GRANULE_SIZE) != 0
#error "`mi_decl_mte_align` must align on a memory tag granule"
#endif

// Set once at process initialization in the sealed process data (see `prim/os.c:_mi_os_init`)
bool   _mi_os_has_memtag(void);         // is memory tagging used?
size_t _mi_os_memtag_line_size(void);   // cache line size tagged by `dc gva`

// Values of the `mte` option (`MIMALLOC_MTE=off|on|sync|async|preferred`). The tag check mode can only be selected on Linux
// (on macOS it is determined by the OS). With `preferred` the kernel selects the preferred mode of the CPU which can
// also be `asymm` (see `/sys/devices/system/cpu/cpu<N>/mte_tcf_preferred`).
#define MI_MEMTAG_MODE_OFF        (0)
#define MI_MEMTAG_MODE_DEFAULT    (1)   // keep the current tag check mode of the process if set, and use `sync` otherwise
#define MI_MEMTAG_MODE_SYNC       (2)
#define MI_MEMTAG_MODE_ASYNC      (3)
#define MI_MEMTAG_MODE_PREFERRED  (4)

static inline bool mi_memtag_is_enabled(void) {
  #if MI_MTE
  return _mi_os_has_memtag();
  #else
  return false;
  #endif
}

// Should meta data in memory that supports tagging (`memid.is_memtag`) be tagged?
static inline bool mi_memtag_use_for_meta(bool is_memtag) {
  return (mi_memtag_is_enabled() && is_memtag);
}

// Should blocks of size `bsize` in memory that supports tagging (`memid.is_memtag`) be tagged? (singleton blocks are never tagged)
static inline bool mi_memtag_use_for(bool is_memtag, size_t bsize) {
  return (mi_memtag_is_enabled() && is_memtag && (bsize % MI_MEMTAG_GRANULE_SIZE) == 0);
}


/* ----------------------------------------------------------------------------
  Pointer memory tags
-----------------------------------------------------------------------------*/

static inline size_t mi_ptr_memtag(const void* p) {
  #if MI_MTE
  return (size_t)(((uintptr_t)p & MI_PTR_MEMTAG_MASK) >> MI_PTR_MEMTAG_SHIFT);
  #else
  MI_UNUSED(p);
  return 0;
  #endif
}

static inline void* mi_ptr_with_memtag(const void* p, size_t memtag) {
  #if MI_MTE
  return (void*)(((uintptr_t)p & MI_PTR_ADDR_MASK) | (((uintptr_t)memtag << MI_PTR_MEMTAG_SHIFT) & MI_PTR_MEMTAG_MASK));
  #else
  MI_UNUSED(memtag);
  return (void*)p;
  #endif
}

// Pointer to meta data (or free arena memory) derived from a (possibly tagged) pointer `p`
static inline void* mi_ptr_memtag_meta(const void* p) {
  return mi_ptr_with_memtag(p, MI_MEMTAG_META);
}

// Primitive memory tag operations
static inline size_t mi_memtag_cpu_line_size(void);
static inline void  mi_memtag_check(const void* p);
static inline void* mi_memtag_random(void* p, size_t exclude);
static inline void* mi_memtag_load(void* p);
static inline void  mi_memtag_store(void* p, size_t size);
static inline void  mi_memtag_store_zero(void* p, size_t size);
static inline void* mi_memtag_random_next(void* p);
static inline void  mi_memtag_blocks_init(void* start, size_t bsize, size_t idx, size_t count);
static inline void* mi_memtag_block_free(void* block, size_t bsize);
static inline void* mi_memtag_block_uncanonical(void* block, size_t bsize, size_t idx);
static inline void  mi_memtag_clear(void* p, size_t size);
static inline void* mi_memtag_meta_init(void* p, size_t size, size_t idx);


/* ----------------------------------------------------------------------------
  Memory tag instructions (only valid to call if `mi_memtag_is_enabled()`).
  We use inline assembly so we do not need to compile with `-march=armv8.5-a+memtag`;
  this way the code is not changed otherwise and still runs on CPU's without MTE.
-----------------------------------------------------------------------------*/
#if MI_MTE
#if defined(__clang__)
#define MI_MEMTAG_ASM   ".arch_extension memtag\n\t"
#else
// older GNU assemblers (like binutils 2.38) only accept memtag instructions for armv8.5-a or later. Note that
// this stays in effect for the rest of the assembly file which is fine as armv8.5-a is a superset of armv8.1-a.
#define MI_MEMTAG_ASM   ".arch armv8.5-a+memtag\n\t"
#endif

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
// (called once at initialization if the OS supports tagging, see `_mi_os_memtag_line_size`)
static inline size_t mi_memtag_cpu_line_size(void) {
  uint64_t dczid;
  __asm__ ("mrs %0, dczid_el0" : "=r"(dczid));
  return ((dczid & 0x10) != 0 ? 0 : (MI_ZU(4) << (dczid & 0x0F)));
}

// Read from `p` such that it faults if the tag of `p` does not match the memory tag
static inline void mi_memtag_check(const void* p) {
  (void)(*(const volatile uint8_t*)p);
}

// Set the tag of the granule at `p` (or two granules, or the cache line) to the tag of `p`, and zero it if `zero` is set
static inline void __mi_memtag_store_granule(uint8_t* p, bool zero) {
  if (zero) { __asm__ __volatile__ (MI_MEMTAG_ASM "stzg %0, [%0]" : : "r"(p) : "memory"); }
       else { __asm__ __volatile__ (MI_MEMTAG_ASM "stg %0, [%0]"  : : "r"(p) : "memory"); }
}
static inline void __mi_memtag_store_granule2(uint8_t* p, bool zero) {
  if (zero) { __asm__ __volatile__ (MI_MEMTAG_ASM "stz2g %0, [%0]" : : "r"(p) : "memory"); }
       else { __asm__ __volatile__ (MI_MEMTAG_ASM "st2g %0, [%0]"  : : "r"(p) : "memory"); }
}
static inline void __mi_memtag_store_line(uint8_t* p, bool zero) {
  if (zero) { __asm__ __volatile__ (MI_MEMTAG_ASM "dc gzva, %0" : : "r"(p) : "memory"); }
       else { __asm__ __volatile__ (MI_MEMTAG_ASM "dc gva, %0"  : : "r"(p) : "memory"); }
}

// Tag the granules in `[p,end)` (using pairs of granules where possible)
static inline void __mi_memtag_store_granules(uint8_t* p, uint8_t* end, bool zero) {
  if (((end - p) % (2*MI_MEMTAG_GRANULE_SIZE)) != 0) { __mi_memtag_store_granule(p, zero); p += MI_MEMTAG_GRANULE_SIZE; }
  for (; p < end; p += 2*MI_MEMTAG_GRANULE_SIZE) { __mi_memtag_store_granule2(p, zero); }
}

// Set the memory tags of `[p,p+size)` to the tag of `p`, and zero it if `zero` is set.
// Small ranges are tagged per granule; for larger ranges only the (unaligned) edges are tagged per granule
// while the aligned middle is tagged per cache line (which is much faster).
static inline void __mi_memtag_store_ex(void* p, size_t size, bool zero) {
  mi_assert_internal(((uintptr_t)p % MI_MEMTAG_GRANULE_SIZE) == 0 && (size % MI_MEMTAG_GRANULE_SIZE) == 0 && size > 0);
  uint8_t* const start = (uint8_t*)p;
  uint8_t* const end = start + size;
  if (size <= MI_MEMTAG_GRANULE_MAX) {
    __mi_memtag_store_granules(start, end, zero);
  }
  else {
    const uintptr_t line_size = _mi_os_memtag_line_size();
    uint8_t* const line_start = (uint8_t*)(((uintptr_t)start + line_size - 1) & ~(line_size - 1));
    uint8_t* const line_end   = (uint8_t*)((uintptr_t)end & ~(line_size - 1));
    if (line_start >= line_end) {
      __mi_memtag_store_granules(start, end, zero);
    }
    else {
      __mi_memtag_store_granules(start, line_start, zero);
      for (uint8_t* q = line_start; q < line_end; q += line_size) { __mi_memtag_store_line(q, zero); }
      __mi_memtag_store_granules(line_end, end, zero);
    }
  }
}

// Set the memory tags of `[p,p+size)` to the tag of `p`
static inline void mi_memtag_store(void* p, size_t size) {
  __mi_memtag_store_ex(p, size, false);
}

// Zero `[p,p+size)` and set its memory tags to the tag of `p`
static inline void mi_memtag_store_zero(void* p, size_t size) {
  __mi_memtag_store_ex(p, size, true);
}

// Return `p` with a fresh random tag of the same class (odd or even) that differs from its current tag (`gmi`, `irg`)
static inline void* mi_memtag_random_next(void* p) {
  const size_t class_exclude = ((mi_ptr_memtag(p) & MI_MEMTAG_ODD_BIT) == 0 ? MI_MEMTAG_EXCLUDE_EVEN : MI_MEMTAG_EXCLUDE_ODD);
  size_t exclude;
  __asm__ (MI_MEMTAG_ASM "gmi %0, %1, %2" : "=r"(exclude) : "r"(p), "r"(class_exclude));
  return mi_memtag_random(p, exclude);
}

/* ----------------------------------------------------------------------------
  Tagging blocks
-----------------------------------------------------------------------------*/

// The tag exclusion mask for the block at index `idx` (alternating between even and odd so adjacent blocks never share a tag)
static inline size_t __mi_memtag_class_exclude(size_t idx) {
  return ((idx&1)==0 ? MI_MEMTAG_EXCLUDE_EVEN : MI_MEMTAG_EXCLUDE_ODD);
}

// Tag `count` fresh blocks of size `bsize` at `start` where the first block has index `idx` in its page.
static inline void mi_memtag_blocks_init(void* start, size_t bsize, size_t idx, size_t count) {
  uint8_t* p = (uint8_t*)start;
  for (size_t i = idx; i < idx + count; i++, p += bsize) {
    void* const tp = mi_memtag_random(p, __mi_memtag_class_exclude(i));
    mi_memtag_store(tp, bsize);
  }
}

// Retag a block on free with a fresh tag (of the same class) and zero it if it is small; returns the block with its new tag.
// The first read uses the current tag of `block` so a stale pointer (e.g. a double free) faults.
static inline void* mi_memtag_block_free(void* block, size_t bsize) {
  mi_memtag_check(block);
  void* const tblock = mi_memtag_random_next(block);
  if (bsize <= MI_MEMTAG_ZERO_MAX) { mi_memtag_store_zero(tblock, bsize); }
                              else { mi_memtag_store(tblock, bsize); }
  return tblock;
}

// Retag a canonical block (with the meta tag, see `_mi_memtag_canonical`) at index `idx` with a fresh tag of its class;
// returns the block with its new tag. The first read uses the meta tag so a stale pointer faults.
static inline void* mi_memtag_block_uncanonical(void* block, size_t bsize, size_t idx) {
  mi_memtag_check(block);
  void* const tblock = mi_memtag_random(block, __mi_memtag_class_exclude(idx));
  mi_memtag_store(tblock, bsize);
  return tblock;
}

// Reset the tags of `[p,p+size)` to the meta data tag
static inline void mi_memtag_clear(void* p, size_t size) {
  mi_memtag_store(mi_ptr_memtag_meta(p), size);
}

// Tag the meta data `[p,p+size)` with a random tag; returns `p` with its new tag.
// The tag has the class of index `idx` such that meta data at adjacent indices has different tags.
static inline void* mi_memtag_meta_init(void* p, size_t size, size_t idx) {
  void* const tp = mi_memtag_random(mi_ptr_memtag_meta(p), __mi_memtag_class_exclude(idx));
  mi_memtag_store(tp, size);
  return tp;
}

#else
static inline size_t mi_memtag_cpu_line_size(void)             { return 0; }
static inline void  mi_memtag_check(const void* p)             { MI_UNUSED(p); }
static inline void* mi_memtag_random(void* p, size_t exclude)  { MI_UNUSED(exclude); return p; }
static inline void* mi_memtag_load(void* p)                    { return p; }
static inline void  mi_memtag_store(void* p, size_t size)      { MI_UNUSED(p); MI_UNUSED(size); }
static inline void  mi_memtag_store_zero(void* p, size_t size) { MI_UNUSED(p); MI_UNUSED(size); }
static inline void* mi_memtag_random_next(void* p)             { return p; }

static inline void mi_memtag_blocks_init(void* start, size_t bsize, size_t idx, size_t count) {
  MI_UNUSED(start); MI_UNUSED(bsize); MI_UNUSED(idx); MI_UNUSED(count);
}
static inline void* mi_memtag_block_free(void* block, size_t bsize) {
  MI_UNUSED(bsize); return block;
}
static inline void* mi_memtag_block_uncanonical(void* block, size_t bsize, size_t idx) {
  MI_UNUSED(bsize); MI_UNUSED(idx); return block;
}
static inline void mi_memtag_clear(void* p, size_t size) {
  MI_UNUSED(p); MI_UNUSED(size);
}
static inline void* mi_memtag_meta_init(void* p, size_t size, size_t idx) {
  MI_UNUSED(size); MI_UNUSED(idx); return p;
}
#endif

#endif // MI_MEMTAG_H
