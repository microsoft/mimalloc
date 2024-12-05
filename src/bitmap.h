/* ----------------------------------------------------------------------------
Copyright (c) 2019-2023 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
Concurrent bitmap that can set/reset sequences of bits atomically
---------------------------------------------------------------------------- */
#pragma once
#ifndef MI_BITMAP_H
#define MI_BITMAP_H

/* --------------------------------------------------------------------------------
  Atomic bitmaps:

  `mi_bfield_t`: is a single machine word that can efficiently be bit counted (usually `size_t`)
      each bit usually represents a single MI_ARENA_SLICE_SIZE in an arena (64 KiB).
      We need 16K bits to represent a 1GiB arena.

  `mi_bitmap_chunk_t`: a chunk of bfield's of a total of MI_BITMAP_CHUNK_BITS (= 512)
      allocations never span across chunks -- so MI_ARENA_MAX_OBJ_SIZE is the number
      of bits in a chunk times the MI_ARENA_SLICE_SIZE (512 * 64KiB = 32 MiB).
      These chunks are cache-aligned and we can use AVX2/AVX512/SVE/SVE2/etc. instructions
      to scan for bits (perhaps) more efficiently.

   `mi_chunkmap_t`: for each chunk we track if it has (potentially) any bit set.
      The chunkmap has 1 bit per chunk that is set if the chunk potentially has a bit set.
      This is used to avoid scanning every chunk. (and thus strictly an optimization)
      It is conservative: it is fine to a bit in the chunk map even if the chunk turns out
      to have no bits set.

      When we (potentially) set a bit in a chunk, we first update the chunkmap.
      However, when we clear a bit in a chunk, and the chunk is indeed all clear, we
      cannot safely clear the bit corresponding to the chunk in the chunkmap since it
      may race with another thread setting a bit in the same chunk (and we may clear the
      bit even though a bit is set in the chunk which is not allowed).

      To fix this, the chunkmap contains 32-bits of bits for chunks, and a 32-bit "epoch"
      counter that is increased everytime a bit is set. We only clear a bit if the epoch
      stayed the same over our clear operation (so we know no other thread in the mean
      time set a bit in any of the chunks corresponding to the chunkmap).
      Since increasing the epoch and setting a bit must be atomic, we use only half-word
      bits (32) (we could use 128-bit atomics if needed since modern hardware supports this)

   `mi_bitmap_t`: a bitmap with N chunks. A bitmap always has MI_BITMAP_MAX_CHUNK_FIELDS (=16)
      and can support arena's from few chunks up to 16 chunkmap's = 16 * 32 chunks = 16 GiB
      The `chunk_count` can be anything from 1 to the max supported by the chunkmap's but
      each chunk is always complete (512 bits, so 512 * 64KiB = 32MiB memory area's).

   For now, the implementation assumes MI_HAS_FAST_BITSCAN and uses trailing-zero-count
   and pop-count (but we think it can be adapted work reasonably well on older hardware too)
--------------------------------------------------------------------------------------------- */

// A word-size bit field.
typedef size_t mi_bfield_t;

#define MI_BFIELD_BITS_SHIFT               (MI_SIZE_SHIFT+3)
#define MI_BFIELD_BITS                     (1 << MI_BFIELD_BITS_SHIFT)
#define MI_BFIELD_SIZE                     (MI_BFIELD_BITS/8)
#define MI_BFIELD_BITS_MOD_MASK            (MI_BFIELD_BITS - 1)
#define MI_BFIELD_LO_BIT8                  (((~(mi_bfield_t)0))/0xFF)         // 0x01010101 ..
#define MI_BFIELD_HI_BIT8                  (MI_BFIELD_LO_BIT8 << 7)           // 0x80808080 ..

#define MI_BITMAP_CHUNK_SIZE               (MI_BITMAP_CHUNK_BITS / 8)
#define MI_BITMAP_CHUNK_FIELDS             (MI_BITMAP_CHUNK_BITS / MI_BFIELD_BITS)
#define MI_BITMAP_CHUNK_BITS_MOD_MASK      (MI_BITMAP_CHUNK_BITS - 1)

// A bitmap chunk contains 512 bits of bfields on 64_bit  (256 on 32-bit)
typedef mi_decl_align(MI_BITMAP_CHUNK_SIZE) struct mi_bitmap_chunk_s {
  _Atomic(mi_bfield_t) bfields[MI_BITMAP_CHUNK_FIELDS];
} mi_bitmap_chunk_t;


// for now 32-bit epoch + 32-bit bit-set   (note: with ABA instructions we can double this)
typedef uint64_t mi_chunkmap_t;
typedef uint32_t mi_epoch_t;
typedef uint32_t mi_cmap_t;


#define MI_CHUNKMAP_BITS            (32)   // 1 chunkmap tracks 32 chunks

#define MI_BITMAP_MAX_CHUNKMAPS     (16)
#define MI_BITMAP_MAX_CHUNK_COUNT   (MI_BITMAP_MAX_CHUNKMAPS * MI_CHUNKMAP_BITS)
#define MI_BITMAP_MIN_CHUNK_COUNT   (1 * MI_CHUNKMAP_BITS)                              // 1 GiB arena

#define MI_BITMAP_MAX_BIT_COUNT     (MI_BITMAP_MAX_CHUNK_COUNT * MI_BITMAP_CHUNK_BITS)  // 16 GiB arena
#define MI_BITMAP_MIN_BIT_COUNT     (MI_BITMAP_MIN_CHUNK_COUNT * MI_BITMAP_CHUNK_BITS)  //  1 GiB arena


// An atomic bitmap
typedef mi_decl_align(MI_BITMAP_CHUNK_SIZE) struct mi_bitmap_s {
  _Atomic(size_t)         chunk_map_count; // valid chunk_map's
  _Atomic(size_t)         chunk_count;     // total count of chunks
  size_t                  padding[MI_BITMAP_CHUNK_SIZE/MI_SIZE_SIZE - 2];    // suppress warning on msvc
  _Atomic(mi_chunkmap_t)  chunk_maps[MI_BITMAP_MAX_CHUNKMAPS];

  mi_bitmap_chunk_t       chunks[MI_BITMAP_MIN_BIT_COUNT];  // or more, up to MI_BITMAP_MAX_CHUNK_COUNT
} mi_bitmap_t;


static inline size_t mi_bitmap_chunk_map_count(const mi_bitmap_t* bitmap) {
  return mi_atomic_load_relaxed(&bitmap->chunk_map_count);
}

static inline size_t mi_bitmap_chunk_count(const mi_bitmap_t* bitmap) {
  return mi_atomic_load_relaxed(&bitmap->chunk_count);
}

static inline size_t mi_bitmap_max_bits(const mi_bitmap_t* bitmap) {
  return (mi_bitmap_chunk_count(bitmap) * MI_BITMAP_CHUNK_BITS);
}



/* --------------------------------------------------------------------------------
  Atomic bitmap operations
-------------------------------------------------------------------------------- */

// Many operations are generic over setting or clearing the bit sequence: we use `mi_xset_t` for this (true if setting, false if clearing)
typedef bool  mi_xset_t;
#define MI_BIT_SET    (true)
#define MI_BIT_CLEAR  (false)


// Required size of a bitmap to represent `bit_count` bits.
size_t mi_bitmap_size(size_t bit_count, size_t* chunk_count);

// Initialize a bitmap to all clear; avoid a mem_zero if `already_zero` is true
// returns the size of the bitmap.
size_t mi_bitmap_init(mi_bitmap_t* bitmap, size_t bit_count, bool already_zero);

// Set/clear a sequence of `n` bits in the bitmap (and can cross chunks). Not atomic so only use if local to a thread.
void mi_bitmap_unsafe_setN(mi_bitmap_t* bitmap, size_t idx, size_t n);

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from all 0's to 1's (or all 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
// If `already_xset` is not NULL, it is set to true if all the bits were already all set/cleared.
bool mi_bitmap_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, size_t* already_xset);

static inline bool mi_bitmap_setN(mi_bitmap_t* bitmap, size_t idx, size_t n, size_t* already_set) {
  return mi_bitmap_xsetN(MI_BIT_SET, bitmap, idx, n, already_set);
}

static inline bool mi_bitmap_clearN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  return mi_bitmap_xsetN(MI_BIT_CLEAR, bitmap, idx, n, NULL);
}


// Is a sequence of n bits already all set/cleared?
bool mi_bitmap_is_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n);

static inline bool mi_bitmap_is_setN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  return mi_bitmap_is_xsetN(MI_BIT_SET, bitmap, idx, n);
}

static inline bool mi_bitmap_is_clearN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  return mi_bitmap_is_xsetN(MI_BIT_CLEAR, bitmap, idx, n);
}


// Try to set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's)
// and false otherwise leaving the bitmask as is.
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
mi_decl_nodiscard bool mi_bitmap_try_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n);

static inline bool mi_bitmap_try_setN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  return mi_bitmap_try_xsetN(MI_BIT_SET, bitmap, idx, n);
}

static inline bool mi_bitmap_try_clearN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  return mi_bitmap_try_xsetN(MI_BIT_CLEAR, bitmap, idx, n);
}

// Find a sequence of `n` bits in the bitmap with all bits set, and atomically unset all.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-n`.
mi_decl_nodiscard bool mi_bitmap_try_find_and_clearN(mi_bitmap_t* bitmap, size_t n, size_t tseq, size_t* pidx);



/* --------------------------------------------------------------------------------
  Atomic bitmap for a pair of bits.

  The valid pairs are CLEAR (0), SET (3), or BUSY (2).

  These bit pairs are used in the abandoned pages maps: when set, the entry has
  an available page. When we scan for an available abandoned page and find an entry SET,
  we first set it to BUSY, and try to claim the page atomically (since it can race
  with a concurrent `mi_free` which also tries to claim the page). However, unlike `mi_free`,
  we cannot be sure that a concurrent `mi_free` also didn't free (and decommit) the page
  just when we got the entry. Therefore, a page can only be freed after `mi_arena_unabandon`
  which (busy) waits until the BUSY flag is cleared to ensure all readers are done.
  (and pair-bit operations must therefore be release_acquire).
-------------------------------------------------------------------------------- */

#define MI_PAIR_CLEAR   (0)
#define MI_PAIR_UNUSED  (1)   // should never occur
#define MI_PAIR_BUSY    (2)
#define MI_PAIR_SET     (3)

// 0b....0101010101010101
#define MI_BFIELD_LO_BIT2     ((MI_BFIELD_LO_BIT8 << 6)|(MI_BFIELD_LO_BIT8 << 4)|(MI_BFIELD_LO_BIT8 << 2)|MI_BFIELD_LO_BIT8)

// A pairmap manipulates pairs of bits (and consists of 2 bitmaps)
typedef struct mi_pairmap_s {
  mi_bitmap_t* bitmap1;
  mi_bitmap_t* bitmap2;
} mi_pairmap_t;

// initialize a pairmap to all clear; avoid a mem_zero if `already_zero` is true
void mi_pairmap_init(mi_pairmap_t* pairmap, mi_bitmap_t* bm1, mi_bitmap_t* bm2);
bool mi_pairmap_set(mi_pairmap_t* pairmap, size_t pair_idx);
bool mi_pairmap_clear(mi_pairmap_t* pairmap, size_t pair_idx);
bool mi_pairmap_is_clear(mi_pairmap_t* pairmap, size_t pair_idx);
void mi_pairmap_clear_once_not_busy(mi_pairmap_t* pairmap, size_t pair_idx);

typedef bool (mi_bitmap_claim_while_busy_fun_t)(size_t pair_index, void* arg1, void* arg2);
mi_decl_nodiscard bool mi_pairmap_try_find_and_set_busy(mi_pairmap_t* pairmap, size_t tseq, size_t* pidx,
                                                        mi_bitmap_claim_while_busy_fun_t* claim, void* arg1 ,void* arg2
                                                       );


#endif // MI_BITMAP_H
