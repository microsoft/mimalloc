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

static inline mi_bfield_t mi_bfield_zero(void) {
  return 0;
}

static inline mi_bfield_t mi_bfield_one(void) {
  return 1;
}

static inline mi_bfield_t mi_bfield_all_set(void) {
  return ~((mi_bfield_t)0);
}

static inline mi_bfield_t mi_bfield_mask(size_t bit_count, size_t shiftl) {
  mi_assert_internal(bit_count + shiftl <= MI_BFIELD_BITS);
  const mi_bfield_t mask0 = (bit_count < MI_BFIELD_BITS ? (mi_bfield_one() << bit_count)-1 : mi_bfield_all_set());
  return (mask0 << shiftl);
}


// Find the least significant bit that can be xset (0 for MI_BIT_SET, 1 for MI_BIT_CLEAR).
// return false if `x==~0` (for MI_BIT_SET) or `x==0` for MI_BIT_CLEAR (with `*idx` undefined) and true otherwise,
// with the `idx` is set to the bit index (`0 <= *idx < MI_BFIELD_BITS`).
static inline bool mi_bfield_find_least_to_xset(mi_xset_t set, mi_bfield_t x, size_t* idx) {
  return mi_bfield_find_least_bit((set ? ~x : x), idx);
}

// Set a bit atomically. Returns `true` if the bit transitioned from 0 to 1
static inline bool mi_bfield_atomic_set(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = mi_bfield_one()<<idx;
  const mi_bfield_t old = mi_atomic_or_acq_rel(b, mask);
  return ((old&mask) == 0);
}

// Clear a bit atomically. Returns `true` if the bit transitioned from 1 to 0.
static inline bool mi_bfield_atomic_clear(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = mi_bfield_one()<<idx;
  mi_bfield_t old = mi_atomic_and_acq_rel(b, ~mask);
  return ((old&mask) == mask);
}

// Set/clear a bit atomically. Returns `true` if the bit transitioned from 0 to 1 (or 1 to 0).
static inline bool mi_bfield_atomic_xset(mi_xset_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
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
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old|mask)) { };  // try to atomically set the mask bits until success
  if (all_already_set!=NULL) { *all_already_set = ((old&mask)==mask); }
  return ((old&mask) == 0);
}

// Clear a pair of bits atomically, and return true of the mask bits transitioned from all 1's to 0's
static inline bool mi_bfield_atomic_clear2(_Atomic(mi_bfield_t)*b, size_t idx, bool* all_already_clear) {
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  const size_t mask = (mi_bfield_t)0x03 << idx;
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask)) { };  // try to atomically clear the mask bits until success
  if (all_already_clear!=NULL) { *all_already_clear = ((old&mask) == 0); }
  return ((old&mask) == mask);
}

// Set/clear a pair of bits atomically, and return true of the mask bits transitioned from all 0's to 1's (or all 1's to 0's)
static inline bool mi_bfield_atomic_xset2(mi_xset_t set, _Atomic(mi_bfield_t)*b, size_t idx, bool* already_xset) {
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
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old|mask)) { };  // try to atomically set the mask bits until success
  if (already_set!=NULL) { *already_set = mi_bfield_popcount(old&mask); }
  return ((old&mask) == 0);
}

// Clear a mask set of bits atomically, and return true of the mask bits transitioned from all 1's to 0's
static inline bool mi_bfield_atomic_clear_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask, size_t* already_clear) {
  mi_assert_internal(mask != 0);
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask)) { };  // try to atomically clear the mask bits until success
  if (already_clear!=NULL) { *already_clear = mi_bfield_popcount(~(old&mask)); }
  return ((old&mask) == mask);
}

// Set/clear a mask set of bits atomically, and return true of the mask bits transitioned from all 0's to 1's (or all 1's to 0's)
static inline bool mi_bfield_atomic_xset_mask(mi_xset_t set, _Atomic(mi_bfield_t)*b, mi_bfield_t mask, size_t* already_xset) {
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
static inline bool mi_bfield_atomic_try_xset( mi_xset_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
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
static inline bool mi_bfield_atomic_try_xset_mask(mi_xset_t set, _Atomic(mi_bfield_t)* b, mi_bfield_t mask ) {
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
static inline bool mi_bfield_atomic_try_xset8(mi_xset_t set, _Atomic(mi_bfield_t)*b, size_t byte_idx) {
  mi_assert_internal(byte_idx < MI_BFIELD_SIZE);
  const mi_bfield_t mask = ((mi_bfield_t)0xFF)<<(byte_idx*8);
  return mi_bfield_atomic_try_xset_mask(set, b, mask);
}

// Try to set a full field of bits atomically, and return true all bits transitioned from all 0's to 1's.
// and false otherwise leaving the bit field as-is.
static inline bool mi_bfield_atomic_try_setX(_Atomic(mi_bfield_t)*b) {
  mi_bfield_t old = 0;
  return mi_atomic_cas_strong_acq_rel(b, &old, mi_bfield_all_set());
}

// Try to clear a full field of bits atomically, and return true all bits transitioned from all 1's to 0's.
// and false otherwise leaving the bit field as-is.
static inline bool mi_bfield_atomic_try_clearX(_Atomic(mi_bfield_t)*b) {
  mi_bfield_t old = mi_bfield_all_set();
  return mi_atomic_cas_strong_acq_rel(b, &old, mi_bfield_zero());
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
static inline bool mi_bfield_atomic_is_xset_mask(mi_xset_t set, _Atomic(mi_bfield_t)*b, mi_bfield_t mask) {
  mi_assert_internal(mask != 0);
  if (set) {
    return mi_bfield_atomic_is_set_mask(b, mask);
  }
  else {
    return mi_bfield_atomic_is_clear_mask(b, mask);
  }
}


// Check if a bit is set/clear
// static inline bool mi_bfield_atomic_is_xset(mi_xset_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
//   mi_assert_internal(idx < MI_BFIELD_BITS);
//   const mi_bfield_t mask = mi_bfield_one()<<idx;
//   return mi_bfield_atomic_is_xset_mask(set, b, mask);
// }


/* --------------------------------------------------------------------------------
 bitmap chunks
-------------------------------------------------------------------------------- */

// Set/clear 2 (aligned) bits within a chunk.
// Returns true if both bits transitioned from 0 to 1 (or 1 to 0).
static inline bool mi_bitmap_chunk_xset2(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t cidx, bool* all_already_xset) {
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
static bool mi_bitmap_chunk_xsetN(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t cidx, size_t n, size_t* pall_already_xset) {
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
    const mi_bfield_t mask = mi_bfield_mask(m, idx);
    size_t already_xset = 0;
    const bool transition = mi_bfield_atomic_xset_mask(set, &chunk->bfields[field], mask, &already_xset);
    if (already_xset > 0 && transition) {
      _mi_error_message(EFAULT, "ouch\n");
    }
    all_transition = all_transition && transition;
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
static inline bool mi_bitmap_chunk_is_xset2(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t cidx) {
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
static bool mi_bitmap_chunk_is_xsetN(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t cidx, size_t n) {
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(n>0);
  size_t idx = cidx % MI_BFIELD_BITS;
  size_t field = cidx / MI_BFIELD_BITS;
  while (n > 0) {
    size_t m = MI_BFIELD_BITS - idx;   // m is the bits to xset in this field
    if (m > n) { m = n; }
    mi_assert_internal(idx + m <= MI_BFIELD_BITS);
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    const size_t mask = mi_bfield_mask(m, idx);
    if (!mi_bfield_atomic_is_xset_mask(set, &chunk->bfields[field], mask)) {
      return false;
    }
    // next field
    field++;
    idx = 0;
    n -= m;
  }
  return true;
}



static inline bool mi_bitmap_chunk_try_xset(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t cidx) {
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

static inline bool mi_bitmap_chunk_try_xset8(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t byte_idx) {
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
static bool mi_bitmap_chunk_try_xsetN(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t cidx, size_t n) {
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(n>0);
  if (n==0) return true;
  size_t start_idx = cidx % MI_BFIELD_BITS;
  size_t start_field = cidx / MI_BFIELD_BITS;
  size_t end_field = MI_BITMAP_CHUNK_FIELDS;
  mi_bfield_t mask_mid = 0;
  mi_bfield_t mask_end = 0;

  // first field
  size_t field = start_field;
  size_t m = MI_BFIELD_BITS - start_idx;   // m is the bits to xset in this field
  if (m > n) { m = n; }
  mi_assert_internal(start_idx + m <= MI_BFIELD_BITS);
  mi_assert_internal(start_field < MI_BITMAP_CHUNK_FIELDS);
  const mi_bfield_t mask_start = mi_bfield_mask(m, start_idx);
  if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_start)) return false;

  // done?
  n -= m;
  if (n==0) return true;

  // continue with mid fields and last field: if these fail we need to recover by unsetting previous fields

  // mid fields
  while (n >= MI_BFIELD_BITS) {
    field++;
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    mask_mid = mi_bfield_all_set();
    if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_mid)) goto restore;
    n -= MI_BFIELD_BITS;
  }

  // last field
  if (n > 0) {
    mi_assert_internal(n < MI_BFIELD_BITS);
    field++;
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    end_field = field;
    mask_end = mi_bfield_mask(n, 0);
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

#if defined(__AVX2__)
static inline __m256i mi_mm256_zero(void) {
  return _mm256_setzero_si256();
}
static inline __m256i mi_mm256_ones(void) {
  return _mm256_set1_epi64x(~0);
}
static inline bool mi_mm256_is_ones(__m256i vec) {
  return _mm256_testc_si256(vec, _mm256_cmpeq_epi32(vec, vec));
}
static inline bool mi_mm256_is_zero( __m256i vec) {
  return _mm256_testz_si256(vec,vec);
}
#endif

// find least 0/1-bit in a chunk and try to set/clear it atomically
// set `*pidx` to the bit index (0 <= *pidx < MI_BITMAP_CHUNK_BITS) on success.
// todo: try neon version
static inline bool mi_bitmap_chunk_find_and_try_xset(mi_xset_t set, mi_bitmap_chunk_t* chunk, size_t* pidx) {
#if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  while (true) {
    const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vcmp = _mm256_cmpeq_epi64(vec, (set ? mi_mm256_ones() : mi_mm256_zero())); // (elem64 == ~0 / 0 ? 0xFF  : 0)
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
    if ((set ? mi_mm256_is_ones(vec) : mi_mm256_is_zero(vec))) {
      chunk_idx += 4;
      vec = _mm256_load_si256(((const __m256i*)chunk->bfields) + 1);
    }
    const __m256i vcmp = _mm256_cmpeq_epi64(vec, (set ? mi_mm256_ones() : mi_mm256_zero())); // (elem64 == ~0 / 0 ? 0xFF  : 0)
    const uint32_t mask = ~_mm256_movemask_epi8(vcmp);  // mask of most significant bit of each byte (so each 8 bits are all set or clear)
    // mask is inverted, so each 8-bits is 0xFF iff the corresponding elem64 has a zero / one bit (and thus can be set/cleared)
    if (mask==0) return false;
    mi_assert_internal((_tzcnt_u32(mask)%8) == 0); // tzcnt == 0, 8, 16, or 24
    chunk_idx += _tzcnt_u32(mask) / 8;
    #else
    const __m256i vec1  = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vec2  = _mm256_load_si256(((const __m256i*)chunk->bfields)+1);
    const __m256i cmpv  = (set ? mi_mm256_ones() : mi_mm256_zero());
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
    const __m256i vcmp = _mm256_cmpeq_epi8(vec, mi_mm256_ones()); // (byte == ~0 ? -1  : 0)
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


// find a sequence of `n` bits in a chunk with `n < MI_BFIELD_BITS` with all bits set,
// and try to clear them atomically.
// set `*pidx` to its bit index (0 <= *pidx <= MI_BITMAP_CHUNK_BITS - n) on success.
static bool mi_bitmap_chunk_find_and_try_clearNX(mi_bitmap_chunk_t* chunk, size_t n, size_t* pidx) {
  if (n == 0 || n > MI_BFIELD_BITS) return false;
  const mi_bfield_t mask = mi_bfield_mask(n, 0);
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
        b >>= ones;
        bshift += ones;
      }
    }
  }
  return false;
}

// find a sequence of `n` bits in a chunk with `n < MI_BITMAP_CHUNK_BITS` with all bits set,
// and try to clear them atomically.
// set `*pidx` to its bit index (0 <= *pidx <= MI_BITMAP_CHUNK_BITS - n) on success.
static bool mi_bitmap_chunk_find_and_try_clearN_(mi_bitmap_chunk_t* chunk, size_t n, size_t* pidx) {
  if (n == 0 || n > MI_BITMAP_CHUNK_BITS) return false;  // cannot be more than a chunk
  // if (n < MI_BFIELD_BITS) return mi_bitmap_chunk_find_and_try_clearNX(chunk, n, pidx);

  // we align an a field, and require `field_count` fields to be all clear.
  // n >= MI_BFIELD_BITS; find a first field that is 0
  const size_t field_count = _mi_divide_up(n, MI_BFIELD_BITS);  // we need this many fields
  for (size_t i = 0; i <= MI_BITMAP_CHUNK_FIELDS - field_count; i++)
  {
    // first pre-scan for a range of fields that are all set
    bool allset = true;
    size_t j = 0;
    do {
      mi_assert_internal(i + j < MI_BITMAP_CHUNK_FIELDS);
      mi_bfield_t b = mi_atomic_load_relaxed(&chunk->bfields[i+j]);
      if (~b != 0) {
        allset = false;
        i += j;  // no need to look again at the previous fields
        break;
      }
    } while (++j < field_count);

    // if all set, we can try to atomically clear them
    if (allset) {
      const size_t cidx = i*MI_BFIELD_BITS;
      if (mi_bitmap_chunk_try_clearN(chunk, cidx, n)) {
        // we cleared all atomically
        *pidx = cidx;
        mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
        mi_assert_internal(*pidx + n <= MI_BITMAP_CHUNK_BITS);
        return true;
      }
    }
  }
  return false;
}


static inline bool mi_bitmap_chunk_find_and_try_clearN(mi_bitmap_chunk_t* chunk, size_t n, size_t* pidx) {
  if (n==1) return mi_bitmap_chunk_find_and_try_clear(chunk, pidx);
  if (n==8) return mi_bitmap_chunk_find_and_try_clear8(chunk, pidx);
  if (n == 0 || n > MI_BITMAP_CHUNK_BITS) return false;  // cannot be more than a chunk
  if (n < MI_BFIELD_BITS) return mi_bitmap_chunk_find_and_try_clearNX(chunk, n, pidx);
  return mi_bitmap_chunk_find_and_try_clearN_(chunk, n, pidx);
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
  return mi_mm256_is_zero(vec);
  #elif defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==512)
  const __m256i vec1 = _mm256_load_si256((const __m256i*)chunk->bfields);
  if (!mi_mm256_is_zero(vec1)) return false;
  const __m256i vec2 = _mm256_load_si256(((const __m256i*)chunk->bfields)+1);
  return (mi_mm256_is_zero(vec2));
  #else
  for(int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    if (chunk->bfields[i] != 0) return false;
  }
  return true;
  #endif
}

/* --------------------------------------------------------------------------------
  chunkmap (for now for 32-bit sets only)
-------------------------------------------------------------------------------- */

static void mi_chunkmap_split(mi_chunkmap_t es, mi_cmap_t* cmap, mi_epoch_t* epoch) {
  *cmap = (mi_cmap_t)es;
  *epoch = (mi_epoch_t)(es >> 32);
}

static mi_chunkmap_t mi_chunkmap_join(mi_cmap_t cmap, mi_epoch_t epoch) {
  return ((mi_chunkmap_t)epoch << MI_CHUNKMAP_BITS) | cmap;
}

// setting a bit increases the epoch
static void mi_chunkmap_set(_Atomic(mi_chunkmap_t)* cm, size_t idx) {
  mi_assert(idx < MI_CHUNKMAP_BITS);
  mi_epoch_t  epoch;
  mi_cmap_t   cmap;
  mi_chunkmap_t cm_new;
  mi_chunkmap_t cm_old = mi_atomic_load_relaxed(cm);
  do {
    mi_chunkmap_split(cm_old, &cmap, &epoch);
    cm_new = mi_chunkmap_join(cmap | (((mi_cmap_t)1)<<idx), epoch+1);
  } while (!mi_atomic_cas_weak_acq_rel(cm, &cm_old, cm_new));
}

// clear-ing a bit only works if the epoch didn't change (so we never clear unintended)
static bool mi_chunkmap_try_clear(_Atomic(mi_chunkmap_t)* cm, size_t idx, mi_epoch_t expected_epoch) {
  mi_assert(idx < MI_CHUNKMAP_BITS);
  mi_epoch_t epoch;
  mi_cmap_t  cmap;
  mi_chunkmap_t cm_new;
  mi_chunkmap_t cm_old = mi_atomic_load_relaxed(cm);
  do {
    mi_chunkmap_split(cm_old, &cmap, &epoch);
    if (epoch != expected_epoch) return false;
    cm_new = mi_chunkmap_join(cmap & ~(((mi_cmap_t)1)<<idx), epoch);  // no need to increase the epoch for clearing
  } while (!mi_atomic_cas_weak_acq_rel(cm, &cm_old, cm_new));
  return true;
}

/* --------------------------------------------------------------------------------
 bitmap chunkmap
-------------------------------------------------------------------------------- */

static void mi_bitmap_chunkmap_set(mi_bitmap_t* bitmap, size_t chunk_idx) {
  mi_assert(chunk_idx < mi_bitmap_chunk_count(bitmap));
  const size_t cmidx = chunk_idx / MI_CHUNKMAP_BITS;
  const size_t idx = chunk_idx % MI_CHUNKMAP_BITS;
  mi_chunkmap_set(&bitmap->chunk_maps[cmidx], idx);
}

static bool mi_bitmap_chunkmap_try_clear(mi_bitmap_t* bitmap, size_t chunk_idx, mi_epoch_t epoch) {
  mi_assert(chunk_idx < mi_bitmap_chunk_count(bitmap));
  const size_t cmidx = chunk_idx / MI_CHUNKMAP_BITS;
  const size_t idx = chunk_idx % MI_CHUNKMAP_BITS;
  return mi_chunkmap_try_clear(&bitmap->chunk_maps[cmidx], idx, epoch);
}

static mi_cmap_t mi_bitmap_chunkmap(mi_bitmap_t* bitmap, size_t chunk_idx, mi_epoch_t* epoch) {
  mi_assert(chunk_idx < mi_bitmap_chunk_count(bitmap));
  const size_t cmidx = chunk_idx / MI_CHUNKMAP_BITS;
  mi_assert_internal(cmidx < bitmap->chunk_map_count);
  mi_cmap_t cmap;
  mi_chunkmap_split(mi_atomic_load_relaxed(&bitmap->chunk_maps[cmidx]), &cmap, epoch);
  return cmap;
}

static mi_epoch_t mi_bitmap_chunkmap_epoch(mi_bitmap_t* bitmap, size_t chunk_idx) {
  mi_epoch_t epoch;
  mi_bitmap_chunkmap(bitmap, chunk_idx, &epoch);
  return epoch;
}

/* --------------------------------------------------------------------------------
 bitmap
-------------------------------------------------------------------------------- */

size_t mi_bitmap_size(size_t bit_count, size_t* pchunk_count) {
  mi_assert_internal((bit_count % MI_BITMAP_CHUNK_BITS) == 0);
  bit_count = _mi_align_up(bit_count, MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(bit_count <= MI_BITMAP_MAX_BIT_COUNT);
  mi_assert_internal(bit_count > 0);
  const size_t chunk_count = bit_count / MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(chunk_count >= 1);
  const size_t size = offsetof(mi_bitmap_t,chunks) + (chunk_count * MI_BITMAP_CHUNK_SIZE);
  mi_assert_internal( (size%MI_BITMAP_CHUNK_SIZE) == 0 );
  if (pchunk_count != NULL) { *pchunk_count = chunk_count;  }
  return size;
}

// initialize a bitmap to all unset; avoid a mem_zero if `already_zero` is true
// returns the size of the bitmap
size_t mi_bitmap_init(mi_bitmap_t* bitmap, size_t bit_count, bool already_zero) {
  size_t chunk_count;
  const size_t size = mi_bitmap_size(bit_count, &chunk_count);
  if (!already_zero) {
    _mi_memzero_aligned(bitmap, size);
  }
  bitmap->chunk_map_count = _mi_divide_up(chunk_count, MI_CHUNKMAP_BITS);
  mi_assert_internal(bitmap->chunk_map_count <= MI_BITMAP_MAX_CHUNKMAPS);
  bitmap->chunk_count = chunk_count;
  mi_assert_internal(bitmap->chunk_map_count <= MI_BITMAP_MAX_CHUNK_COUNT);
  return size;
}

// Set a sequence of `n` bits in the bitmap (and can cross chunks). Not atomic so only use if local to a thread.
void mi_bitmap_unsafe_setN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(idx + n <= mi_bitmap_max_bits(bitmap));

  // first chunk
  size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  size_t m = MI_BITMAP_CHUNK_BITS - cidx;
  if (m > n) { m = n; }
  mi_bitmap_chunk_setN(&bitmap->chunks[chunk_idx], cidx, m, NULL);
  mi_bitmap_chunkmap_set(bitmap, chunk_idx);

  // n can be large so use memset for efficiency for all in-between chunks
  chunk_idx++;
  n -= m;
  const size_t mid_chunks = n / MI_BITMAP_CHUNK_BITS;
  if (mid_chunks > 0) {
    _mi_memset(&bitmap->chunks[chunk_idx], ~0, mid_chunks * MI_BITMAP_CHUNK_SIZE);
    const size_t end_chunk = chunk_idx + mid_chunks;
    while (chunk_idx < end_chunk) {
      mi_bitmap_chunkmap_set(bitmap, chunk_idx);
      chunk_idx++;
    }
    n -= (mid_chunks * MI_BITMAP_CHUNK_BITS);
  }

  // last chunk
  if (n > 0) {
    mi_assert_internal(n < MI_BITMAP_CHUNK_BITS);
    mi_assert_internal(chunk_idx < MI_BITMAP_CHUNK_FIELDS);
    mi_bitmap_chunk_setN(&bitmap->chunks[chunk_idx], 0, n, NULL);
    mi_bitmap_chunkmap_set(bitmap, chunk_idx);
  }
}


// Try to set/clear a bit in the bitmap; returns `true` if atomically transitioned from 0 to 1 (or 1 to 0),
// and false otherwise leaving the bitmask as is.
static bool mi_bitmap_try_xset(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < mi_bitmap_max_bits(bitmap));
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (set) {
    // first set the chunkmap since it is a conservative approximation (increases epoch)
    mi_bitmap_chunkmap_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_try_set(&bitmap->chunks[chunk_idx], cidx);
  }
  else {
    const mi_epoch_t epoch = mi_bitmap_chunkmap_epoch(bitmap, chunk_idx);
    bool cleared = mi_bitmap_chunk_try_clear(&bitmap->chunks[chunk_idx], cidx);
    if (cleared && epoch == mi_bitmap_chunkmap_epoch(bitmap, chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
    }
    return cleared;
  }
}

// Try to set/clear a byte in the bitmap; returns `true` if atomically transitioned from 0 to 0xFF (or 0xFF to 0)
// and false otherwise leaving the bitmask as is.
static bool mi_bitmap_try_xset8(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < mi_bitmap_max_bits(bitmap));
  mi_assert_internal(idx%8 == 0);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t byte_idx  = (idx % MI_BITMAP_CHUNK_BITS)/8;
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));

  if (set) {
    // first set the anyset since it is a conservative approximation (increases epoch)
    mi_bitmap_chunkmap_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_try_set8(&bitmap->chunks[chunk_idx], byte_idx);
  }
  else {
    const mi_epoch_t epoch = mi_bitmap_chunkmap_epoch(bitmap,chunk_idx);
    bool cleared = mi_bitmap_chunk_try_clear8(&bitmap->chunks[chunk_idx], byte_idx);
    if (cleared && epoch == mi_bitmap_chunkmap_epoch(bitmap,chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
    }
    return cleared;
  }
}


// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's)
// and false otherwise leaving the bitmask as is.
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
static bool mi_bitmap_try_xsetN_(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(idx + n <= mi_bitmap_max_bits(bitmap));
  if (n==0 || idx + n > mi_bitmap_max_bits(bitmap)) return false;

  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (cidx + n > MI_BITMAP_CHUNK_BITS) { n = MI_BITMAP_CHUNK_BITS - cidx; }  // paranoia

  if (set) {
    // first set the chunkmap since it is a conservative approximation (increases epoch)
    mi_bitmap_chunkmap_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_try_setN(&bitmap->chunks[chunk_idx], cidx, n);
  }
  else {
    const mi_epoch_t epoch = mi_bitmap_chunkmap_epoch(bitmap,chunk_idx);
    bool cleared = mi_bitmap_chunk_try_clearN(&bitmap->chunks[chunk_idx], cidx, n);
    if (cleared && epoch == mi_bitmap_chunkmap_epoch(bitmap,chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
    }
    return cleared;
  }
}

mi_decl_nodiscard bool mi_bitmap_try_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0 && n<=MI_BITMAP_CHUNK_BITS);
  if (n==1) return mi_bitmap_try_xset(set, bitmap, idx);
  if (n==8) return mi_bitmap_try_xset8(set, bitmap, idx);
  // todo: add 32/64 for large pages
  return mi_bitmap_try_xsetN_(set, bitmap, idx, n);
}

// Set/clear a sequence of 2 bits that were on an even `idx` in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
static bool mi_bitmap_xset_pair(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal((idx%2)==0);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + 2 <= MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));

  if (set) {
    // first set the chunkmap since it is a conservative approximation (increases epoch)
    mi_bitmap_chunkmap_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_set2(&bitmap->chunks[chunk_idx], cidx, NULL);
  }
  else {
    const mi_epoch_t epoch = mi_bitmap_chunkmap_epoch(bitmap, chunk_idx);
    bool already_clear = false;
    const bool allset = mi_bitmap_chunk_clear2(&bitmap->chunks[chunk_idx], cidx, &already_clear);
    if (!already_clear && epoch == mi_bitmap_chunkmap_epoch(bitmap, chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
    }
    return allset;
  }
}

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
static bool mi_bitmap_xsetN_(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, size_t* already_xset ) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BITMAP_CHUNK_BITS);

  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (cidx + n > MI_BITMAP_CHUNK_BITS) { n = MI_BITMAP_CHUNK_BITS - cidx; }  // paranoia

  if (set) {
    // first set the chunkmap since it is a conservative approximation (increases epoch)
    mi_bitmap_chunkmap_set(bitmap, chunk_idx);
    // then actually try to set it atomically
    return mi_bitmap_chunk_setN(&bitmap->chunks[chunk_idx], cidx, n, already_xset);
  }
  else {
    const mi_epoch_t epoch = mi_bitmap_chunkmap_epoch(bitmap,chunk_idx);
    size_t already_clear = 0;
    const bool allset = mi_bitmap_chunk_clearN(&bitmap->chunks[chunk_idx], cidx, n, &already_clear);
    if (already_xset != NULL) { *already_xset = already_clear; }
    if (already_clear < n && epoch == mi_bitmap_chunkmap_epoch(bitmap,chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
    }
    return allset;
  }
}

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
bool mi_bitmap_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, size_t* already_xset) {
  mi_assert_internal(n>0 && n<=MI_BITMAP_CHUNK_BITS);
  //TODO: specialize?
  //if (n==1) return mi_bitmap_xset(set, bitmap, idx);
  //if (n==2) return mi_bitmap_xset(set, bitmap, idx);
  //if (n==8) return mi_bitmap_xset8(set, bitmap, idx);
  return mi_bitmap_xsetN_(set, bitmap, idx, n, already_xset);
}


// Is a sequence of 2 bits already all set/cleared?
static inline bool mi_bitmap_is_xset2(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx + 2 <= mi_bitmap_max_bits(bitmap));
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + 2 <= MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  return mi_bitmap_chunk_is_xset2(set, &bitmap->chunks[chunk_idx], cidx);
}


// Is a sequence of n bits already all set/cleared?
bool mi_bitmap_is_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(idx + n <= mi_bitmap_max_bits(bitmap));

  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (cidx + n > MI_BITMAP_CHUNK_BITS) { n = MI_BITMAP_CHUNK_BITS - cidx; }  // paranoia

  return mi_bitmap_chunk_is_xsetN(set, &bitmap->chunks[chunk_idx], cidx, n);
}


/* --------------------------------------------------------------------------------
  bitmap try_find_and_clear
-------------------------------------------------------------------------------- */
/*
typedef bool (mi_bitmap_find_fun_t)(mi_bitmap_t* bitmap, size_t n, size_t chunk_idx, mi_epoch_t epoch, size_t* pidx);

static inline bool mi_bitmap_try_find(mi_bitmap_t* bitmap, size_t n, size_t tseq, size_t* pidx, mi_bitmap_find_fun_t* find_fun)
{
  if (n == 0 || n > MI_BITMAP_CHUNK_BITS) return false;

  // start chunk index -- todo: can depend on the tseq to decrease contention between threads
  MI_UNUSED(tseq);
  const size_t chunk_start = 0;
  const size_t chunk_map_start = chunk_start / MI_CHUNKMAP_BITS;
  const size_t chunk_map_start_idx = chunk_start % MI_CHUNKMAP_BITS;

  // for each chunkmap entry `i`
  for( size_t _i = 0; _i < bitmap->chunk_map_count; _i++)
  {
    size_t i = (_i + chunk_map_start);
    if (i > bitmap->chunk_map_count) i -= bitmap->chunk_map_count;  // adjust for the start position

    const size_t chunk_idx0 = i*MI_CHUNKMAP_BITS;
    mi_epoch_t epoch;
    mi_cmap_t  cmap = mi_bitmap_chunkmap(bitmap, chunk_idx0, &epoch);
    if (_i == 0) { cmap = mi_rotr32(cmap, chunk_map_start_idx); }   // rotate right for the start position (on the first iteration)

    uint32_t cmap_idx;             // one bit set of each chunk that may have bits set
    size_t cmap_idx_shift = 0;     // shift through the cmap
    while (mi_bsf32(cmap, &cmap_idx)) {     // find least bit that is set
      // adjust for the start position
      if (_i == 0) { cmap_idx = (cmap_idx + chunk_map_start_idx) % MI_CHUNKMAP_BITS; }
      // set the chunk idx
      const size_t chunk_idx = chunk_idx0 + cmap_idx + cmap_idx_shift;

      // try to find and clear N bits in that chunk
      if (chunk_idx < mi_bitmap_chunk_count(bitmap)) {   // we can have less chunks than in the chunkmap..
        if ((*find_fun)(bitmap, n, chunk_idx, epoch, pidx)) {
          return true;
        }
      }

      // skip to the next bit
      cmap_idx_shift += cmap_idx+1;
      cmap >>= cmap_idx;            // skip scanned bits (and avoid UB for `cmap_idx+1`)
      cmap >>= 1;
    }
  }

  return false;
}
*/

#define mi_bitmap_forall_chunks(bitmap, tseq, name_epoch, name_chunk_idx) \
  { \
  /* start chunk index -- todo: can depend on the tseq to decrease contention between threads */ \
  MI_UNUSED(tseq); \
  const size_t chunk_start = 0; \
  const size_t chunk_map_start = chunk_start / MI_CHUNKMAP_BITS; \
  const size_t chunk_map_start_idx = chunk_start % MI_CHUNKMAP_BITS; \
  /* for each chunkmap entry `i` */ \
  for (size_t _i = 0; _i < bitmap->chunk_map_count; _i++) { \
    size_t i = (_i + chunk_map_start); \
    if (i > bitmap->chunk_map_count) i -= bitmap->chunk_map_count;  /* adjust for the start position */ \
    \
    const size_t chunk_idx0 = i*MI_CHUNKMAP_BITS; \
    mi_epoch_t name_epoch; \
    mi_cmap_t  cmap = mi_bitmap_chunkmap(bitmap, chunk_idx0, &name_epoch); \
    if (_i == 0) { cmap = mi_rotr32(cmap, chunk_map_start_idx); }   /* rotate right for the start position (on the first iteration) */ \
    \
    uint32_t cmap_idx;             /* one bit set of each chunk that may have bits set */ \
    size_t   cmap_idx_shift = 0;   /* shift through the cmap */ \
    while (mi_bsf32(cmap, &cmap_idx)) {     /* find least bit that is set */ \
      /* adjust for the start position again */ \
      if (_i == 0) { cmap_idx = (cmap_idx + chunk_map_start_idx) % MI_CHUNKMAP_BITS; } \
      /* set the chunk idx */ \
      const size_t name_chunk_idx = chunk_idx0 + cmap_idx + cmap_idx_shift; \
      /* try to find and clear N bits in that chunk */ \
      if (name_chunk_idx < mi_bitmap_chunk_count(bitmap)) {   /* we can have less chunks than in the chunkmap.. */ 

#define mi_bitmap_forall_chunks_end() \
      } \
      /* skip to the next bit */ \
      cmap_idx_shift += cmap_idx+1; \
      cmap >>= cmap_idx;            /* skip scanned bits (and avoid UB for `cmap_idx+1`) */ \
      cmap >>= 1; \
    } \
  }}
   
//static bool mi_bitmap_try_find_and_clearN_at(mi_bitmap_t* bitmap, size_t n, size_t chunk_idx, mi_epoch_t epoch, size_t* pidx) {
//  size_t cidx;
//  if mi_likely(mi_bitmap_chunk_find_and_try_clearN(&bitmap->chunks[chunk_idx], n, &cidx)) {
//    *pidx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
//    mi_assert_internal(*pidx <= mi_bitmap_max_bits(bitmap) - n);
//    return true;
//  }
//  else {
//    // we may find that all are cleared only on a second iteration but that is ok as
//    // the chunkmap is a conservative approximation.
//    if (epoch == mi_bitmap_chunkmap_epoch(bitmap, chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
//      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
//    }
//    return false;
//  }
//}

// Find a sequence of `n` bits in the bitmap with all bits set, and atomically unset all.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-n`.
mi_decl_nodiscard bool mi_bitmap_try_find_and_clearN(mi_bitmap_t* bitmap, size_t n, size_t tseq, size_t* pidx)
{
  // return mi_bitmap_try_find(bitmap, n, tseq, pidx, &mi_bitmap_try_find_and_clearN_at);
  mi_bitmap_forall_chunks(bitmap, tseq, epoch, chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bitmap_chunk_find_and_try_clearN(&bitmap->chunks[chunk_idx], n, &cidx)) {
      *pidx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
      mi_assert_internal(*pidx <= mi_bitmap_max_bits(bitmap) - n);
      return true;
    }
    else {
      // we may find that all are cleared only on a second iteration but that is ok as
      // the chunkmap is a conservative approximation.
      if (epoch == mi_bitmap_chunkmap_epoch(bitmap, chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
        mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
      }
      // continue
    }
  }
  mi_bitmap_forall_chunks_end();
  return false;
}

/* --------------------------------------------------------------------------------
  pairmap
-------------------------------------------------------------------------------- */

void mi_pairmap_init(mi_pairmap_t* pairmap, mi_bitmap_t* bm1, mi_bitmap_t* bm2) {
  mi_assert_internal(mi_bitmap_chunk_count(bm1)==mi_bitmap_chunk_count(bm2));
  pairmap->bitmap1 = bm1;
  pairmap->bitmap2 = bm2;
}

static void mi_pairmap_from_pair_idx(mi_pairmap_t* pairmap, size_t pair_idx, mi_bitmap_t** bitmap, size_t* pidx) {
  const size_t idx = 2*pair_idx;
  const size_t maxbits = mi_bitmap_max_bits(pairmap->bitmap1);
  mi_assert_internal(pair_idx < maxbits);
  if (idx < maxbits) {
    *bitmap = pairmap->bitmap1;
    *pidx = idx;
  }
  else {
    *bitmap = pairmap->bitmap2;
    *pidx = idx - maxbits;
  }
}

bool mi_pairmap_set(mi_pairmap_t* pairmap, size_t pair_idx) {
  mi_bitmap_t* bitmap;
  size_t idx;
  mi_pairmap_from_pair_idx(pairmap, pair_idx, &bitmap, &idx);
  return mi_bitmap_xset_pair(MI_BIT_SET, bitmap, idx);
}

bool mi_pairmap_clear(mi_pairmap_t* pairmap, size_t pair_idx) {
  mi_bitmap_t* bitmap;
  size_t idx;
  mi_pairmap_from_pair_idx(pairmap, pair_idx, &bitmap, &idx);
  return mi_bitmap_xset_pair(MI_BIT_CLEAR, bitmap, idx);
}

bool mi_pairmap_is_clear(mi_pairmap_t* pairmap, size_t pair_idx) {
  mi_bitmap_t* bitmap;
  size_t idx;
  mi_pairmap_from_pair_idx(pairmap, pair_idx, &bitmap, &idx);
  return mi_bitmap_is_xset2(MI_BIT_CLEAR, bitmap, idx);
}



/* --------------------------------------------------------------------------------
  pairmap clear while not busy
-------------------------------------------------------------------------------- */

static inline bool mi_bfield_atomic_clear2_once_not_busy(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal((idx%2)==0); // bit patterns are 00 (clear), 10 (busy), and 11 (set).
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  const mi_bfield_t mask = ((mi_bfield_t)MI_PAIR_SET << idx);
  const mi_bfield_t mask_busy = ((mi_bfield_t)MI_PAIR_BUSY << idx);
  mi_bfield_t bnew;
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  do {
    if mi_unlikely((old&mask)==mask_busy) {
      old = mi_atomic_load_acquire(b);
      if ((old&mask)==mask_busy) { _mi_stat_counter_increase(&_mi_stats_main.pages_unabandon_busy_wait, 1); }
      while ((old&mask)==mask_busy) {  // busy wait
        mi_atomic_yield();
        old = mi_atomic_load_acquire(b);
      }
    }
    bnew = (old & ~mask);  // clear
  } while (!mi_atomic_cas_weak_acq_rel(b, &old, bnew));
  mi_assert_internal((old&mask) != mask_busy);  // we should never clear a busy page
  mi_assert_internal((old&mask) == mask); // in our case: we should only go from set to clear (when reclaiming an abandoned page from a free)
  return ((old&mask) == mask);
}

static inline bool mi_bitmap_chunk_clear2_once_not_busy(mi_bitmap_chunk_t* chunk, size_t cidx) {
  mi_assert_internal(cidx < MI_BITMAP_CHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  return mi_bfield_atomic_clear2_once_not_busy(&chunk->bfields[i], idx);
}

static bool mi_bitmap_clear2_once_not_busy(mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal((idx%2)==0);
  mi_assert_internal(idx < mi_bitmap_max_bits(bitmap));
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  const mi_epoch_t epoch = mi_bitmap_chunkmap_epoch(bitmap, chunk_idx);
  bool cleared = mi_bitmap_chunk_clear2_once_not_busy(&bitmap->chunks[chunk_idx], cidx);
  if (cleared && epoch == mi_bitmap_chunkmap_epoch(bitmap, chunk_idx) && mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
    mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx, epoch);
  }
  return cleared;
}

void mi_pairmap_clear_once_not_busy(mi_pairmap_t* pairmap, size_t pair_idx) {
  mi_bitmap_t* bitmap;
  size_t idx;
  mi_pairmap_from_pair_idx(pairmap, pair_idx, &bitmap, &idx);
  mi_bitmap_clear2_once_not_busy(bitmap, idx);
}



/* --------------------------------------------------------------------------------
  pairmap try and set busy
-------------------------------------------------------------------------------- */

// Atomically go from set to busy, or return false otherwise and leave the bit field as-is.
static inline bool mi_bfield_atomic_try_set_busy(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal((idx%2)==0); // bit patterns are 00 (clear), 10 (busy), and 11 (set).
  mi_assert_internal(idx < MI_BFIELD_BITS-1);
  const mi_bfield_t mask = ((mi_bfield_t)MI_PAIR_SET << idx);
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

static inline bool mi_bitmap_chunk_try_find_and_set_busy(mi_bitmap_chunk_t* chunk, size_t* pidx) {
  for (int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    while (true) {
      const mi_bfield_t b = mi_atomic_load_relaxed(&chunk->bfields[i]) & MI_BFIELD_LO_BIT2; // only keep MI_PAIR_SET bits
      size_t idx;
      if (!mi_bfield_find_least_bit(b, &idx)) { // find least 1-bit
        break; // not found: continue with the next field
      }
      else {
        mi_assert_internal((idx%2)==0);
        if mi_likely(mi_bfield_atomic_try_set_busy(&chunk->bfields[i], idx)) {
          *pidx = (i*MI_BFIELD_BITS) + idx;
          mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS-1);
          return true;
        }
        // else: try this word once again
      }
    }
  }
  return false;
}


static bool mi_bitmap_try_find_and_set_busy(mi_bitmap_t* bitmap, size_t n, size_t tseq, size_t idx_offset, size_t* ppair_idx,
                                            mi_bitmap_claim_while_busy_fun_t* claim, void* arg1, void* arg2) 
{
  mi_bitmap_forall_chunks(bitmap, tseq, epoch, chunk_idx)
  {
    MI_UNUSED(epoch); MI_UNUSED(n);
    mi_assert_internal(n==2);
    size_t cidx;
    if mi_likely(mi_bitmap_chunk_try_find_and_set_busy(&bitmap->chunks[chunk_idx], &cidx)) {
      const size_t idx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
      mi_assert_internal((idx%2)==0);
      const size_t pair_idx = (idx + idx_offset)/2;
      if (claim(pair_idx, arg1, arg2)) { // while busy, the claim function can read from the page
        mi_bitmap_xset_pair(MI_BIT_CLEAR, bitmap, idx); // claimed, clear the entry
        *ppair_idx = pair_idx;
        return true;
      }
      else {
        mi_bitmap_xset_pair(MI_BIT_SET, bitmap, idx); // not claimed, reset the entry
        // and continue
      }
    }
  }
  mi_bitmap_forall_chunks_end();
  return false;
}

// Used to find an abandoned page, and transition from set to busy.
mi_decl_nodiscard bool mi_pairmap_try_find_and_set_busy(mi_pairmap_t* pairmap, size_t tseq, size_t* pair_idx, 
                                                        mi_bitmap_claim_while_busy_fun_t* claim, void* arg1, void* arg2 ) {
  if (mi_bitmap_try_find_and_set_busy(pairmap->bitmap1, 2, tseq, 0, pair_idx, claim, arg1, arg2)) return true;
  return mi_bitmap_try_find_and_set_busy(pairmap->bitmap2, 2, tseq, mi_bitmap_max_bits(pairmap->bitmap1), pair_idx, claim, arg1, arg2);  
}
