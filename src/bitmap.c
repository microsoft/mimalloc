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

static inline size_t mi_bfield_clz(mi_bfield_t x) {
  return mi_clz(x);
}

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

// Set/clear a bit atomically. Returns `true` if the bit transitioned from 0 to 1 (or 1 to 0).
static inline bool mi_bfield_atomic_xset(mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = ((mi_bfield_t)1)<<idx;
  if (set) {
    const mi_bfield_t old = mi_atomic(fetch_or_explicit)(b, mask, mi_memory_order(acq_rel));
    return ((old&mask) == 0);
  }
  else {
    mi_bfield_t old = mi_atomic(fetch_and_explicit)(b, ~mask, mi_memory_order(acq_rel));
    return ((old&mask) == mask);
  }
}

// Set/clear a mask set of bits atomically, and return true of the mask bits transitioned from all 0's to 1's (or all 1's to 0's)
// `already_xset` is true if all bits for the mask were already set/cleared.
static bool mi_bfield_atomic_xset_mask(mi_bit_t set, _Atomic(mi_bfield_t)*b, mi_bfield_t mask, bool* already_xset) {
  mi_assert_internal(mask != 0);
  if (set) {
    mi_bfield_t old = *b;
    while (!mi_atomic_cas_weak_acq_rel(b, &old, old|mask));  // try to atomically set the mask bits until success
    *already_xset = ((old&mask) == mask);
    return ((old&mask) == 0);
  }
  else { // clear
    mi_bfield_t old = *b;
    while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask));  // try to atomically clear the mask bits until success
    *already_xset = ((old&mask) == 0);
    return ((old&mask) == mask);
  }
}

// Tries to set/clear a bit atomically, and returns true if the bit atomically transitioned from 0 to 1 (or 1 to 0)
static bool mi_bfield_atomic_try_xset( mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  // for a single bit, we can always just set/clear and test afterwards if it was actually us that changed it first
  return mi_bfield_atomic_xset(set, b, idx);
}

// Tries to (un)set a mask atomically, and returns true if the mask bits atomically transitioned from 0 to mask (or mask to 0)
// and false otherwise (leaving the bit field as is).
static bool mi_bfield_atomic_try_xset_mask(mi_bit_t set, _Atomic(mi_bfield_t)* b, mi_bfield_t mask ) {
  mi_assert_internal(mask != 0);
  if (set) {
    mi_bfield_t old = *b;
    do {
      if ((old&mask) != 0) return false; // the mask bits are no longer 0
    } while (!mi_atomic_cas_weak_acq_rel(b, &old, old|mask));  // try to atomically set the mask bits
    return true;
  }
  else { // clear
    mi_bfield_t old = *b;
    do {
      if ((old&mask) != mask) return false; // the mask bits are no longer set
    } while (!mi_atomic_cas_weak_acq_rel(b, &old, old&~mask));  // try to atomically clear the mask bits
    return true;
  }
}

// Tries to set/clear a byte atomically, and returns true if the byte atomically transitioned from 0 to 0xFF (or 0xFF to 0)
// and false otherwise (leaving the bit field as is).
static bool mi_bfield_atomic_try_xset8(mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t byte_idx) {
  mi_assert_internal(byte_idx < MI_BFIELD_SIZE);
  const mi_bfield_t mask = ((mi_bfield_t)0xFF)<<(byte_idx*8);
  return mi_bfield_atomic_try_xset_mask(set, b, mask);
}


// Check if all bits corresponding to a mask are set/cleared.
static bool mi_bfield_atomic_is_xset_mask(mi_bit_t set, _Atomic(mi_bfield_t)*b, mi_bfield_t mask) {
  mi_assert_internal(mask != 0);
  if (set) {
    return ((*b & mask) == mask);
  }
  else {
    return ((*b & mask) == 0);
  }
}

// Check if a bit is set/clear
static inline bool mi_bfield_atomic_is_xset(mi_bit_t set, _Atomic(mi_bfield_t)*b, size_t idx) {
  mi_assert_internal(idx < MI_BFIELD_BITS);
  const mi_bfield_t mask = ((mi_bfield_t)1)<<idx;
  return mi_bfield_atomic_is_xset_mask(set, b, mask);
}


/* --------------------------------------------------------------------------------
 bitmap chunks
-------------------------------------------------------------------------------- */

static bool mi_bitmap_chunk_try_xset(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx ) {
  mi_assert_internal(cidx < MI_BITMAP_CHUNK_BITS);
  const size_t i   = cidx / MI_BFIELD_BITS;
  const size_t idx = cidx % MI_BFIELD_BITS;
  return mi_bfield_atomic_try_xset( set, &chunk->bfields[i], idx);
}

static bool mi_bitmap_chunk_try_xset8(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t byte_idx ) {
  mi_assert_internal(byte_idx*8 < MI_BITMAP_CHUNK_BITS);
  const size_t i         = byte_idx / MI_BFIELD_SIZE;
  const size_t ibyte_idx = byte_idx % MI_BFIELD_SIZE;
  return mi_bfield_atomic_try_xset8( set, &chunk->bfields[i], ibyte_idx);
}

// Set/clear a sequence of `n` bits within a chunk. Returns true if all bits transitioned from 0 to 1 (or 1 to 0)
static bool mi_bitmap_chunk_xsetN(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t cidx, size_t n, bool* palready_xset) {
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);
  mi_assert_internal(n>0);
  bool all_transition = true;
  bool all_already_xset = true;
  size_t idx   = cidx % MI_BFIELD_BITS;
  size_t field = cidx / MI_BFIELD_BITS;
  while (n > 0) {
    size_t m = MI_BFIELD_BITS - idx;   // m is the bits to xset in this field
    if (m > n) { m = n; }
    mi_assert_internal(idx + m <= MI_BFIELD_BITS);
    mi_assert_internal(field < MI_BITMAP_CHUNK_FIELDS);
    const size_t mask = (m == MI_BFIELD_BITS ? ~MI_ZU(0) : ((MI_ZU(1)<<m)-1) << idx);
    bool already_xset = false;
    all_transition = all_transition && mi_bfield_atomic_xset_mask(set, &chunk->bfields[field], mask, &already_xset);
    all_already_xset = all_already_xset && already_xset;
    // next field
    field++;
    idx = 0;
    n -= m;
  }
  *palready_xset = all_already_xset;
  return all_transition;
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
    bool already_xset;
    mi_bfield_atomic_xset_mask(!set, &chunk->bfields[field], mask, &already_xset);
  }
  return false;
}


// find least 0/1-bit in a chunk and try to set/clear it atomically
// set `*pidx` to the bit index (0 <= *pidx < MI_BITMAP_CHUNK_BITS) on success.
// todo: try neon version
static inline bool mi_bitmap_chunk_find_and_try_xset(mi_bit_t set, mi_bitmap_chunk_t* chunk, size_t* pidx) {
#if 0 && defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  while (true) {
    const __m256i vec   = _mm256_load_si256((const __m256i*)chunk->bfields);
    const __m256i vcmp  = _mm256_cmpeq_epi64(vec, (set ? _mm256_set1_epi64x(~0) : _mm256_setzero_si256())); // (elem64 == ~0 / 0 ? 0xFF  : 0)
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

/*
// find least 1-bit in a chunk and try unset it atomically
// set `*pidx` to thi bit index (0 <= *pidx < MI_BITMAP_CHUNK_BITS) on success.
// todo: try neon version
static inline bool mi_bitmap_chunk_find_and_try_clear(mi_bitmap_chunk_t* chunk, size_t* pidx) {
  #if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  while(true) {
    const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
    if (_mm256_testz_si256(vec,vec)) return false;   // vec == 0 ?
    const __m256i vcmp = _mm256_cmpeq_epi64(vec, _mm256_setzero_si256()); // (elem64 == 0 ? -1  : 0)
    const uint32_t mask = ~_mm256_movemask_epi8(vcmp);    // mask of most significant bit of each byte (so each 8 bits in the mask will be all 1 or all 0)
    mi_assert_internal(mask != 0);
    const size_t chunk_idx = _tzcnt_u32(mask) / 8;           // tzcnt == 0, 8, 16, or 24
    mi_assert_internal(chunk_idx < MI_BITMAP_CHUNK_FIELDS);
    size_t cidx;
    if (mi_bfield_find_least_bit(chunk->bfields[chunk_idx],&cidx)) {           // find the bit that is set
      if mi_likely(mi_bfield_atomic_try_xset(MI_BIT_CLEAR,&chunk->bfields[chunk_idx], cidx)) {  // unset atomically
        *pidx = (chunk_idx*MI_BFIELD_BITS) + cidx;
        mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
        return true;
      }
    }
    // try again
  }
  #else
  for(int i = 0; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    size_t idx;
    if mi_unlikely(mi_bfield_find_least_bit(chunk->bfields[i],&idx)) { // find least 1-bit
      if mi_likely(mi_bfield_atomic_try_xset(MI_BIT_CLEAR,&chunk->bfields[i],idx)) {  // try unset atomically
        *pidx = (i*MI_BFIELD_BITS + idx);
        mi_assert_internal(*pidx < MI_BITMAP_CHUNK_BITS);
        return true;
      }
    }
  }
  return false;
  #endif
}
*/

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


// find a sequence of `n` bits in a chunk with all `n` bits set, and try unset it atomically
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
        if mi_likely(mi_bfield_atomic_try_xset_mask(MI_BIT_CLEAR,&chunk->bfields[i],mask<<bshift)) {
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
static inline bool mi_bitmap_chunk_all_are_set(mi_bitmap_chunk_t* chunk) {
  #if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
  return _mm256_test_all_ones(vec);
  #else
  // written like this for vectorization
  mi_bfield_t x = chunk->bfields[0];
  for(int i = 1; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    x = x & chunk->bfields[i];
  }
  return (~x == 0);
  #endif
}

// are all bits in a bitmap chunk clear?
static bool mi_bitmap_chunk_all_are_clear(mi_bitmap_chunk_t* chunk) {
  #if defined(__AVX2__) && (MI_BITMAP_CHUNK_BITS==256)
  const __m256i vec = _mm256_load_si256((const __m256i*)chunk->bfields);
  return _mm256_testz_si256( vec, vec );
  #else
  // written like this for vectorization
  mi_bfield_t x = chunk->bfields[0];
  for(int i = 1; i < MI_BITMAP_CHUNK_FIELDS; i++) {
    x = x | chunk->bfields[i];
  }
  return (x == 0);
  #endif
}

/* --------------------------------------------------------------------------------
 bitmap
-------------------------------------------------------------------------------- */
static void mi_bitmap_update_anyset(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx) {
  if (set) {
    mi_bfield_atomic_xset(MI_BIT_SET, &bitmap->any_set, idx);
  }
  else { // clear
    if (mi_bitmap_chunk_all_are_clear(&bitmap->chunks[idx])) {
      mi_bfield_atomic_xset(MI_BIT_CLEAR, &bitmap->any_set, idx);
    }
  }
}

// initialize a bitmap to all unset; avoid a mem_zero if `already_zero` is true
void mi_bitmap_init(mi_bitmap_t* bitmap, bool already_zero) {
  if (!already_zero) {
    _mi_memzero_aligned(bitmap, sizeof(*bitmap));
  }
}

// Set/clear a sequence of `n` bits in the bitmap (and can cross chunks). Not atomic so only use if local to a thread.
void mi_bitmap_unsafe_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n) {
  mi_assert_internal(n>0);
  mi_assert_internal(idx + n<=MI_BITMAP_MAX_BITS);

  // first chunk
  size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  size_t m = MI_BITMAP_CHUNK_BITS - cidx;
  if (m > n) { m = n; }
  bool already_xset;
  mi_bitmap_chunk_xsetN(set, &bitmap->chunks[chunk_idx], cidx, m, &already_xset);
  mi_bitmap_update_anyset(set, bitmap, chunk_idx);

  // n can be large so use memset for efficiency for all in-between chunks
  chunk_idx++;
  n -= m;
  const size_t mid_chunks = n / MI_BITMAP_CHUNK_BITS;
  if (mid_chunks > 0) {
    _mi_memset(&bitmap->chunks[chunk_idx], (set ? ~0 : 0), mid_chunks * (MI_BITMAP_CHUNK_BITS/8));
    const size_t end_chunk = chunk_idx + mid_chunks;
    while (chunk_idx < end_chunk) {
      mi_bitmap_update_anyset(set, bitmap, chunk_idx);
      chunk_idx++;
    }
    n -= (mid_chunks * MI_BITMAP_CHUNK_BITS);
  }

  // last chunk
  if (n > 0) {
    mi_assert_internal(n < MI_BITMAP_CHUNK_BITS);
    mi_assert_internal(chunk_idx < MI_BITMAP_CHUNK_FIELDS);
    mi_bitmap_chunk_xsetN(set, &bitmap->chunks[chunk_idx], 0, n, &already_xset);
    mi_bitmap_update_anyset(set, bitmap, chunk_idx);
  }
}


// Try to set/clear a bit in the bitmap; returns `true` if atomically transitioned from 0 to 1 (or 1 to 0),
// and false otherwise leaving the bitmask as is.
bool mi_bitmap_try_xset(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < MI_BITMAP_MAX_BITS);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx      = idx % MI_BITMAP_CHUNK_BITS;
  bool ok = mi_bitmap_chunk_try_xset( set, &bitmap->chunks[chunk_idx], cidx);
  if (ok) { mi_bitmap_update_anyset(set, bitmap, chunk_idx); }
  return ok;
}

// Try to set/clear a byte in the bitmap; returns `true` if atomically transitioned from 0 to 0xFF (or 0xFF to 0)
// and false otherwise leaving the bitmask as is.
bool mi_bitmap_try_xset8(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx) {
  mi_assert_internal(idx < MI_BITMAP_MAX_BITS);
  mi_assert_internal(idx%8 == 0);
  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t byte_idx  = (idx % MI_BITMAP_CHUNK_BITS)/8;
  bool ok = mi_bitmap_chunk_try_xset8( set, &bitmap->chunks[chunk_idx],byte_idx);
  if (ok) { mi_bitmap_update_anyset(set, bitmap, chunk_idx); }
  return ok;
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

  bool ok = mi_bitmap_chunk_try_xsetN( set, &bitmap->chunks[chunk_idx], cidx, n);
  if (ok) { mi_bitmap_update_anyset(set, bitmap, chunk_idx); }
  return ok;
}

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
bool mi_bitmap_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, bool* already_xset) {
  mi_assert_internal(n>0);
  mi_assert_internal(n<=MI_BITMAP_CHUNK_BITS);
  bool local_already_xset;
  if (already_xset==NULL) { already_xset = &local_already_xset;  }
  // if (n==1) { return mi_bitmap_xset(set, bitmap, idx); }
  // if (n==8) { return mi_bitmap_xset8(set, bitmap, idx); }
  mi_assert_internal(idx + n <= MI_BITMAP_MAX_BITS);

  const size_t chunk_idx = idx / MI_BITMAP_CHUNK_BITS;
  const size_t cidx = idx % MI_BITMAP_CHUNK_BITS;
  mi_assert_internal(cidx + n <= MI_BITMAP_CHUNK_BITS);  // don't cross chunks (for now)
  mi_assert_internal(chunk_idx < MI_BFIELD_BITS);
  if (cidx + n > MI_BITMAP_CHUNK_BITS) { n = MI_BITMAP_CHUNK_BITS - cidx; }  // paranoia

  const bool allx = mi_bitmap_chunk_xsetN(set, &bitmap->chunks[chunk_idx], cidx, n, already_xset);
  mi_bitmap_update_anyset(set, bitmap, chunk_idx);
  return allx;
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


#define mi_bitmap_forall_set_chunks(bitmap,tseq,decl_chunk_idx) \
  { size_t _set_idx; \
    size_t _start = tseq % MI_BFIELD_BITS; \
    mi_bfield_t _any_set = mi_bfield_rotate_right(bitmap->any_set, _start); \
    while (mi_bfield_find_least_bit(_any_set,&_set_idx)) { \
      decl_chunk_idx = (_set_idx + _start) % MI_BFIELD_BITS;

#define mi_bitmap_forall_set_chunks_end() \
      _start += _set_idx+1;    /* so chunk_idx stays valid */ \
      _any_set >>= _set_idx;   /* skip scanned bits (and avoid UB with (idx+1)) */ \
      _any_set >>= 1; \
    } \
  }

// Find a set bit in a bitmap and atomically unset it. Returns true on success,
// and in that case sets the index: `0 <= *pidx < MI_BITMAP_MAX_BITS`.
// The low `MI_BFIELD_BITS` of start are used to set the start point of the search
// (to reduce thread contention).
bool mi_bitmap_try_find_and_clear(mi_bitmap_t* bitmap, size_t tseq, size_t* pidx) {
  mi_bitmap_forall_set_chunks(bitmap,tseq,size_t chunk_idx)
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
      if (mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
        mi_bfield_atomic_xset(MI_BIT_CLEAR,&bitmap->any_set,chunk_idx);
      }
    }
  }
  mi_bitmap_forall_set_chunks_end();
  return false;
}


// Find a byte in the bitmap with all bits set (0xFF) and atomically unset it to zero.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-8`.
bool mi_bitmap_try_find_and_clear8(mi_bitmap_t* bitmap, size_t tseq, size_t* pidx ) {
  mi_bitmap_forall_set_chunks(bitmap,tseq,size_t chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bitmap_chunk_find_and_try_clear8(&bitmap->chunks[chunk_idx],&cidx)) {
      *pidx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
      mi_assert_internal(*pidx <= MI_BITMAP_MAX_BITS-8);
      mi_assert_internal((*pidx % 8) == 0);
      return true;
    }
    else {
      if (mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
        mi_bfield_atomic_xset(MI_BIT_CLEAR,&bitmap->any_set,chunk_idx);
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
  if (n == 1) return mi_bitmap_try_find_and_clear(bitmap, tseq, pidx);
  if (n == 8) return mi_bitmap_try_find_and_clear8(bitmap, tseq, pidx);

  mi_bitmap_forall_set_chunks(bitmap,tseq,size_t chunk_idx)
  {
    size_t cidx;
    if mi_likely(mi_bitmap_chunk_find_and_try_clearN(&bitmap->chunks[chunk_idx],n,&cidx)) {
      *pidx = (chunk_idx * MI_BITMAP_CHUNK_BITS) + cidx;
      mi_assert_internal(*pidx <= MI_BITMAP_MAX_BITS-n);
      return true;
    }
    else {
      if (mi_bitmap_chunk_all_are_clear(&bitmap->chunks[chunk_idx])) {
        mi_bfield_atomic_xset(MI_BIT_CLEAR,&bitmap->any_set,chunk_idx);
      }
    }
  }
  mi_bitmap_forall_set_chunks_end();
  return false;
}
