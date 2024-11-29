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
  Definitions
-------------------------------------------------------------------------------- */

typedef size_t mi_bfield_t;

#define MI_BFIELD_BITS_SHIFT               (MI_SIZE_SHIFT+3)
#define MI_BFIELD_BITS                     (1 << MI_BFIELD_BITS_SHIFT)
#define MI_BFIELD_SIZE                     (MI_BFIELD_BITS/8)
#define MI_BFIELD_BITS_MOD_MASK            (MI_BFIELD_BITS - 1)
#define MI_BFIELD_LO_BIT8                  ((~(mi_bfield_t(0)))/0xFF)         // 0x01010101 ..
#define MI_BFIELD_HI_BIT8                  (MI_BFIELD_LO_BIT8 << 7)           // 0x80808080 ..

#define MI_BITMAP_CHUNK_FIELDS             (MI_BITMAP_CHUNK_BITS / MI_BFIELD_BITS)
#define MI_BITMAP_CHUNK_BITS_MOD_MASK      (MI_BITMAP_CHUNK_BITS - 1)

typedef mi_decl_align(32) struct mi_bitmap_chunk_s {
  _Atomic(mi_bfield_t) bfields[MI_BITMAP_CHUNK_FIELDS];
} mi_bitmap_chunk_t;


typedef mi_decl_align(32) struct mi_bitmap_s {
  mi_bitmap_chunk_t chunks[MI_BFIELD_BITS];
  _Atomic(mi_bfield_t)any_set;
} mi_bitmap_t;

#define MI_BITMAP_MAX_BITS  (MI_BFIELD_BITS * MI_BITMAP_CHUNK_BITS)      // 16k bits on 64bit, 8k bits on 32bit

/* --------------------------------------------------------------------------------
  Bitmap
-------------------------------------------------------------------------------- */

typedef bool  mi_bit_t;
#define MI_BIT_SET    (true)
#define MI_BIT_CLEAR  (false)

// initialize a bitmap to all unset; avoid a mem_zero if `already_zero` is true
void mi_bitmap_init(mi_bitmap_t* bitmap, bool already_zero);

// Set/clear a sequence of `n` bits in the bitmap (and can cross chunks). Not atomic so only use if local to a thread.
void mi_bitmap_unsafe_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n);

// Set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from all 0's to 1's (or all 1's to 0's).
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
// If `already_xset` is not NULL, it is set to true if all the bits were already all set/cleared.
bool mi_bitmap_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n, bool* already_xset);

// Is a sequence of n bits already all set/cleared?
bool mi_bitmap_is_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n);

// Try to set/clear a bit in the bitmap; returns `true` if atomically transitioned from 0 to 1 (or 1 to 0)
// and false otherwise leaving the bitmask as is.
mi_decl_nodiscard bool mi_bitmap_try_xset(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx);

// Try to set/clear a byte in the bitmap; returns `true` if atomically transitioned from 0 to 0xFF (or 0xFF to 0)
// and false otherwise leaving the bitmask as is.
mi_decl_nodiscard bool mi_bitmap_try_xset8(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx);

// Try to set/clear a sequence of `n` bits in the bitmap; returns `true` if atomically transitioned from 0's to 1's (or 1's to 0's)
// and false otherwise leaving the bitmask as is.
// `n` cannot cross chunk boundaries (and `n <= MI_BITMAP_CHUNK_BITS`)!
mi_decl_nodiscard bool mi_bitmap_try_xsetN(mi_bit_t set, mi_bitmap_t* bitmap, size_t idx, size_t n);

// Find a set bit in a bitmap and atomically unset it. Returns true on success,
// and in that case sets the index: `0 <= *pidx < MI_BITMAP_MAX_BITS`.
// The low `MI_BFIELD_BITS` of start are used to set the start point of the search
// (to reduce thread contention).
mi_decl_nodiscard bool mi_bitmap_try_find_and_clear(mi_bitmap_t* bitmap, size_t tseq, size_t* pidx);

// Find a byte in the bitmap with all bits set (0xFF) and atomically unset it to zero.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-8`.
mi_decl_nodiscard bool mi_bitmap_try_find_and_clear8(mi_bitmap_t* bitmap, size_t tseq, size_t* pidx );

// Find a sequence of `n` bits in the bitmap with all bits set, and atomically unset all.
// Returns true on success, and in that case sets the index: `0 <= *pidx <= MI_BITMAP_MAX_BITS-n`.
mi_decl_nodiscard bool mi_bitmap_try_find_and_clearN(mi_bitmap_t* bitmap, size_t n, size_t tseq, size_t* pidx );

#endif // MI_XBITMAP_H
