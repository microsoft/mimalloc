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
// `all_clear` is set if the new bfield is zero.
static inline bool mi_bfield_atomic_clear(_Atomic(mi_bfield_t)*b, size_t idx, bool* all_clear) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = mi_bfield_one()<<idx;
  mi_bfield_t old = mi_atomic_and_acq_rel(b, ~mask);
  if (all_clear != NULL) { *all_clear = ((old&~mask)==0); }
  return ((old&mask) == mask);
}

// Clear a bit but only when/once it is set. This is used by concurrent free's while
// the page is abandoned and mapped. 
static inline void mi_bfield_atomic_clear_once_set(_Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = mi_bfield_one()<<idx;
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  do {
    if mi_unlikely((old&mask) == 0) {
      old = mi_atomic_load_acquire(b);
      if ((old&mask)==0) { _mi_stat_counter_increase(&_mi_stats_main.pages_unabandon_busy_wait, 1); }
      while ((old&mask)==0) { // busy wait
        mi_atomic_yield();
        old = mi_atomic_load_acquire(b);
      }
    }
  } while (!mi_atomic_cas_weak_acq_rel(b,&old, (old&~mask)));  
  mi_assert_internal((old&mask)==mask);  // we should only clear when it was set
}

// Set/clear a bit atomically. Returns `true` if the bit transitioned from 0 to 1 (or 1 to 0).
static inline bool mi_bfield_atomic_xset(mi_xset_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
  if (set) {
    return mi_bfield_atomic_set(b, idx);
  }
  else {
    return mi_bfield_atomic_clear(b, idx, NULL);
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
// `all_clear` is set to true if the new bfield is zero (and false otherwise)
static inline bool mi_bfield_atomic_try_clear(_Atomic(mi_bfield_t)*b, size_t idx, bool* all_clear) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = mi_bfield_one()<<idx;
  const mi_bfield_t old = mi_atomic_and_acq_rel(b, ~mask);
  if (all_clear != NULL) { *all_clear = ((old&~mask)==0); }
  return ((old&mask) == mask);
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
static inline bool mi_bfield_atomic_try_clear_mask(_Atomic(mi_bfield_t)*b, mi_bfield_t mask, bool* all_clear) {
  mi_assert_internal(mask != 0);
  mi_bfield_t old = mi_atomic_load_relaxed(b);
  do {
    if ((old&mask) != mask) {  
      // the mask bits are no longer set
      if (all_clear != NULL) { *all_clear = (old==0); }
      return false; 
    }
  } while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask));  // try to atomically clear the mask bits
  if (all_clear != NULL) { *all_clear = ((old&~mask) == 0); }
  return true;
}


// Tries to (un)set a mask atomically, and returns true if the mask bits atomically transitioned from 0 to mask (or mask to 0)
// and false otherwise (leaving the bit field as is).
static inline bool mi_bfield_atomic_try_xset_mask(mi_xset_t set, _Atomic(mi_bfield_t)* b, mi_bfield_t mask, bool* all_clear ) {
  mi_assert_internal(mask != 0);
  if (set) {
    if (all_clear != NULL) { *all_clear = false; }
    return mi_bfield_atomic_try_set_mask(b, mask);
  }
  else {
    return mi_bfield_atomic_try_clear_mask(b, mask, all_clear);
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
static inline bool mi_bfield_atomic_try_clear8(_Atomic(mi_bfield_t)*b, size_t byte_idx, bool* all_clear) {
  mi_assert_internal(byte_idx < MI_BFIELD_SIZE);
  const mi_bfield_t mask = ((mi_bfield_t)0xFF)<<(byte_idx*8);
  return mi_bfield_atomic_try_clear_mask(b, mask, all_clear);
}

//// Tries to set/clear a byte atomically, and returns true if the byte atomically transitioned from 0 to 0xFF (or 0xFF to 0)
//// and false otherwise (leaving the bit field as is).
//static inline bool mi_bfield_atomic_try_xset8(mi_xset_t set, _Atomic(mi_bfield_t)*b, size_t byte_idx) {
//  mi_assert_internal(byte_idx < MI_BFIELD_SIZE);
//  const mi_bfield_t mask = ((mi_bfield_t)0xFF)<<(byte_idx*8);
//  return mi_bfield_atomic_try_xset_mask(set, b, mask);
//}


// Try to set a full field of bits atomically, and return true all bits transitioned from all 0's to 1's.
// and false otherwise leaving the bit field as-is.
static inline bool mi_bfield_atomic_try_setX(_Atomic(mi_bfield_t)*b) {
  mi_bfield_t old = 0;
  return mi_atomic_cas_weak_acq_rel(b, &old, mi_bfield_all_set());
}

// Try to clear a full field of bits atomically, and return true all bits transitioned from all 1's to 0's.
// and false otherwise leaving the bit field as-is.
static inline bool mi_bfield_atomic_try_clearX(_Atomic(mi_bfield_t)*b) {
  mi_bfield_t old = mi_bfield_all_set();
  return mi_atomic_cas_weak_acq_rel(b, &old, mi_bfield_zero());
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

// ------ xset --------

//static inline bool mi_bchunk_xset(mi_xset_t set, mi_bchunk_t* chunk, size_t cidx) {
//  mi_assert_internal(cidx < MI_BCHUNK_BITS);
//  const size_t i = cidx / MI_BFIELD_BITS;
//  const size_t idx = cidx % MI_BFIELD_BITS;
//  return mi_bfield_atomic_xset(set, &chunk->bfields[i], idx);
//}

static inline bool mi_bchunk_set(mi_bchunk_t* chunk, size_t cidx) {
  mi_assert_internal(cidx < MI_BCHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  return mi_bfield_atomic_set(&chunk->bfields[i], idx);
}

static inline bool mi_bchunk_clear(mi_bchunk_t* chunk, size_t cidx, bool* maybe_all_clear) {
  mi_assert_internal(cidx < MI_BCHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  return mi_bfield_atomic_clear(&chunk->bfields[i], idx, maybe_all_clear);
}


// Set/clear a sequence of `n` bits within a chunk.
// Returns true if all bits transitioned from 0 to 1 (or 1 to 0).
static bool mi_bchunk_xsetN(mi_xset_t set, mi_bchunk_t* chunk, size_t cidx, size_t n, size_t* palready_xset) {
  mi_assert_internal(cidx + n <= MI_BCHUNK_BITS);
  mi_assert_internal(n>0);
  bool all_transition = true;
  size_t total_already_xset = 0;
  size_t idx   = cidx % MI_BFIELD_BITS;
  size_t field = cidx / MI_BFIELD_BITS;
  while (n > 0) {
    size_t m = MI_BFIELD_BITS - idx;   // m is the bits to xset in this field
    if (m > n) { m = n; }
    mi_assert_internal(idx + m <= MI_BFIELD_BITS);
    mi_assert_internal(field < MI_BCHUNK_FIELDS);
    const mi_bfield_t mask = mi_bfield_mask(m, idx);
    size_t already_xset = 0;
    const bool transition = mi_bfield_atomic_xset_mask(set, &chunk->bfields[field], mask, &already_xset);
    mi_assert_internal((transition && already_xset == m) || (!transition && already_xset > 0));
    all_transition = all_transition && transition;
    total_already_xset += already_xset;
    // next field
    field++;
    idx = 0;
    n -= m;
  }
  if (palready_xset!=NULL) { *palready_xset = total_already_xset; }
  return all_transition;
}


static inline bool mi_bchunk_setN(mi_bchunk_t* chunk, size_t cidx, size_t n, size_t* already_set) {
  return mi_bchunk_xsetN(MI_BIT_SET, chunk, cidx, n, already_set);
}

static inline bool mi_bchunk_clearN(mi_bchunk_t* chunk, size_t cidx, size_t n, size_t* already_clear) {
  return mi_bchunk_xsetN(MI_BIT_CLEAR, chunk, cidx, n, already_clear);
}



// ------ is_xset --------

// Check if a sequence of `n` bits within a chunk are all set/cleared.
static bool mi_bchunk_is_xsetN(mi_xset_t set, mi_bchunk_t* chunk, size_t cidx, size_t n) {
  mi_assert_internal(cidx + n <= MI_BCHUNK_BITS);
  mi_assert_internal(n>0);
  size_t idx = cidx % MI_BFIELD_BITS;
  size_t field = cidx / MI_BFIELD_BITS;
  while (n > 0) {
    size_t m = MI_BFIELD_BITS - idx;   // m is the bits to xset in this field
    if (m > n) { m = n; }
    mi_assert_internal(idx + m <= MI_BFIELD_BITS);
    mi_assert_internal(field < MI_BCHUNK_FIELDS);
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


// ------ try_xset --------

static inline bool mi_bchunk_try_xset(mi_xset_t set, mi_bchunk_t* chunk, size_t cidx) {
  mi_assert_internal(cidx < MI_BCHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  return mi_bfield_atomic_try_xset(set, &chunk->bfields[i], idx);
}

static inline bool mi_bchunk_try_set(mi_bchunk_t* chunk, size_t cidx) {
  return mi_bchunk_try_xset(MI_BIT_SET, chunk, cidx);
}

static inline bool mi_bchunk_try_clear(mi_bchunk_t* chunk, size_t cidx, bool* maybe_all_clear) {
  mi_assert_internal(cidx < MI_BCHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  return mi_bfield_atomic_try_clear(&chunk->bfields[i], idx, maybe_all_clear);
}


//static inline bool mi_bchunk_try_xset8(mi_xset_t set, mi_bchunk_t* chunk, size_t byte_idx) {
//  mi_assert_internal(byte_idx*8 < MI_BCHUNK_BITS);
//  const size_t i = byte_idx / MI_BFIELD_SIZE;
//  const size_t ibyte_idx = byte_idx % MI_BFIELD_SIZE;
//  return mi_bfield_atomic_try_xset8(set, &chunk->bfields[i], ibyte_idx);
//}

static inline bool mi_bchunk_try_set8(mi_bchunk_t* chunk, size_t byte_idx) {
  mi_assert_internal(byte_idx*8 < MI_BCHUNK_BITS);
  const size_t i = byte_idx / MI_BFIELD_SIZE;
  const size_t ibyte_idx = byte_idx % MI_BFIELD_SIZE;
  return mi_bfield_atomic_try_set8(&chunk->bfields[i], ibyte_idx);
}

static inline bool mi_bchunk_try_clear8(mi_bchunk_t* chunk, size_t byte_idx, bool* maybe_all_clear) {
  mi_assert_internal(byte_idx*8 < MI_BCHUNK_BITS);
  const size_t i = byte_idx / MI_BFIELD_SIZE;
  const size_t ibyte_idx = byte_idx % MI_BFIELD_SIZE;
  return mi_bfield_atomic_try_clear8(&chunk->bfields[i], ibyte_idx, maybe_all_clear);
}


// Try to atomically set/clear a sequence of `n` bits within a chunk.
// Returns true if all bits transitioned from 0 to 1 (or 1 to 0),
// and false otherwise leaving all bit fields as is.
static bool mi_bchunk_try_xsetN(mi_xset_t set, mi_bchunk_t* chunk, size_t cidx, size_t n, bool* pmaybe_all_clear) {
  mi_assert_internal(cidx + n <= MI_BCHUNK_BITS);
  mi_assert_internal(n>0);
  if (n==0) return true;
  size_t start_idx = cidx % MI_BFIELD_BITS;
  size_t start_field = cidx / MI_BFIELD_BITS;
  size_t end_field = MI_BCHUNK_FIELDS;
  mi_bfield_t mask_mid = 0;
  mi_bfield_t mask_end = 0;
  bool field_is_clear;
  bool maybe_all_clear = true;
  if (pmaybe_all_clear != NULL) { *pmaybe_all_clear = false; }

  // first field
  size_t field = start_field;
  size_t m = MI_BFIELD_BITS - start_idx;   // m is the bits to xset in this field
  if (m > n) { m = n; }
  mi_assert_internal(start_idx + m <= MI_BFIELD_BITS);
  mi_assert_internal(start_field < MI_BCHUNK_FIELDS);
  const mi_bfield_t mask_start = mi_bfield_mask(m, start_idx);
  if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_start, &field_is_clear)) return false;
  maybe_all_clear = maybe_all_clear && field_is_clear;

  // done?
  n -= m;
  if (n==0) {
    if (pmaybe_all_clear != NULL) { *pmaybe_all_clear = maybe_all_clear; }
    return true;
  }

  // continue with mid fields and last field: if these fail we need to recover by unsetting previous fields

  // mid fields
  while (n >= MI_BFIELD_BITS) {
    field++;
    mi_assert_internal(field < MI_BCHUNK_FIELDS);
    mask_mid = mi_bfield_all_set();
    if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_mid, &field_is_clear)) goto restore;
    maybe_all_clear = maybe_all_clear && field_is_clear;
    n -= MI_BFIELD_BITS;
  }

  // last field
  if (n > 0) {
    mi_assert_internal(n < MI_BFIELD_BITS);
    field++;
    mi_assert_internal(field < MI_BCHUNK_FIELDS);
    end_field = field;
    mask_end = mi_bfield_mask(n, 0);
    if (!mi_bfield_atomic_try_xset_mask(set, &chunk->bfields[field], mask_end, &field_is_clear)) goto restore;
    maybe_all_clear = maybe_all_clear && field_is_clear;
  }

  if (pmaybe_all_clear != NULL) { *pmaybe_all_clear = maybe_all_clear; }
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

static inline bool mi_bchunk_try_setN(mi_bchunk_t* chunk, size_t cidx, size_t n) {
  return mi_bchunk_try_xsetN(MI_BIT_SET, chunk, cidx, n, NULL);
}

static inline bool mi_bchunk_try_clearN(mi_bchunk_t* chunk, size_t cidx, size_t n, bool* maybe_all_clear) {
  return mi_bchunk_try_xsetN(MI_BIT_CLEAR, chunk, cidx, n, maybe_all_clear);
}

static inline void mi_bchunk_clear_once_set(mi_bchunk_t* chunk, size_t cidx) {
  mi_assert_internal(cidx < MI_BCHUNK_BITS);
  const size_t i = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  mi_bfield_atomic_clear_once_set(&chunk->bfields[i], idx);
}

// ------ find_and_try_xset --------

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
// set `*pidx` to the bit index (0 <= *pidx < MI_BCHUNK_BITS) on success.
// todo: try neon version
static inline bool mi_bchunk_find_and_try_xset(mi_xset_t set, mi_bchunk_t* chunk, size_t* pidx) {
#if defined(__AVX2__) && (MI_BCHUNK_BITS==256)
  while (true) {
    const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vcmp = _mm256_cmpeq_epi64(vec, (set ? mi_mm256_ones() : mi_mm256_zero())); // (elem64 == ~0 / 0 ? 0xFF  : 0)
    const uint32_t mask = ~_mm256_movemask_epi8(vcmp);  // mask of most significant bit of each byte (so each 8 bits are all set or clear)
    // mask is inverted, so each 8-bits is 0xFF iff the corresponding elem64 has a zero / one bit (and thus can be set/cleared)
    if (mask==0) return false;
    mi_assert_internal((_tzcnt_u32(mask)%8) == 0); // tzcnt == 0, 8, 16, or 24
    const size_t chunk_idx = _tzcnt_u32(mask) / 8;
    mi_assert_internal(chunk_idx < MI_BCHUNK_FIELDS);
    size_t cidx;
    if (mi_bfield_find_least_to_xset(set, chunk->bfields[chunk_idx], &cidx)) {           // find the bit-idx that is set/clear
      if mi_likely(mi_bfield_atomic_try_xset(set, &chunk->bfields[chunk_idx], cidx)) {  // set/clear it atomically
        *pidx = (chunk_idx*MI_BFIELD_BITS) + cidx;
        mi_assert_internal(*pidx < MI_BCHUNK_BITS);
        return true;
      }
    }
    // try again
  }
#elif defined(__AVX2__) && (MI_BCHUNK_BITS==512)
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
    mi_assert_internal(chunk_idx < MI_BCHUNK_FIELDS);
    size_t cidx;
    if (mi_bfield_find_least_to_xset(set, chunk->bfields[chunk_idx], &cidx)) {           // find the bit-idx that is set/clear
      if mi_likely(mi_bfield_atomic_try_xset(set, &chunk->bfields[chunk_idx], cidx)) {  // set/clear it atomically
        *pidx = (chunk_idx*MI_BFIELD_BITS) + cidx;
        mi_assert_internal(*pidx < MI_BCHUNK_BITS);
        return true;
      }
    }
    // try again
  }
#else
  for (int i = 0; i < MI_BCHUNK_FIELDS; i++) {
    size_t idx;
    if mi_unlikely(mi_bfield_find_least_to_xset(set, chunk->bfields[i], &idx)) { // find least 0-bit
      if mi_likely(mi_bfield_atomic_try_xset(set, &chunk->bfields[i], idx)) {  // try to set it atomically
        *pidx = (i*MI_BFIELD_BITS + idx);
        mi_assert_internal(*pidx < MI_BCHUNK_BITS);
        return true;
      }
    }
  }
  return false;
#endif
}

static inline bool mi_bchunk_find_and_try_clear(mi_bchunk_t* chunk, size_t* pidx) {
  return mi_bchunk_find_and_try_xset(MI_BIT_CLEAR, chunk, pidx);
}

static inline bool mi_bchunk_find_and_try_set(mi_bchunk_t* chunk, size_t* pidx) {
  return mi_bchunk_find_and_try_xset(MI_BIT_SET, chunk, pidx);
}


// find least byte in a chunk with all bits set, and try unset it atomically
// set `*pidx` to its bit index (0 <= *pidx < MI_BCHUNK_BITS) on success.
// todo: try neon version
static inline bool mi_bchunk_find_and_try_clear8(mi_bchunk_t* chunk, size_t* pidx) {
  #if defined(__AVX2__) && (MI_BCHUNK_BITS==256)
  while(true) {
    const __m256i vec  = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vcmp = _mm256_cmpeq_epi8(vec, mi_mm256_ones()); // (byte == ~0 ? -1  : 0)
    const uint32_t mask = _mm256_movemask_epi8(vcmp);    // mask of most significant bit of each byte
    if (mask == 0) return false;
    const size_t i = _tzcnt_u32(mask);
    mi_assert_internal(8*i < MI_BCHUNK_BITS);
    const size_t chunk_idx = i / MI_BFIELD_SIZE;
    const size_t byte_idx  = i % MI_BFIELD_SIZE;
    if mi_likely(mi_bfield_atomic_try_xset8(MI_BIT_CLEAR,&chunk->bfields[chunk_idx],byte_idx)) {  // try to unset atomically
      *pidx = (chunk_idx*MI_BFIELD_BITS) + (byte_idx*8);
      mi_assert_internal(*pidx < MI_BCHUNK_BITS);
      return true;
    }
    // try again
  }
  #else
    for(int i = 0; i < MI_BCHUNK_FIELDS; i++) {
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
        if mi_likely(mi_bfield_atomic_try_clear8(&chunk->bfields[i],byte_idx,NULL)) {  // unset the byte atomically
          *pidx = (i*MI_BFIELD_BITS) + idx;
          mi_assert_internal(*pidx + 8 <= MI_BCHUNK_BITS);
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
// set `*pidx` to its bit index (0 <= *pidx <= MI_BCHUNK_BITS - n) on success.
static bool mi_bchunk_find_and_try_clearNX(mi_bchunk_t* chunk, size_t n, size_t* pidx) {
  if (n == 0 || n > MI_BFIELD_BITS) return false;
  const mi_bfield_t mask = mi_bfield_mask(n, 0);
  for(int i = 0; i < MI_BCHUNK_FIELDS; i++) {
    mi_bfield_t b = chunk->bfields[i];
    size_t bshift = 0;
    size_t idx;
    while (mi_bfield_find_least_bit(b, &idx)) { // find least 1-bit
      b >>= idx;
      bshift += idx;
      if (bshift + n > MI_BFIELD_BITS) break;

      if ((b&mask) == mask) { // found a match
        mi_assert_internal( ((mask << bshift) >> bshift) == mask );
        if mi_likely(mi_bfield_atomic_try_clear_mask(&chunk->bfields[i],mask<<bshift,NULL)) {
          *pidx = (i*MI_BFIELD_BITS) + bshift;
          mi_assert_internal(*pidx < MI_BCHUNK_BITS);
          mi_assert_internal(*pidx + n <= MI_BCHUNK_BITS);
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

// find a sequence of `n` bits in a chunk with `n < MI_BCHUNK_BITS` with all bits set,
// and try to clear them atomically.
// set `*pidx` to its bit index (0 <= *pidx <= MI_BCHUNK_BITS - n) on success.
static bool mi_bchunk_find_and_try_clearN_(mi_bchunk_t* chunk, size_t n, size_t* pidx) {
  if (n == 0 || n > MI_BCHUNK_BITS) return false;  // cannot be more than a chunk
  // if (n < MI_BFIELD_BITS) return mi_bchunk_find_and_try_clearNX(chunk, n, pidx);

  // we align an a field, and require `field_count` fields to be all clear.
  // n >= MI_BFIELD_BITS; find a first field that is 0
  const size_t field_count = _mi_divide_up(n, MI_BFIELD_BITS);  // we need this many fields
  for (size_t i = 0; i <= MI_BCHUNK_FIELDS - field_count; i++)
  {
    // first pre-scan for a range of fields that are all set
    bool allset = true;
    size_t j = 0;
    do {
      mi_assert_internal(i + j < MI_BCHUNK_FIELDS);
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
      if (mi_bchunk_try_clearN(chunk, cidx, n, NULL)) {
        // we cleared all atomically
        *pidx = cidx;
        mi_assert_internal(*pidx < MI_BCHUNK_BITS);
        mi_assert_internal(*pidx + n <= MI_BCHUNK_BITS);
        return true;
      }
    }
  }
  return false;
}


static inline bool mi_bchunk_find_and_try_clearN(mi_bchunk_t* chunk, size_t n, size_t* pidx) {
  if (n==1) return mi_bchunk_find_and_try_clear(chunk, pidx);
  if (n==8) return mi_bchunk_find_and_try_clear8(chunk, pidx);
  if (n == 0 || n > MI_BCHUNK_BITS) return false;  // cannot be more than a chunk
  if (n < MI_BFIELD_BITS) return mi_bchunk_find_and_try_clearNX(chunk, n, pidx);
  return mi_bchunk_find_and_try_clearN_(chunk, n, pidx);
}


// are all bits in a bitmap chunk clear? (this uses guaranteed atomic reads)
static inline bool mi_bchunk_all_are_clear(mi_bchunk_t* chunk) {
  for(int i = 0; i < MI_BCHUNK_FIELDS; i++) {
    if (mi_atomic_load_relaxed(&chunk->bfields[i]) != 0) return false;
  }
  return true;
}

// are all bits in a bitmap chunk clear?
static inline bool mi_bchunk_all_are_clear_relaxed(mi_bchunk_t* chunk) {
  #if defined(__AVX2__) && (MI_BCHUNK_BITS==256)
  const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
  return mi_mm256_is_zero(vec);
  #elif defined(__AVX2__) && (MI_BCHUNK_BITS==512)
  // a 64b cache-line contains the entire chunk anyway so load both at once
  const __m256i vec1 = _mm256_load_si256((const __m256i*)chunk->bfields);
  const __m256i vec2 = _mm256_load_si256(((const __m256i*)chunk->bfields)+1);
  return (mi_mm256_is_zero(_mm256_or_epi64(vec1,vec2)));
  #else
  return mi_bchunk_all_are_clear(chunk);
  #endif
}


/* --------------------------------------------------------------------------------
  chunkmap
-------------------------------------------------------------------------------- */


/* --------------------------------------------------------------------------------
 bitmap chunkmap
-------------------------------------------------------------------------------- */

static void mi_bitmap_chunkmap_set(mi_bitmap_t* bitmap, size_t chunk_idx) {
  mi_assert(chunk_idx < mi_bitmap_chunk_count(bitmap));
  mi_bchunk_set(&bitmap->chunkmap, chunk_idx);
}

static bool mi_bitmap_chunkmap_try_clear(mi_bitmap_t* bitmap, size_t chunk_idx) {
  mi_assert(chunk_idx < mi_bitmap_chunk_count(bitmap));
  // check if the corresponding chunk is all clear
  if (!mi_bchunk_all_are_clear_relaxed(&bitmap->chunks[chunk_idx])) return false;
  // clear the chunkmap bit
  mi_bchunk_clear(&bitmap->chunkmap, chunk_idx, NULL);
  // .. but a concurrent set may have happened in between our all-clear test and the clearing of the
  // bit in the mask. We check again to catch this situation.
  if (!mi_bchunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
    mi_bchunk_set(&bitmap->chunkmap, chunk_idx);
    return false;
  }
  return true;
}

/* --------------------------------------------------------------------------------
 bitmap
-------------------------------------------------------------------------------- */

size_t mi_bitmap_size(size_t bit_count, size_t* pchunk_count) {
  mi_assert_internal((bit_count % MI_BCHUNK_BITS) == 0);
  bit_count = _mi_align_up(bit_count, MI_BCHUNK_BITS);
  mi_assert_internal(bit_count <= MI_BITMAP_MAX_BIT_COUNT);
  mi_assert_internal(bit_count > 0);
  const size_t chunk_count = bit_count / MI_BCHUNK_BITS;
  mi_assert_internal(chunk_count >= 1);
  const size_t size = sizeof(mi_bitmap_t) + ((chunk_count - 1) * MI_BCHUNK_SIZE);
  mi_assert_internal( (size%MI_BCHUNK_SIZE) == 0 );
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
  mi_atomic_store_release(&bitmap->chunk_count, chunk_count);
  mi_assert_internal(mi_atomic_load_relaxed(&bitmap->chunk_count) <= MI_BITMAP_MAX_CHUNK_COUNT);
  return size;
}

// Set a sequence of `n` bits in the bitmap (and can cross chunks). Not atomic so only use if local to a thread.
void mi_bitmap_unsafe_setN(mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(idx + n <= mi_bitmap_max_bits(bitmap));

  // first chunk
  size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t cidx = idx % MI_BCHUNK_BITS;
  size_t m = MI_BCHUNK_BITS - cidx;
  if (m > n) { m = n; }
  mi_bchunk_setN(&bitmap->chunks[chunk_idx], cidx, m, NULL);
  mi_bitmap_chunkmap_set(bitmap, chunk_idx);

  // n can be large so use memset for efficiency for all in-between chunks
  chunk_idx++;
  n -= m;
  const size_t mid_chunks = n / MI_BCHUNK_BITS;
  if (mid_chunks > 0) {
    _mi_memset(&bitmap->chunks[chunk_idx], ~0, mid_chunks * MI_BCHUNK_SIZE);
    const size_t end_chunk = chunk_idx + mid_chunks;
    while (chunk_idx < end_chunk) {
      if ((chunk_idx % MI_BFIELD_BITS) == 0 && (chunk_idx + MI_BFIELD_BITS <= end_chunk)) {
        // optimize: we can set a full bfield in the chunkmap
        mi_atomic_store_relaxed( &bitmap->chunkmap.bfields[chunk_idx/MI_BFIELD_BITS], mi_bfield_all_set());
        chunk_idx += MI_BFIELD_BITS;
      }
      else {
        mi_bitmap_chunkmap_set(bitmap, chunk_idx);
        chunk_idx++;
      }
    }
    n -= (mid_chunks * MI_BCHUNK_BITS);
  }

  // last chunk
  if (n > 0) {
    mi_assert_internal(n < MI_BCHUNK_BITS);
    mi_assert_internal(chunk_idx < MI_BCHUNK_FIELDS);
    mi_bchunk_setN(&bitmap->chunks[chunk_idx], 0, n, NULL);
    mi_bitmap_chunkmap_set(bitmap, chunk_idx);
  }
}


// Try to set/clear a bit in the bitmap; returns `true` if atomically transitioned from 0 to 1 (or 1 to 0),
// and false otherwise leaving the bitmask as is.
static bool mi_bitmap_try_xset(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < mi_bitmap_max_bits(bitmap));
  const size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t cidx = idx % MI_BCHUNK_BITS;
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (set) {
    const bool ok = mi_bchunk_try_set(&bitmap->chunks[chunk_idx], cidx);
    if (ok) { mi_bitmap_chunkmap_set(bitmap,chunk_idx); }  // set afterwards
    return ok;
  }
  else {
    bool maybe_all_clear;
    const bool ok = mi_bchunk_try_clear(&bitmap->chunks[chunk_idx], cidx, &maybe_all_clear);
    if (maybe_all_clear) { mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx); }
    return ok;
  }
}

// Try to set/clear a byte in the bitmap; returns `true` if atomically transitioned from 0 to 0xFF (or 0xFF to 0)
// and false otherwise leaving the bitmask as is.
static bool mi_bitmap_try_xset8(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < mi_bitmap_max_bits(bitmap));
  mi_assert_internal(idx%8 == 0);
  const size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t byte_idx  = (idx % MI_BCHUNK_BITS)/8;
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (set) {
    const bool ok = mi_bchunk_try_set8(&bitmap->chunks[chunk_idx], byte_idx);
    if (ok) { mi_bitmap_chunkmap_set(bitmap,chunk_idx); }  // set afterwards
    return ok;
  }
  else {
    bool maybe_all_clear;
    const bool ok = mi_bchunk_try_clear8(&bitmap->chunks[chunk_idx], byte_idx, &maybe_all_clear);
    if (maybe_all_clear) { mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx); }
    return ok;
  }
}

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's)
// and false otherwise leaving the bitmask as is.
// `n` cannot cross chunk boundaries (and `n <= MI_BCHUNK_BITS`)!
static bool mi_bitmap_try_xsetN_(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BCHUNK_BITS);
  mi_assert_internal(idx + n <= mi_bitmap_max_bits(bitmap));
  if (n==0 || idx + n > mi_bitmap_max_bits(bitmap)) return false;

  const size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t cidx = idx % MI_BCHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BCHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (cidx + n > MI_BCHUNK_BITS) { n = MI_BCHUNK_BITS - cidx; }  // paranoia
  if (set) {
    const bool ok = mi_bchunk_try_setN(&bitmap->chunks[chunk_idx], cidx, n);
    if (ok) { mi_bitmap_chunkmap_set(bitmap,chunk_idx); }  // set afterwards
    return ok;
  }
  else {
    bool maybe_all_clear;
    const bool ok = mi_bchunk_try_clearN(&bitmap->chunks[chunk_idx], cidx, n, &maybe_all_clear);
    if (maybe_all_clear) { mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx); }
    return ok;
  }
}

mi_decl_nodiscard bool mi_bitmap_try_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0 && n<=MI_BCHUNK_BITS);
  if (n==1) return mi_bitmap_try_xset(set, bitmap, idx);
  if (n==8) return mi_bitmap_try_xset8(set, bitmap, idx);
  // todo: add 32/64 for large pages ?
  return mi_bitmap_try_xsetN_(set, bitmap, idx, n);
}


// Set/clear a bit in the bitmap; returns `true` if atomically transitioned from 0 to 1 (or 1 to 0)
bool mi_bitmap_xset(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < mi_bitmap_max_bits(bitmap));
  const size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t cidx = idx % MI_BCHUNK_BITS;
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (set) {
    const bool wasclear = mi_bchunk_set(&bitmap->chunks[chunk_idx], cidx);
    mi_bitmap_chunkmap_set(bitmap, chunk_idx); // set afterwards
    return wasclear;
  }
  else {
    bool maybe_all_clear;
    const bool wasset = mi_bchunk_clear(&bitmap->chunks[chunk_idx], cidx, &maybe_all_clear);
    if (maybe_all_clear) { mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx); }
    return wasset;
  }
}

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BCHUNK_BITS`)!
static bool mi_bitmap_xsetN_(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, size_t* already_xset ) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BCHUNK_BITS);

  const size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t cidx = idx % MI_BCHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BCHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (cidx + n > MI_BCHUNK_BITS) { n = MI_BCHUNK_BITS - cidx; }  // paranoia

  if (set) {
    const bool allclear = mi_bchunk_setN(&bitmap->chunks[chunk_idx], cidx, n, already_xset);
    mi_bitmap_chunkmap_set(bitmap,chunk_idx);   // set afterwards
    return allclear;
  }
  else {
    size_t already_clear = 0;
    const bool allset = mi_bchunk_clearN(&bitmap->chunks[chunk_idx], cidx, n, &already_clear );
    if (already_xset != NULL) { *already_xset = already_clear; }
    if (already_clear < n) { mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx); }
    return allset;
  }
}

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BCHUNK_BITS`)!
bool mi_bitmap_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, size_t* already_xset) {
  mi_assert_internal(n>0 && n<=MI_BCHUNK_BITS);
  //TODO: specialize?
  //if (n==1) return mi_bitmap_xset(set, bitmap, idx);
  //if (n==2) return mi_bitmap_xset(set, bitmap, idx);
  //if (n==8) return mi_bitmap_xset8(set, bitmap, idx);
  return mi_bitmap_xsetN_(set, bitmap, idx, n, already_xset);
}


// Is a sequence of n bits already all set/cleared?
bool mi_bitmap_is_xsetN(mi_xset_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BCHUNK_BITS);
  mi_assert_internal(idx + n <= mi_bitmap_max_bits(bitmap));

  const size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t cidx = idx % MI_BCHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BCHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  if (cidx + n > MI_BCHUNK_BITS) { n = MI_BCHUNK_BITS - cidx; }  // paranoia

  return mi_bchunk_is_xsetN(set, &bitmap->chunks[chunk_idx], cidx, n);
}


/* --------------------------------------------------------------------------------
  bitmap try_find_and_clear
-------------------------------------------------------------------------------- */


#define mi_bitmap_forall_chunks(bitmap, tseq, name_epoch, name_chunk_idx) \
  { \
  /* start chunk index -- todo: can depend on the tseq to decrease contention between threads */ \
  MI_UNUSED(tseq); \
  const size_t chunk_start = 0; /* tseq % (1 + mi_bitmap_find_hi_chunk(bitmap)); */ \
  const size_t chunkmap_max_bfield = _mi_divide_up( mi_bitmap_chunk_count(bitmap), MI_BCHUNK_BITS ); \
  const size_t chunkmap_start = chunk_start / MI_BFIELD_BITS; \
  const size_t chunkmap_start_idx = chunk_start % MI_BFIELD_BITS; \
  /* for each chunkmap entry `i` */ \
  for (size_t _i = 0; _i < chunkmap_max_bfield; _i++) { \
    size_t i = (_i + chunkmap_start); \
    if (i >= chunkmap_max_bfield) { i -= chunkmap_max_bfield; } /* adjust for the start position */ \
    \
    const size_t chunk_idx0 = i*MI_BFIELD_BITS; \
    mi_bfield_t cmap = mi_atomic_load_relaxed(&bitmap->chunkmap.bfields[i]); \
    size_t      cmap_idx_shift = 0;   /* shift through the cmap */ \
    if (_i == 0) { cmap = mi_rotr(cmap, chunkmap_start_idx); cmap_idx_shift = chunkmap_start_idx; }   /* rotate right for the start position (on the first iteration) */ \
    \
    size_t cmap_idx; \
    while (mi_bsf(cmap, &cmap_idx)) {     /* find least bit that is set */ \
      /* set the chunk idx */ \
      size_t name_chunk_idx = chunk_idx0 + ((cmap_idx + cmap_idx_shift) % MI_BFIELD_BITS); \
      mi_assert(chunk_idx < mi_bitmap_chunk_count(bitmap)); \
      /* try to find and clear N bits in that chunk */ \
      {

#define mi_bitmap_forall_chunks_end() \
      } \
      /* skip to the next bit */ \
      cmap_idx_shift += cmap_idx+1; \
      cmap >>= cmap_idx;            /* skip scanned bits (and avoid UB for `cmap_idx+1`) */ \
      cmap >>= 1; \
    } \
  }}

// Find a sequence of `n` bits in the bitmap with all bits set, and atomically unset all.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-n`.
mi_decl_nodiscard bool mi_bitmap_try_find_and_clearN(mi_bitmap_t* bitmap, size_t n, size_t tseq, size_t* pidx)
{
  mi_bitmap_forall_chunks(bitmap, tseq, epoch, chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bchunk_find_and_try_clearN(&bitmap->chunks[chunk_idx], n, &cidx)) {
      *pidx = (chunk_idx * MI_BCHUNK_BITS) + cidx;
      mi_assert_internal(*pidx <= mi_bitmap_max_bits(bitmap) - n);
      return true;
    }
    else {
      // we may find that all are cleared only on a second iteration but that is ok as
      // the chunkmap is a conservative approximation.
      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx);
      // continue
    }
  }
  mi_bitmap_forall_chunks_end();
  return false;
}


mi_decl_nodiscard bool mi_bitmap_try_find_and_claim(mi_bitmap_t* bitmap, size_t tseq, size_t* pidx,
                                                    mi_claim_fun_t* claim, void* arg1, void* arg2)
{
  mi_bitmap_forall_chunks(bitmap, tseq, epoch, chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bchunk_find_and_try_clear(&bitmap->chunks[chunk_idx], &cidx)) {
      const size_t slice_index = (chunk_idx * MI_BCHUNK_BITS) + cidx;
      mi_assert_internal(slice_index < mi_bitmap_max_bits(bitmap));
      bool keep_set = true;
      if ((*claim)(slice_index, arg1, arg2, &keep_set)) {
        // success!
        mi_assert_internal(!keep_set);
        *pidx = slice_index;
        return true;
      }
      else {
        // failed to claim it, set abandoned mapping again (unless thet page was freed)
        if (keep_set) {
          const bool wasclear = mi_bchunk_set(&bitmap->chunks[chunk_idx], cidx);
          mi_assert_internal(wasclear); MI_UNUSED(wasclear);
        }        
        // continue
      }
    }
    else {
      // we may find that all are cleared only on a second iteration but that is ok as
      // the chunkmap is a conservative approximation.
      mi_bitmap_chunkmap_try_clear(bitmap, chunk_idx);
      // continue
    }
  }
  mi_bitmap_forall_chunks_end();
  return false;
}

// Clear a bit once it is set.
void mi_bitmap_clear_once_set(mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < mi_bitmap_max_bits(bitmap));
  const size_t chunk_idx = idx / MI_BCHUNK_BITS;
  const size_t cidx = idx % MI_BCHUNK_BITS;
  mi_assert_internal(chunk_idx < mi_bitmap_chunk_count(bitmap));
  mi_bchunk_clear_once_set(&bitmap->chunks[chunk_idx], cidx);
}