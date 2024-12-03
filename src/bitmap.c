/* ----------------------------------------------------------------------------
Copyright (c) 2019-2024 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
Concurrent bitmap that can set/reset sequences of bits atomically
---------------------------------------------------------------------------- */

#include "mimalloc.h"
#include "mimalloc/internal.h"
#include "mimalloc/bits.h"
#include "bitmap.h"

/* --------------------------------------------------------------------------------
  bfields
-------------------------------------------------------------------------------- */

static inline size_t mi_bfield_ctz(mi_bfield_t x) {
  return mi_ctz(x);
}


static inline size_t mi_bfield_popcount(mi_bfield_t x) {
  return mi_popcount(x);
}

//static inline size_t mi_bfield_clz(mi_bfield_t x) {
//  return mi_clz(x);
//}

// find the least significant bit that is set (i.e. count trailing zero's)
// return false if `x==0` (with `*idx` undefined) and true otherwise,
// with the `idx` is set to the bit index (`0 <= *idx < MI_BFIELD_BITS`).
static inline bool mi_bfield_find_least_bit(mi_bfield_t x, size_t* idx) {
  return mi_bsf(x,idx);
}

static inline mi_bfield_t mi_bfield_rotate_right(mi_bfield_t x, size_t r) {
  return mi_rotr(x,r);
}

// Find the least significant bit that can be xset (0 for MI_BIT_SET, 1 for MI_BIT_CLEAR).
// return false if `x==~0` (for MI_BIT_SET) or `x==0` for MI_BIT_CLEAR (with `*idx` undefined) and true otherwise,
// with the `idx` is set to the bit index (`0 <= *idx < MI_BFIELD_BITS`).
static inline bool mi_bfield_find_least_to_xset(mi_bit_t set, mi_bfield_t x, size_t* idx) {
  return mi_bfield_find_least_bit((set ? ~x : x), idx);
}

// Set a bit atomically. Returns `true` if the bit transitioned from 0 to 1
static inline bool mi_bfield_atomic_set(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = ((mi_bfield_t)1)<<idx;
  const mi_bfield_t old = mi_atomic_or_acq_rel(b, mask);
  return ((old&mask) == 0);
}

// Clear a bit atomically. Returns `true` if the bit transitioned from 1 to 0.
static inline bool mi_bfield_atomic_clear(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = ((mi_bfield_t)1)<<idx;
  mi_bfield_t old = mi_atomic_and_acq_rel(b, ~mask);
  return ((old&mask) == mask);
}

// Set/clear a bit atomically. Returns `true` if the bit transitioned from 0 to 1 (or 1 to 0).
static inline bool mi_bfield_atomic_xset(mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
  if (set) {
    return mi_bfield_atomic_set(b, idx);
  }
  else {
    return mi_bfield_atomic_clear(b, idx);
  }
}

// Set a pair of bits atomically, and return true of the mask bits transitioned from all 0's to 1's.
static inline bool mi_bfield_atomic_set2(_Atomic(mi_bfield_t)*b, size_t idx, bool* all_already_set) {
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  const size_t mask = (mi_bfield_t)0x03 << idx;
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old|mask));  // try to atomically set the mask bits until success
  if (all_already_set!=NULL) { *all_already_set = ((old&mask)==mask); }
  return ((old&mask) == 0);
}

// Clear a pair of bits atomically, and return true of the mask bits transitioned from all 1's to 0's
static inline bool mi_bfield_atomic_clear2(_Atomic(mi_bfield_t)*b, size_t idx, bool* all_already_clear) {
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  const size_t mask = (mi_bfield_t)0x03 << idx;
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask));  // try to atomically clear the mask bits until success
  if (all_already_clear!=NULL) { *all_already_clear = ((old&mask) == 0); }
  return ((old&mask) == mask);
}

// Set/clear a pair of bits atomically, and return true of the mask bits transitioned from all 0's to 1's (or all 1's to 0's)
static inline bool mi_bfield_atomic_xset2(mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t idx, bool* already_xset) {
  if (set) {
    return mi_bfield_atomic_set2(b, idx, already_xset);
  }
  else {
    return mi_bfield_atomic_clear2(b, idx, already_xset);
  }
}


// Set a mask set of bits atomically, and return true of the mask bits transitioned from all 0's to 1's.
static inline bool mi_bfield_atomic_set_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask, size_t* already_set) {
  mi_assert_internal(mask != 0);
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old|mask));  // try to atomically set the mask bits until success
  if (already_set!=NULL) { *already_set = mi_bfield_popcount(old&mask); }
  return ((old&mask) == 0);
}

// Clear a mask set of bits atomically, and return true of the mask bits transitioned from all 1's to 0's
static inline bool mi_bfield_atomic_clear_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask, size_t* already_clear) {
  mi_assert_internal(mask != 0);
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask));  // try to atomically clear the mask bits until success
  if (already_clear!=NULL) { *already_clear = mi_bfield_popcount(~(old&mask)); }
  return ((old&mask) == mask);
}

// Set/clear a mask set of bits atomically, and return true of the mask bits transitioned from all 0's to 1's (or all 1's to 0's)
static inline bool mi_bfield_atomic_xset_mask(mi_bit_t set, _Atomic(mi_bfield_t)*b, mi_bfield_t mask, size_t* already_xset) {
  mi_assert_internal(mask != 0);
  if (set) {
    return mi_bfield_atomic_set_mask(b, mask, already_xset);
  }
  else {
    return mi_bfield_atomic_clear_mask(b, mask, already_xset);
  }
}


// Tries to set a bit atomically. Returns `true` if the bit transitioned from 0 to 1
// and otherwise false (leaving the bit unchanged)
static inline bool mi_bfield_atomic_try_set(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  return mi_bfield_atomic_set(b, idx); // for a single bit there is no difference
}

// Tries to clear a bit atomically. Returns `true` if the bit transitioned from 1 to 0.
// `allclear` is set to true if the new bfield is all zeros (and false otherwise)
static inline bool mi_bfield_atomic_try_clear(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  return mi_bfield_atomic_clear(b, idx);
}

// Tries to set/clear a bit atomically, and returns true if the bit atomically transitioned from 0 to 1 (or 1 to 0)
static inline bool mi_bfield_atomic_try_xset( mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  // for a single bit, we can always just set/clear and test afterwards if it was actually us that changed it first
  return mi_bfield_atomic_xset(set, b, idx);
}


// Tries to  set a mask atomically, and returns true if the mask bits atomically transitioned from 0 to mask
// and false otherwise (leaving the bit field as is).
static inline bool mi_bfield_atomic_try_set_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask) {
  mi_assert_internal(mask != 0);
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  do {
    if ((old&mask) != 0) return false; // the mask bits are no longer 0
  } while (!mi_atomic_cas_weak_acq_rel(b, &old, old|mask));  // try to atomically set the mask bits
  return true;
}

// Tries to clear a mask atomically, and returns true if the mask bits atomically transitioned from mask to 0
// and false otherwise (leaving the bit field as is).
static inline bool mi_bfield_atomic_try_clear_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask) {
  mi_assert_internal(mask != 0);
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  do {
    if ((old&mask) != mask) return false; // the mask bits are no longer set
  } while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask));  // try to atomically clear the mask bits
  return true;
}


// Tries to (un)set a mask atomically, and returns true if the mask bits atomically transitioned from 0 to mask (or mask to 0)
// and false otherwise (leaving the bit field as is).
static inline bool mi_bfield_atomic_try_xset_mask(mi_bit_t set, _Atomic(mi_bfield_t)* b, mi_bfield_t mask ) {
  mi_assert_internal(mask != 0);
  if (set) {
    return mi_bfield_atomic_try_set_mask(b, mask);
  }
  else {
    return mi_bfield_atomic_try_clear_mask(b, mask);
  }
}

// Tries to set a byte atomically, and returns true if the byte atomically transitioned from 0 to 0xFF
// and false otherwise (leaving the bit field as is).
static inline bool mi_bfield_atomic_try_set8(_Atomic(mi_bfield_t)*b, size_t byte_idx) {
  mi_assert_internal(byte_idx < MI_BFIELD_SIZE);
  const mi_bfield_t mask = ((mi_bfield_t)0xFF)<<(byte_idx*8);
  return mi_bfield_atomic_try_set_mask(b, mask);
}

// Tries to clear a byte atomically, and returns true if the byte atomically transitioned from 0xFF to 0
static inline bool mi_bfield_atomic_try_clear8(_Atomic(mi_bfield_t)*b, size_t byte_idx) {
  mi_assert_internal(byte_idx < MI_BFIELD_SIZE);
  const mi_bfield_t mask = ((mi_bfield_t)0xFF)<<(byte_idx*8);
  return mi_bfield_atomic_try_clear_mask(b, mask);
}

// Tries to set/clear a byte atomically, and returns true if the byte atomically transitioned from 0 to 0xFF (or 0xFF to 0)
// and false otherwise (leaving the bit field as is).
static inline bool mi_bfield_atomic_try_xset8(mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t byte_idx) {
  mi_assert_internal(byte_idx < MI_BFIELD_SIZE);
  const mi_bfield_t mask = ((mi_bfield_t)0xFF)<<(byte_idx*8);
  return mi_bfield_atomic_try_xset_mask(set, b, mask);
}


// Check if all bits corresponding to a mask are set.
static inline bool mi_bfield_atomic_is_set_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask) {
  mi_assert_internal(mask != 0);
  return ((*b & mask) == mask);
}

// Check if all bits corresponding to a mask are clear.
static inline bool mi_bfield_atomic_is_clear_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask) {
  mi_assert_internal(mask != 0);
  return ((*b & mask) == 0);
}


// Check if all bits corresponding to a mask are set/cleared.
static inline bool mi_bfield_atomic_is_xset_mask(mi_bit_t set, _Atomic(mi_bfield_t)*b, mi_bfield_t mask) {
  mi_assert_internal(mask != 0);
  if (set) {
    return mi_bfield_atomic_is_set_mask(b, mask);
  }
  else {
    return mi_bfield_atomic_is_clear_mask(b, mask);
  }
}


// Check if a bit is set/clear
// static inline bool mi_bfield_atomic_is_xset(mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
//   mi_assert_internal(idx < MI_BFIELD_BITS);
//   const mi_bfield_t mask = ((mi_bfield_t)1)<<idx;
//   return mi_bfield_atomic_is_xset_mask(set, b, mask);
// }


/* --------------------------------------------------------------------------------
 bitmap chunks
-------------------------------------------------------------------------------- */

// Set/clear 2 (aligned) bits within a chunk.
// Returns true if both bits transitioned from 0 to 1 (or 1 to 0).
static inline bool mi_bitmap_chunk_xset2(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx, bool* all_already_xset) {
  mi_assert_internal(cidx < MI_BITMAP_CHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  mi_assert_internal((idx%2)==0);  
  return mi_bfield_atomic_xset2(set, &chunk->bfields[i], idx, all_already_xset);
}

static inline bool mi_bitmap_chunk_set2(mi_bitmap_chunk_t* chunk, size_t cidx, bool* all_already_set) {
  return mi_bitmap_chunk_xset2(MI_BIT_SET, chunk, cidx, all_already_set);
}

static inline bool mi_bitmap_chunk_clear2(mi_bitmap_chunk_t* chunk, size_t cidx, bool* all_already_clear) {
  return mi_bitmap_chunk_xset2(MI_BIT_CLEAR, chunk, cidx, all_already_clear);
}


// Set/clear a sequence of `n` bits within a chunk.
// Returns true if all bits transitioned from 0 to 1 (or 1 to 0).
static bool mi_bitmap_chunk_xsetN(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx, size_t n, size_t* pall_already_xset) {
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(n>0);
  bool all_transition = true;
  size_t all_already_xset = 0;
  size_t idx   = cidx % MI_BFIELD_BITS;
  size_t field = cidx / MI_BFIELD_BITS;
  while (n > 0) {
    size_t m = MI_BFIELD_BITS - idx;   // m is the bits to xset in this field
    if (m > n) { m = n; }
    mi_assert_internal(idx + m <= MI_BFIELD_BITS);
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    const size_t mask = (m == MI_BFIELD_BITS ? ~MI_ZU(0) : ((MI_ZU(1)<<m)-1) << idx);
    size_t already_xset = 0;
    all_transition = all_transition && mi_bfield_atomic_xset_mask(set, &chunk->bfields[field], mask, &already_xset );
    all_already_xset += already_xset;
    // next field
    field++;
    idx = 0;
    n -= m;
  }
  if (pall_already_xset!=NULL) { *pall_already_xset = all_already_xset; }
  return all_transition;
}


static inline bool mi_bitmap_chunk_setN(mi_bitmap_chunk_t* chunk, size_t cidx, size_t n, size_t* already_set) {
  return mi_bitmap_chunk_xsetN(MI_BIT_SET, chunk, cidx, n, already_set);
}

static inline bool mi_bitmap_chunk_clearN(mi_bitmap_chunk_t* chunk, size_t cidx, size_t n, size_t* already_clear) {
  return mi_bitmap_chunk_xsetN(MI_BIT_CLEAR, chunk, cidx, n, already_clear);
}


// check if a pair of bits is set/clear
static inline bool mi_bitmap_chunk_is_xset2(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx) {
  mi_assert_internal(cidx < MI_BITMAP_CHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  mi_assert_internal((idx%2)==0);
  const size_t mask = (mi_bfield_t)0x03 << idx;
  return mi_bfield_atomic_is_xset_mask(set, &chunk->bfields[i], mask);
}

static inline bool mi_bitmap_chunk_is_set2(mi_bitmap_chunk_t* chunk, size_t cidx) {
  return mi_bitmap_chunk_is_xset2(MI_BIT_SET, chunk, cidx);
}

static inline bool mi_bitmap_chunk_is_clear2(mi_bitmap_chunk_t* chunk, size_t cidx) {
  return mi_bitmap_chunk_is_xset2(MI_BIT_CLEAR, chunk, cidx);
}


// Check if a sequence of `n` bits within a chunk are all set/cleared.
static bool mi_bitmap_chunk_is_xsetN(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx, size_t n) {
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(n>0);
  bool all_xset = true;
  size_t idx = cidx % MI_BFIELD_BITS;
  size_t field = cidx / MI_BFIELD_BITS;
  while (n > 0) {
    size_t m = MI_BFIELD_BITS - idx;   // m is the bits to xset in this field
    if (m > n) { m = n; }
    mi_assert_internal(idx + m <= MI_BFIELD_BITS);
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    const size_t mask = (m == MI_BFIELD_BITS ? ~MI_ZU(0) : ((MI_ZU(1)<<m)-1) << idx);
    all_xset = all_xset && mi_bfield_atomic_is_xset_mask(set, &chunk->bfields[field], mask);
    // next field
    field++;
    idx = 0;
    n -= m;
  }
  return all_xset;
}



static inline bool mi_bitmap_chunk_try_xset(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx) {
  mi_assert_internal(cidx < MI_BITMAP_CHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  return mi_bfield_atomic_try_xset(set, &chunk->bfields[i], idx);
}

static inline bool mi_bitmap_chunk_try_set(mi_bitmap_chunk_t* chunk, size_t cidx) {
  return mi_bitmap_chunk_try_xset(MI_BIT_SET, chunk, cidx);
}

static inline bool mi_bitmap_chunk_try_clear(mi_bitmap_chunk_t* chunk, size_t cidx) {
  return mi_bitmap_chunk_try_xset(MI_BIT_CLEAR, chunk, cidx);
}

static inline bool mi_bitmap_chunk_try_xset8(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t byte_idx) {
  mi_assert_internal(byte_idx*8 < MI_BITMAP_CHUNK_BITS);
  const size_t i = byte_idx / MI_BFIELD_SIZE;
  const size_t ibyte_idx = byte_idx % MI_BFIELD_SIZE;
  return mi_bfield_atomic_try_xset8(set, &chunk->bfields[i], ibyte_idx);
}

static inline bool mi_bitmap_chunk_try_set8(mi_bitmap_chunk_t* chunk, size_t byte_idx) {
  return mi_bitmap_chunk_try_xset8(MI_BIT_SET, chunk, byte_idx);
}

static inline bool mi_bitmap_chunk_try_clear8(mi_bitmap_chunk_t* chunk, size_t byte_idx) {
  return mi_bitmap_chunk_try_xset8(MI_BIT_CLEAR, chunk, byte_idx);
}

// Try to atomically set/clear a sequence of `n` bits within a chunk.
// Returns true if all bits transitioned from 0 to 1 (or 1 to 0),
// and false otherwise leaving all bit fields as is.
static bool mi_bitmap_chunk_try_xsetN(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx, size_t n) {
  mi_assert_internal(cidx + n < MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(n>0);
  if (n==0) return true;
  size_t start_idx = cidx % MI_BFIELD_BITS;
  size_t start_field = cidx / MI_BFIELD_BITS;
  size_t end_field = MI_BITMAP_CHUNK_FIELDS;
  size_t mask_mid = 0;
  size_t mask_end = 0;

  // first field
  size_t field = start_field;
  size_t m = MI_BFIELD_BITS - start_idx;   // m is the bits to xset in this field
  if (m > n) { m = n; }
  mi_assert_internal(start_idx + m <= MI_BFIELD_BITS);
  mi_assert_internal(start_field < MI_BITMAP_CHUNK_FIELDS);
  const size_t mask_start = (m == MI_BFIELD_BITS ? ~MI_ZU(0) : ((MI_ZU(1)<<m)-1) << start_idx);
  if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_start)) return false;

  // done?
  n -= m;
  if (n==0) return true;

  // continue with mid fields and last field: if these fail we need to recover by unsetting previous fields

  // mid fields
  while (n >= MI_BFIELD_BITS) {
    field++;
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    mask_mid = ~MI_ZU(0);
    if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_mid)) goto restore;
    n -= MI_BFIELD_BITS;
  }

  // last field
  if (n > 0) {
    mi_assert_internal(n < MI_BFIELD_BITS);
    field++;
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    end_field = field;
    mask_end = (MI_ZU(1)<<n)-1;
    if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_end)) goto restore;
  }

  return true;

restore:
  // field is on the field that failed to set atomically; we need to restore all previous fields
  mi_assert_internal(field > start_field);
  while( field > start_field) {
    field--;
    const size_t mask = (field == start_field ? mask_start : (field == end_field ? mask_end : mask_mid));
    mi_bfield_atomic_xset_mask(!set, &chunk->bfields[field], mask, NULL);
  }
  return false;
}

static inline bool mi_bitmap_chunk_try_setN(mi_bitmap_chunk_t* chunk, size_t cidx, size_t n) {
  return mi_bitmap_chunk_try_xsetN(MI_BIT_SET, chunk, cidx, n);
}

static inline bool mi_bitmap_chunk_try_clearN(mi_bitmap_chunk_t* chunk, size_t cidx, size_t n) {
  return mi_bitmap_chunk_try_xsetN(MI_BIT_CLEAR, chunk, cidx, n);
}


// find least 0/1-bit in a chunk and try to set/clear it atomically
// set `*pidx` to the bit index (0 <= *pidx < MI_BITMAP_CHUNK_BITS) on success.
// todo: try neon version
static inline bool mi_bitmap_chunk_find_and_try_xset(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t* pidx) {
#if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  while (true) {
    const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vcmp = _mm256_cmpeq_epi64(vec, (set ? _mm256_set1_epi64x(~0) : _mm256_setzero_si256())); // (elem64 == ~0 / 0 ? 0xFF  : 0)
    const uint32_t mask = ~_mm256_movemask_epi8(vcmp);  // mask of most significant bit of each byte (so each 8 bits are all set or clear)
    // mask is inverted, so each 8-bits is 0xFF iff the corresponding elem64 has a zero / one bit (and thus can be set/cleared)
    if (mask==0) return false;
    mi_assert_internal((_tzcnt_u32(mask)%8) == 0); // tzcnt == 0, 8, 16, or 24
    const size_t chunk_idx = _tzcnt_u32(mask) / 8;
    mi_assert_internal(chunk_idx < MI_BITMAP_CHUNK_FIELDS);
    size_t cidx;
    if (mi_bfield_find_least_to_xset(set, chunk->bfields[chunk_idx], &cidx)) {           // find the bit-idx that is set/clear
      if mi_likely(mi_bfield_atomic_try_xset(set, &chunk->bfields[chunk_idx], cidx)) {  // set/clear it atomically
        *pidx = (chunk_idx*MI_BFIELD_BITS) + cidx;
        mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
        return true;
      }
    }
    // try again
  }
#elif defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==512)
  while (true) {
    size_t chunk_idx = 0;
    #if 1
    __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
    if ((set ? _mm256_test_all_ones(vec) : _mm256_testz_si256(vec,vec))) {
      chunk_idx += 4;
      vec = _mm256_load_si256(((const __m256i*)chunk->bfields) + 1);
    }
    const __m256i vcmp = _mm256_cmpeq_epi64(vec, (set ? _mm256_set1_epi64x(~0) : _mm256_setzero_si256())); // (elem64 == ~0 / 0 ? 0xFF  : 0)
    const uint32_t mask = ~_mm256_movemask_epi8(vcmp);  // mask of most significant bit of each byte (so each 8 bits are all set or clear)
    // mask is inverted, so each 8-bits is 0xFF iff the corresponding elem64 has a zero / one bit (and thus can be set/cleared)
    if (mask==0) return false;
    mi_assert_internal((_tzcnt_u32(mask)%8) == 0); // tzcnt == 0, 8, 16, or 24
    chunk_idx += _tzcnt_u32(mask) / 8;
    #else
    const __m256i vec1  = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vec2  = _mm256_load_si256(((const __m256i*)chunk->bfields)+1);
    const __m256i cmpv  = (set ? _mm256_set1_epi64x(~0) : _mm256_setzero_si256());
    const __m256i vcmp1 = _mm256_cmpeq_epi64(vec1, cmpv); // (elem64 == ~0 / 0 ? 0xFF  : 0)
    const __m256i vcmp2 = _mm256_cmpeq_epi64(vec2, cmpv); // (elem64 == ~0 / 0 ? 0xFF  : 0)
    const uint32_t mask1 = ~_mm256_movemask_epi8(vcmp1);  // mask of most significant bit of each byte (so each 8 bits are all set or clear)
    const uint32_t mask2 = ~_mm256_movemask_epi8(vcmp1);  // mask of most significant bit of each byte (so each 8 bits are all set or clear)
    const uint64_t mask = ((uint64_t)mask2 << 32) | mask1;
    // mask is inverted, so each 8-bits is 0xFF iff the corresponding elem64 has a zero / one bit (and thus can be set/cleared)
    if (mask==0) return false;
    mi_assert_internal((_tzcnt_u64(mask)%8) == 0); // tzcnt == 0, 8, 16, 24 , ..
    const size_t chunk_idx = _tzcnt_u64(mask) / 8;
    #endif
    mi_assert_internal(chunk_idx < MI_BITMAP_CHUNK_FIELDS);
    size_t cidx;
    if (mi_bfield_find_least_to_xset(set, chunk->bfields[chunk_idx], &cidx)) {           // find the bit-idx that is set/clear
      if mi_likely(mi_bfield_atomic_try_xset(set, &chunk->bfields[chunk_idx], cidx)) {  // set/clear it atomically
        *pidx = (chunk_idx*MI_BFIELD_BITS) + cidx;
        mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
        return true;
      }
    }
    // try again
  }
#else
  for (int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    size_t idx;
    if mi_unlikely(mi_bfield_find_least_to_xset(set, chunk->bfields[i], &idx)) { // find least 0-bit
      if mi_likely(mi_bfield_atomic_try_xset(set, &chunk->bfields[i], idx)) {  // try to set it atomically
        *pidx = (i*MI_BFIELD_BITS + idx);
        mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
        return true;
      }
    }
  }
  return false;
#endif
}

static inline bool mi_bitmap_chunk_find_and_try_clear(mi_bitmap_chunk_t* chunk, size_t* pidx) {
  return mi_bitmap_chunk_find_and_try_xset(MI_BIT_CLEAR, chunk, pidx);
}

static inline bool mi_bitmap_chunk_find_and_try_set(mi_bitmap_chunk_t* chunk, size_t* pidx) {
  return mi_bitmap_chunk_find_and_try_xset(MI_BIT_SET, chunk, pidx);
}


// find least byte in a chunk with all bits set, and try unset it atomically
// set `*pidx` to its bit index (0 <= *pidx < MI_BITMAP_CHUNK_BITS) on success.
// todo: try neon version
static inline bool mi_bitmap_chunk_find_and_try_clear8(mi_bitmap_chunk_t* chunk, size_t* pidx) {
  #if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  while(true) {
    const __m256i vec  = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vcmp = _mm256_cmpeq_epi8(vec, _mm256_set1_epi64x(~0)); // (byte == ~0 ? -1  : 0)
    const uint32_t mask = _mm256_movemask_epi8(vcmp);    // mask of most significant bit of each byte
    if (mask == 0) return false;
    const size_t i = _tzcnt_u32(mask);
    mi_assert_internal(8*i < MI_BITMAP_CHUNK_BITS);
    const size_t chunk_idx = i / MI_BFIELD_SIZE;
    const size_t byte_idx  = i % MI_BFIELD_SIZE;
    if mi_likely(mi_bfield_atomic_try_xset8(MI_BIT_CLEAR,&chunk->bfields[chunk_idx],byte_idx)) {  // try to unset atomically
      *pidx = (chunk_idx*MI_BFIELD_BITS) + (byte_idx*8);
      mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
      return true;
    }
    // try again
  }
  #else
    for(int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
      const mi_bfield_t x = chunk->bfields[i];
      // has_set8 has low bit in each byte set if the byte in x == 0xFF
      const mi_bfield_t has_set8 = ((~x - MI_BFIELD_LO_BIT8) &      // high bit set if byte in x is 0xFF or < 0x7F
                                    (x  & MI_BFIELD_HI_BIT8))       // high bit set if byte in x is >= 0x80
                                    >> 7;                           // shift high bit to low bit
      size_t idx;
      if mi_unlikely(mi_bfield_find_least_bit(has_set8,&idx)) { // find least 1-bit
        mi_assert_internal(idx <= (MI_BFIELD_BITS - 8));
        mi_assert_internal((idx%8)==0);
        const size_t byte_idx = idx/8;
        if mi_likely(mi_bfield_atomic_try_xset8(MI_BIT_CLEAR,&chunk->bfields[i],byte_idx)) {  // unset the byte atomically
          *pidx = (i*MI_BFIELD_BITS) + idx;
          mi_assert_internal(*pidx + 8 <= MI_BITMAP_CHUNK_BITS);
          return true;
        }
        // else continue
      }
    }
    return false;
  #endif
}


// find a sequence of `n` bits in a chunk with all `n` (`< MI_BFIELD_BITS`!) bits set,
// and try unset it atomically
// set `*pidx` to its bit index (0 <= *pidx <= MI_BITMAP_CHUNK_BITS - n) on success.
// todo: try avx2 and neon version
// todo: allow spanning across bfield boundaries?
static inline bool mi_bitmap_chunk_find_and_try_clearN(mi_bitmap_chunk_t* chunk, size_t n, size_t* pidx) {
  if (n == 0 || n > MI_BFIELD_BITS) return false;  // TODO: allow larger?
  const mi_bfield_t mask = (n==MI_BFIELD_BITS ? ~((mi_bfield_t)0) : (((mi_bfield_t)1) << n)-1);
  for(int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    mi_bfield_t b = chunk->bfields[i];
    size_t bshift = 0;
    size_t idx;
    while (mi_bfield_find_least_bit(b, &idx)) { // find least 1-bit
      b >>= idx;
      bshift += idx;
      if (bshift + n > MI_BFIELD_BITS) break;

      if ((b&mask) == mask) { // found a match
        mi_assert_internal( ((mask << bshift) >> bshift) == mask );
        if mi_likely(mi_bfield_atomic_try_clear_mask(&chunk->bfields[i],mask<<bshift)) {
          *pidx = (i*MI_BFIELD_BITS) + bshift;
          mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
          mi_assert_internal(*pidx + n <= MI_BITMAP_CHUNK_BITS);
          return true;
        }
        else {
          // if failed to atomically commit, try again from this position
          b = (chunk->bfields[i] >> bshift);
        }
      }
      else {
        // advance
        const size_t ones = mi_bfield_ctz(~b);      // skip all ones (since it didn't fit the mask)
        mi_assert_internal(ones>0);
        bshift += ones;
        b >>= ones;
      }
    }
  }
  return false;
}


// are all bits in a bitmap chunk set?
// static inline bool mi_bitmap_chunk_all_are_set(mi_bitmap_chunk_t* chunk) {
//   #if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
//   const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
//   return _mm256_test_all_ones(vec);
//   #else
//   // written like this for vectorization
//   mi_bfield_t x = chunk->bfields[0];
//   for(int i = 1; i < MI_BITMAP_CHUNK_FIELDS; i++) {
//     x = x & chunk->bfields[i];
//   }
//   return (~x == 0);
//   #endif
// }

// are all bits in a bitmap chunk clear?
static inline bool mi_bitmap_chunk_all_are_clear(mi_bitmap_chunk_t* chunk) {
  #if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
  return _mm256_testz_si256( vec, vec );
  #elif defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==512)
  const __m256i vec1 = _mm256_load_si256((const __m256i*)chunk->bfields);
  if (!_mm256_testz_si256(vec1, vec1)) return false;
  const __m256i vec2 = _mm256_load_si256(((const __m256i*)chunk->bfields)+1);
  return (_mm256_testz_si256(vec2, vec2));
  #else
  for(int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    if (chunk->bfields[i] != 0) return false;
  }
  return true;
  #endif
}

/* --------------------------------------------------------------------------------
  epochset (for now for 32-bit sets only)
-------------------------------------------------------------------------------- */

static void mi_epochset_split(mi_epochset_t es, uint32_t* bset, size_t* epoch) {
  *bset = (uint32_t)es;
  *epoch = (size_t)(es >> 32);
}

static mi_epochset_t mi_epochset_join(uint32_t bset, size_t epoch) {
  return ((uint64_t)epoch << 32) | bset;
}

// setting a bit increases the epoch
static void mi_epochset_set(_Atomic(mi_epochset_t)*es, size_t idx) {
  mi_assert(idx < 32);
  size_t epoch;
  uint32_t bset;
  mi_epochset_t es_new;
  mi_epochset_t es_old = mi_atomic_load_relaxed(es);
  do {
    mi_epochset_split(es_old, &bset, &epoch);
    es_new = mi_epochset_join(bset | (MI_ZU(1)<<idx), epoch+1);
  } while (!mi_atomic_cas_weak_acq_rel(es, &es_old, es_new));
}

// clear-ing a bit only works if the epoch didn't change (so we never clear unintended)
static bool mi_epochset_try_clear(_Atomic(mi_epochset_t)*es, size_t idx, size_t expected_epoch) {
  mi_assert(idx < MI_EPOCHSET_BITS);
  size_t   epoch;
  uint32_t bset;
  mi_epochset_t es_new;
  mi_epochset_t es_old = mi_atomic_load_relaxed(es);
  do {
    mi_epochset_split(es_old, &bset, &epoch);
    if (epoch != expected_epoch) return false;
    es_new = mi_epochset_join(bset & ~(MI_ZU(1)<<idx), epoch);  // no need to increase the epoch for clearing
  } while (!mi_atomic_cas_weak_acq_rel(es, &es_old, es_new));
  return true;
}

/* --------------------------------------------------------------------------------
 bitmap epochset
-------------------------------------------------------------------------------- */

static void mi_bitmap_anyset_set(mi_bitmap_t* bitmap, size_t chunk_idx) {
  mi_assert(chunk_idx < MI_BITMAP_CHUNK_COUNT);
  mi_epochset_set(&bitmap->any_set, chunk_idx);
}

static bool mi_bitmap_anyset_try_clear(mi_bitmap_t* bitmap, size_t chunk_idx, size_t epoch) {
  mi_assert(chunk_idx < MI_BITMAP_CHUNK_COUNT);
  return mi_epochset_try_clear(&bitmap->any_set, chunk_idx, epoch);
}

static uint32_t mi_bitmap_anyset(mi_bitmap_t* bitmap, size_t* epoch) {
  uint32_t bset;
  mi_epochset_split(mi_atomic_load_relaxed(&bitmap->any_set), &bset, epoch);
  return bset;
}

static size_t mi_bitmap_epoch(mi_bitmap_t* bitmap) {
  size_t   epoch;
  uint32_t bset;
  mi_epochset_split(mi_atomic_load_relaxed(&bitmap->any_set), &bset, &epoch);
  return epoch;
}

/* --------------------------------------------------------------------------------
 bitmap
-------------------------------------------------------------------------------- */

// initialize a bitmap to all unset; avoid a mem_zero if `already_zero` is true
void mi_bitmap_init(mi_bitmap_t* bitmap, bool already_zero) {
  if (!already_zero) {
    _mi_memzero_aligned(bitmap, sizeof(*bitmap));
  }
}

// Set a sequence of `n` bits in the bitmap (and can cross chunks). Not atomic so only use if local to a thread.
void mi_bitmap_unsafe_setN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(idx + n<=MI_BITMAP_MAX_BITS);

  // first chunk
  size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  size_t m = MI_BITMAP_CHUNK_BITS - cidx;
  if (m > n) { m = n; }
  mi_bitmap_chunk_setN(&bitmap->chunks[chunk_idx], cidx, m, NULL);
  mi_bitmap_anyset_set(bitmap, chunk_idx);

  // n can be large so use memset for efficiency for all in-between chunks
  chunk_idx++;
  n -= m;
  const size_t mid_chunks = n / MI_BITMAP_CHUNK_BITS;
  if (mid_chunks > 0) {
    _mi_memset(&bitmap->chunks[chunk_idx], ~0, mid_chunks * (MI_BITMAP_CHUNK_BITS/8));
    const size_t end_chunk = chunk_idx + mid_chunks;
    while (chunk_idx < end_chunk) {
      mi_bitmap_anyset_set(bitmap, chunk_idx);
      chunk_idx++;
    }
    n -= (mid_chunks * MI_BITMAP_CHUNK_BITS);
  }

  // last chunk
  if (n > 0) {
    mi_assert_internal(n < MI_BITMAP_CHUNK_BITS);
    mi_assert_internal(chunk_idx < MI_BITMAP_CHUNK_FIELDS);
    mi_bitmap_chunk_setN(&bitmap->chunks[chunk_idx], 0, n, NULL);
    mi_bitmap_anyset_set(bitmap, chunk_idx);
  }
}


// Try to set/clear a bit in the bitmap; returns `true` if atomically transitioned from 0 to 1 (or 1 to 0),
// and false otherwise leaving the bitmask as is.
bool mi_bitmap_try_xset(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < MI_BITMAP_MAX_BITS);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  if (set) {
    // first set the anyset since it is a conservative approximation (increases epoch)
    mi_bitmap_anyset_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_try_set(&bitmap->chunks[chunk_idx], cidx);
  }
  else {
    const size_t epoch = mi_bitmap_epoch(bitmap);
    bool cleared = mi_bitmap_chunk_try_clear(&bitmap->chunks[chunk_idx], cidx);
    if (cleared && epoch == mi_bitmap_epoch(bitmap) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_anyset_try_clear(bitmap, chunk_idx, epoch);
    }
    return cleared;
  }
}




// Try to set/clear a byte in the bitmap; returns `true` if atomically transitioned from 0 to 0xFF (or 0xFF to 0)
// and false otherwise leaving the bitmask as is.
bool mi_bitmap_try_xset8(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < MI_BITMAP_MAX_BITS);
  mi_assert_internal(idx%8 == 0);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t byte_idx  = (idx % MI_BITMAP_CHUNK_BITS)/8;
  if (set) {
    // first set the anyset since it is a conservative approximation (increases epoch)
    mi_bitmap_anyset_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_try_set8(&bitmap->chunks[chunk_idx], byte_idx);
  }
  else {
    const size_t epoch = mi_bitmap_epoch(bitmap);
    bool cleared = mi_bitmap_chunk_try_clear8(&bitmap->chunks[chunk_idx], byte_idx);
    if (cleared && epoch == mi_bitmap_epoch(bitmap) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_anyset_try_clear(bitmap, chunk_idx, epoch);
    }
    return cleared;
  }
}


// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's)
// and false otherwise leaving the bitmask as is.
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
bool mi_bitmap_try_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BITMAP_CHUNK_BITS);
  if (n==1) { return mi_bitmap_try_xset(set,bitmap,idx); }
  if (n==8) { return mi_bitmap_try_xset8(set,bitmap,idx); }

  mi_assert_internal(idx + n <= MI_BITMAP_MAX_BITS);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < MI_BFIELD_BITS);
  if (cidx + n > MI_BITMAP_CHUNK_BITS) { n = MI_BITMAP_CHUNK_BITS - cidx; }  // paranoia

  if (set) {
    // first set the anyset since it is a conservative approximation (increases epoch)
    mi_bitmap_anyset_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_try_setN(&bitmap->chunks[chunk_idx], cidx, n);
  }
  else {
    const size_t epoch = mi_bitmap_epoch(bitmap);
    bool cleared = mi_bitmap_chunk_try_clearN(&bitmap->chunks[chunk_idx], cidx, n);
    if (cleared && epoch == mi_bitmap_epoch(bitmap) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_anyset_try_clear(bitmap, chunk_idx, epoch);
    }
    return cleared;
  }
}


// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
bool mi_bitmap_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, size_t* already_xset ) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BITMAP_CHUNK_BITS);

  //if (n==1) { return mi_bitmap_xset(set, bitmap, idx); }
  //if (n==8) { return mi_bitmap_xset8(set, bitmap, idx); }

  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < MI_BFIELD_BITS);
  if (cidx + n > MI_BITMAP_CHUNK_BITS) { n = MI_BITMAP_CHUNK_BITS - cidx; }  // paranoia

  if (set) {
    // first set the anyset since it is a conservative approximation (increases epoch)
    mi_bitmap_anyset_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_setN(&bitmap->chunks[chunk_idx], cidx, n, already_xset);
  }
  else {
    const size_t epoch = mi_bitmap_epoch(bitmap);
    bool cleared = mi_bitmap_chunk_clearN(&bitmap->chunks[chunk_idx], cidx, n, already_xset);
    if (cleared && epoch == mi_bitmap_epoch(bitmap) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_anyset_try_clear(bitmap, chunk_idx, epoch);
    }
    return cleared;
  }
}


// Is a sequence of n bits already all set/cleared?
bool mi_bitmap_is_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(idx + n <= MI_BITMAP_MAX_BITS);

  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < MI_BFIELD_BITS);
  if (cidx + n > MI_BITMAP_CHUNK_BITS) { n = MI_BITMAP_CHUNK_BITS - cidx; }  // paranoia

  return mi_bitmap_chunk_is_xsetN(set, &bitmap->chunks[chunk_idx], cidx, n);
}


/* --------------------------------------------------------------------------------
  bitmap try_find_and_clear
-------------------------------------------------------------------------------- */


#define mi_bitmap_forall_set_chunks(bitmap,tseq,name_epoch,name_chunk_idx) \
  { uint32_t  _bit_idx; \
    uint32_t  _start = (uint32_t)(tseq % MI_EPOCHSET_BITS); \
    size_t    name_epoch; \
    uint32_t _any_set = mi_bitmap_anyset(bitmap,&name_epoch); \
    _any_set = mi_rotr32(_any_set, _start); \
    while (mi_bsf32(_any_set,&_bit_idx)) { \
      size_t name_chunk_idx = (_bit_idx + _start) % MI_BFIELD_BITS;

#define mi_bitmap_forall_set_chunks_end() \
      _start += _bit_idx+1;    /* so chunk_idx calculation stays valid */ \
      _any_set >>= _bit_idx;   /* skip scanned bits (and avoid UB with (_bit_idx+1)) */ \
      _any_set >>= 1; \
    } \
  }

// Find a set bit in a bitmap and atomically unset it. Returns true on success,
// and in that case sets the index: `0 <= *pidx < MI_BITMAP_MAX_BITS`.
// The low `MI_BFIELD_BITS` of start are used to set the start point of the search
// (to reduce thread contention).
mi_decl_nodiscard bool mi_bitmap_try_find_and_clear(mi_bitmap_t* bitmap, size_t tseq, size_t* pidx) {
  mi_bitmap_forall_set_chunks(bitmap, tseq, epoch, chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bitmap_chunk_find_and_try_clear(&bitmap->chunks[chunk_idx],&cidx)) {
      *pidx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
      mi_assert_internal(*pidx < MI_BITMAP_MAX_BITS);
      return true;
    }
    else {
      // we may find that all are unset only on a second iteration but that is ok as
      // _any_set is a conservative approximation.
      if (epoch == mi_bitmap_epoch(bitmap) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
        mi_bitmap_anyset_try_clear(bitmap, chunk_idx, epoch);
      }
    }
  }
  mi_bitmap_forall_set_chunks_end();
  return false;
}


// Find a byte in the bitmap with all bits set (0xFF) and atomically unset it to zero.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-8`.
mi_decl_nodiscard bool mi_bitmap_try_find_and_clear8(mi_bitmap_t* bitmap, size_t tseq, size_t* pidx ) {
  mi_bitmap_forall_set_chunks(bitmap,tseq, epoch, chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bitmap_chunk_find_and_try_clear8(&bitmap->chunks[chunk_idx],&cidx)) {
      *pidx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
      mi_assert_internal(*pidx <= MI_BITMAP_MAX_BITS-8);
      mi_assert_internal((*pidx % 8) == 0);
      return true;
    }
    else {
      // we may find that all are unset only on a second iteration but that is ok as
      // _any_set is a conservative approximation.
      if (epoch == mi_bitmap_epoch(bitmap) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
        mi_bitmap_anyset_try_clear(bitmap, chunk_idx, epoch);
      }
    }
  }
  mi_bitmap_forall_set_chunks_end();
  return false;
}

// Find a sequence of `n` bits in the bitmap with all bits set, and atomically unset all.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-n`.
mi_decl_nodiscard bool mi_bitmap_try_find_and_clearN(mi_bitmap_t* bitmap, size_t n, size_t tseq, size_t* pidx ) {
  // TODO: allow at least MI_BITMAP_CHUNK_BITS and probably larger
  // TODO: allow spanning across chunk boundaries
  if (n == 0 || n > MI_BFIELD_BITS) return false;
  mi_bitmap_forall_set_chunks(bitmap,tseq,epoch,chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bitmap_chunk_find_and_try_clearN(&bitmap->chunks[chunk_idx],n,&cidx)) {
      *pidx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
      mi_assert_internal(*pidx <= MI_BITMAP_MAX_BITS-n);
      return true;
    }
    else {
      // we may find that all are unset only on a second iteration but that is ok as
      // _any_set is a conservative approximation.
      if (epoch == mi_bitmap_epoch(bitmap) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
        mi_bitmap_anyset_try_clear(bitmap, chunk_idx, epoch);
      }
    }
  }
  mi_bitmap_forall_set_chunks_end();
  return false;
}


/* --------------------------------------------------------------------------------
  pairmap epochset
-------------------------------------------------------------------------------- */

static void mi_pairmap_anyset_set(mi_pairmap_t* pairmap, size_t chunk_idx) {
  mi_assert(chunk_idx < MI_BITMAP_CHUNK_COUNT);
  mi_epochset_set(&pairmap->any_set, chunk_idx);
}

static bool mi_pairmap_anyset_try_clear(mi_pairmap_t* pairmap, size_t chunk_idx, size_t epoch) {
  mi_assert(chunk_idx < MI_BITMAP_CHUNK_COUNT);
  return mi_epochset_try_clear(&pairmap->any_set, chunk_idx, epoch);
}

static uint32_t mi_pairmap_anyset(mi_pairmap_t* pairmap, size_t* epoch) {
  uint32_t bset;
  mi_epochset_split(mi_atomic_load_relaxed(&pairmap->any_set), &bset, epoch);
  return bset;
}

static size_t mi_pairmap_epoch(mi_pairmap_t* pairmap) {
  size_t   epoch;
  uint32_t bset;
  mi_epochset_split(mi_atomic_load_relaxed(&pairmap->any_set), &bset, &epoch);
  return epoch;
}

/* --------------------------------------------------------------------------------
  pairmap 
-------------------------------------------------------------------------------- */

// initialize a pairmap to all clear; avoid a mem_zero if `already_zero` is true
void mi_pairmap_init(mi_pairmap_t* pairmap, bool already_zero) {
  if (!already_zero) {
    _mi_memzero_aligned(pairmap, sizeof(*pairmap));
  }
}

/* --------------------------------------------------------------------------------
  pairmap set/clear unconditionally
-------------------------------------------------------------------------------- */

// is a pairmap entry clear?
bool mi_pairmap_is_clear(mi_pairmap_t* pairmap, size_t pair_idx) {
  const size_t idx = 2*pair_idx;
  mi_assert_internal(idx < MI_PAIRMAP_MAX_BITS);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  return mi_bitmap_chunk_is_clear2(&pairmap->chunks[chunk_idx], cidx);
}

// A reader can set from busy, or a new abandoned page can set from clear
bool mi_pairmap_set(mi_pairmap_t* pairmap, size_t pair_idx) {
  const size_t idx = 2*pair_idx;
  mi_assert_internal(idx < MI_PAIRMAP_MAX_BITS);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  // first set the anyset since it is a conservative approximation(increases epoch)
  mi_pairmap_anyset_set(pairmap, chunk_idx/2);
  return mi_bitmap_chunk_set2(&pairmap->chunks[chunk_idx], cidx, NULL);
}

// A busy reader can clear unconditionally
void mi_pairmap_clear(mi_pairmap_t* pairmap, size_t pair_idx) {
  const size_t idx = 2*pair_idx;
  mi_assert_internal(idx < MI_PAIRMAP_MAX_BITS);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  const size_t epoch = mi_pairmap_epoch(pairmap);
  bool both_already_clear = false;
  mi_bitmap_chunk_clear2(&pairmap->chunks[chunk_idx], cidx, &both_already_clear);
  mi_assert_internal(!both_already_clear);  // in our use cases this should not happen
  if (!both_already_clear && epoch == mi_pairmap_epoch(pairmap)) {
    const size_t chunk_idx1 = 2*(chunk_idx/2); // round down to even
    mi_bitmap_chunk_t* chunk1 = &pairmap->chunks[chunk_idx1];
    mi_bitmap_chunk_t* chunk2 = &pairmap->chunks[chunk_idx1 + 1];
    if (mi_bitmap_chunk_all_are_clear(chunk1) && mi_bitmap_chunk_all_are_clear(chunk2)) {
      mi_pairmap_anyset_try_clear(pairmap, chunk_idx1/2, epoch);
    }
  }
}



/* --------------------------------------------------------------------------------
  pairmap clear while not busy
-------------------------------------------------------------------------------- */

static inline bool mi_bfield_atomic_clear_while_not_busy(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal((idx%2)==0); // bit patterns are 00 (clear), 01 (busy), and 11 (set).      
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  const mi_bfield_t mask = ((mi_bfield_t)0x03 << idx);
  const mi_bfield_t mask_busy = ((mi_bfield_t)MI_PAIR_BUSY << idx);
  mi_bfield_t old;
  mi_bfield_t bnew;
  do {
    old = mi_atomic_load_relaxed(b);
    if mi_unlikely((old&mask)==mask_busy) {
      old = mi_atomic_load_acquire(b);
      while ((old&mask)==mask_busy) {  // busy wait
        mi_atomic_yield();
        old = mi_atomic_load_acquire(b);
      }
    }
    bnew = (old & ~mask);  // clear
  } while (!mi_atomic_cas_weak_acq_rel(b, &old, bnew));
  mi_assert_internal((old&mask) != mask_busy);  // we should never clear a busy page
  mi_assert_internal((old&mask) == mask); // in our case: we should only go from set to clear (when reclaiming an abandoned page from a free)
  return true;
}

static void mi_pairmap_chunk_clear_while_not_busy(mi_bitmap_chunk_t* chunk, size_t cidx) {
  mi_assert_internal(cidx < MI_BITMAP_CHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  mi_bfield_atomic_clear_while_not_busy(&chunk->bfields[i], idx);
}

// Used for a page about to be freed to clear itself from the abandoned map; it has to wait
// for all readers to finish reading the page
void mi_pairmap_clear_while_not_busy(mi_pairmap_t* pairmap, size_t pair_idx) {
  const size_t idx = 2*pair_idx;
  mi_assert_internal(idx < MI_PAIRMAP_MAX_BITS);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  const size_t epoch = mi_pairmap_epoch(pairmap);
  mi_pairmap_chunk_clear_while_not_busy(&pairmap->chunks[chunk_idx], cidx);
  if (epoch == mi_pairmap_epoch(pairmap)) {
    const size_t chunk_idx1 = 2*(chunk_idx/2); // round down to even
    mi_bitmap_chunk_t* chunk1 = &pairmap->chunks[chunk_idx1];
    mi_bitmap_chunk_t* chunk2 = &pairmap->chunks[chunk_idx1 + 1];
    if (mi_bitmap_chunk_all_are_clear(chunk1) && mi_bitmap_chunk_all_are_clear(chunk2)) {
      mi_pairmap_anyset_try_clear(pairmap, chunk_idx1/2, epoch);
    }
  }
}


/* --------------------------------------------------------------------------------
  pairmap try and set busy
-------------------------------------------------------------------------------- */

// Atomically go from set to busy, or return false otherwise and leave the bit field as-is.
static inline bool mi_bfield_atomic_try_set_busy(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal((idx%2)==0); // bit patterns are 00 (clear), 01 (busy), and 11 (set).      
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  const mi_bfield_t mask = ((mi_bfield_t)0x03 << idx);
  const mi_bfield_t mask_busy = ((mi_bfield_t)MI_PAIR_BUSY << idx);
  mi_bfield_t old;
  mi_bfield_t bnew;
  do {
    old = mi_atomic_load_relaxed(b);
    if ((old & mask) != mask) return false;  // no longer set
    bnew = (old & ~mask) | mask_busy;
  } while (!mi_atomic_cas_weak_acq_rel(b, &old, bnew));
  return true;
}

static inline bool mi_pairmap_chunk_find_and_set_busy(mi_bitmap_chunk_t* chunk, size_t* pidx) {
  for (int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    size_t idx;
    if mi_unlikely(mi_bfield_find_least_bit(chunk->bfields[i], &idx)) { // find least 1-bit, it may be set or busy
      mi_assert_internal((idx%2)==0); // bit patterns are 00 (clear), 01 (busy), and 11 (set).      
      if mi_likely(mi_bfield_atomic_try_set_busy(&chunk->bfields[i], idx)) {
        *pidx = (i*MI_BFIELD_BITS) + idx;
        mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS-1);
        return true;
      }
    }
  }
  return false;
}

// Used to find an abandoned page, and transition from set to busy.
mi_decl_nodiscard bool mi_pairmap_try_find_and_set_busy(mi_pairmap_t* pairmap, size_t tseq, size_t* pidx) {
  uint32_t  bit_idx;
  uint32_t  start = (uint32_t)(tseq % MI_EPOCHSET_BITS);
  size_t    epoch;
  uint32_t  any_set = mi_pairmap_anyset(pairmap,&epoch);
  any_set = mi_rotr32(any_set, start);
  while (mi_bsf32(any_set,&bit_idx)) { \
    size_t chunk_idx = 2*((bit_idx + start) % MI_BFIELD_BITS);
    {
      // look at chunk_idx and chunck_idx+1
      mi_bitmap_chunk_t* chunk1 = &pairmap->chunks[chunk_idx];
      mi_bitmap_chunk_t* chunk2 = &pairmap->chunks[chunk_idx+1];
      size_t cidx;
      if (mi_pairmap_chunk_find_and_set_busy(chunk1, &cidx)) {
        const size_t idx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
        mi_assert_internal(idx < MI_PAIRMAP_MAX_BITS);
        mi_assert_internal((idx%2)==0);
        *pidx = idx/2;
        return true;
      }
      else if (mi_pairmap_chunk_find_and_set_busy(chunk2, &cidx)) {
        const size_t idx = ((chunk_idx+1) * MI_BITMAP_CHUNK_BITS) + cidx;
        mi_assert_internal(idx < MI_PAIRMAP_MAX_BITS);
        mi_assert_internal((idx%2)==0);
        *pidx = idx/2;
        return true;
      }
      else if (epoch == mi_pairmap_epoch(pairmap) && mi_bitmap_chunk_all_are_clear(chunk1) && mi_bitmap_chunk_all_are_clear(chunk1)) {
        mi_pairmap_anyset_try_clear(pairmap, chunk_idx/2, epoch);
      }
    }
    start += bit_idx+1;    /* so chunk_idx computation stays valid */
    any_set >>= bit_idx;   /* skip scanned bits (and avoid UB with (idx+1)) */
    any_set >>= 1;
  }
  return false;
}
