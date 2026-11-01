/* ----------------------------------------------------------------------------
Copyright (c) 2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MI_PAC_H
#define MI_PAC_H

#include "types.h"
#include "heap/process.h"   // mi_process_get_sealed_once

/* -----------------------------------------------------------------------------
Encoded pointers (`MI_PAC`, see `arch.h`)

Some internal pointers (currently the page map entries) are stored as an encoded
`mi_ptr_pac_t` that is bound to the address of the slot it is stored in (the `salt`).
`NULL` is always encoded as 0 (so zero initialized memory decodes as `NULL`).

With memory tagging (`MI_MTE`) an encoded pointer `p` must be `NULL` or aligned to
`MI_MEMTAG_UNIT_SIZE` (which holds for any tagged object start): the memory tag is moved
into the low 4 bits before encoding (`q = mi_ptr_pac_untag(p)`) such that the top 16 bits
are available for the authentication code (and the tag is still authenticated).

- `MI_PAC=0` (off): the identity.
- `MI_PAC=1` (weak): `x = q ^ K ^ T(salt)` where `K` is a random process key in the sealed-once
  process data, and `T(salt) = salt >>> 18` rotates the (8-byte aligned) slot address such that its
  bits 3 to 17 end up in bits 49 to 63. A decoded pointer must be canonical: the top 16 bits must be zero.
  This detects most forged values (if `K` is unknown), and always detects an entry that is copied to
  another slot in the same sub map. Since `K` always has bit 48 set (and `T(salt)` never has), the
  encoding of `NULL` (0) never decodes as canonical and is handled on the slow path.
- `MI_PAC=2` (on):
  - with the IB key (`MI_PAC_IB`, not on Apple): `x = pacib(q, salt') ^ K ^ T(salt)` using the
    arm64 pointer authentication hint instructions; these are a no-op if the CPU or OS does not
    support the IB key in which case it is equivalent to weak.
  - with the generic key (`MI_PAC_GA`, on Apple, where the IB key is only enabled for arm64e
    processes): `x = q | (pacga(q, salt) & MI_PAC_HI_MASK)`. All Apple silicon Macs support
    pointer authentication (checked at startup).
- `MI_PAC=3` (required): as on, but without the process key (as pointer authentication is
  checked at startup to be supported and enabled).

The free list links are always encoded with a MAC using per-page keys (see `alloc/page.h`).
With `MI_PAC` the MAC is extended with the process key `K` such that the free list cannot be
forged from just reading the page keys. With `MI_PAC=3` or `MI_PAC_GA` the MAC is `pacga(v, b ^ keys[0])`
instead, and the page offset is stored unmasked (as the page keys need not be secret).
Decoding failures report `EFAULT` and return `NULL` (see `_mi_ptr_pac_invalid`).
----------------------------------------------------------------------------- */

#if MI_PAC

// the top 16 bits must be zero in a decoded pointer (after moving the memory tag to the low bits)
#define MI_PAC_ADDR_BITS    (48)
#define MI_PAC_HI_MASK      (~MI_ADDR(0) << MI_PAC_ADDR_BITS)

#if MI_MAX_VABITS > MI_PAC_ADDR_BITS
#error "encoded pointers (MI_PAC) require at most 48-bit virtual addresses"
#endif

mi_decl_cold void* _mi_ptr_pac_invalid(mi_ptr_pac_t x, mi_addr_t salt);  // slow path: returns NULL for x==0, and reports EFAULT otherwise
mi_decl_cold void  _mi_ptr_pac_unaligned(const void* p);                 // fatal error for an unaligned pointer with memory tagging

// Move the memory tag into the low bits (and back)
static inline mi_addr_t mi_ptr_pac_untag(const void* p) {
  #if MI_MTE
  const mi_addr_t u = (mi_addr_t)(uintptr_t)p;
  if mi_unlikely((u % MI_MEMTAG_UNIT_SIZE) != 0) { _mi_ptr_pac_unaligned(p); }
  return ((u & MI_PTR_ADDR_MASK) | ((u & MI_PTR_MEMTAG_MASK) >> MI_PTR_MEMTAG_SHIFT));
  #else
  return (mi_addr_t)(uintptr_t)p;
  #endif
}

static inline void* mi_ptr_pac_retag(mi_addr_t q) {
  #if MI_MTE
  const mi_addr_t t = (q % MI_MEMTAG_UNIT_SIZE);
  return (void*)(uintptr_t)((q ^ t) | (t << MI_PTR_MEMTAG_SHIFT));
  #else
  return (void*)(uintptr_t)q;
  #endif
}

#if MI_PAC_KEY
#define MI_PAC_KEY_ALWAYS   (MI_ADDR(1) << MI_PAC_ADDR_BITS)    // always set in the key so 0 never decodes as canonical

// the tweak for a slot address
static inline mi_addr_t mi_ptr_pac_tweak(mi_addr_t salt) {
  mi_assert_internal((salt & 4) == 0);  // so bit `MI_PAC_ADDR_BITS` is never set
  return mi_rotr(salt, 18);             // bit 2 to bit 48, and bits 3 to 17 to bits 49 to 63
}

static inline mi_addr_t mi_ptr_pac_key(void) {
  mi_assert_internal(mi_process_get_sealed_once()->ptr_pac_key != 0);  // initialized in `_mi_os_init`
  return mi_process_get_sealed_once()->ptr_pac_key;
}

// the combined key for a slot
static inline mi_addr_t mi_ptr_pac_slot_key(mi_addr_t salt) {
  return (mi_ptr_pac_key() ^ mi_ptr_pac_tweak(salt));
}
#endif

#if MI_PAC_IB
// a high constant in the modifier such that it is distinct from any stack pointer
#define MI_PAC_MODIFIER   (MI_ADDR(0x6D69) << MI_PAC_ADDR_BITS)

static inline mi_addr_t mi_pac_sign(mi_addr_t p, mi_addr_t modifier) {
  register mi_addr_t x17 __asm__("x17") = p;
  register mi_addr_t x16 __asm__("x16") = modifier;
  __asm__ ("hint #10" : "+r"(x17) : "r"(x16));     // pacib1716
  return x17;
}

static inline mi_addr_t mi_pac_auth(mi_addr_t p, mi_addr_t modifier) {
  register mi_addr_t x17 __asm__("x17") = p;
  register mi_addr_t x16 __asm__("x16") = modifier;
  __asm__ ("hint #14" : "+r"(x17) : "r"(x16));     // autib1716
  return x17;
}
#endif

#if MI_PAC_GA || (MI_PAC >= 3)
#define MI_PAC_USE_GA_MAC  1
// generic authentication code (in the high 32 bits); requires pointer authentication support (not a hint instruction)
static inline mi_addr_t mi_pac_ga(mi_addr_t x, mi_addr_t modifier) {
  register mi_addr_t x17 __asm__("x17") = x;
  register mi_addr_t x16 __asm__("x16") = modifier;
  __asm__ (".inst 0x9ad03231" : "+r"(x17) : "r"(x16));  // pacga x17, x17, x16
  return x17;
}
#endif

// Encode a pointer `p` to be stored at address `salt`
static inline mi_ptr_pac_t mi_ptr_pac_encode(const void* p, mi_addr_t salt) {
  if (p==NULL) return 0;
  mi_addr_t x = mi_ptr_pac_untag(p);
  #if MI_PAC_GA
  x = x | (mi_pac_ga(x, salt) & MI_PAC_HI_MASK);
  #elif MI_PAC_IB
  x = mi_pac_sign(x, salt ^ MI_PAC_MODIFIER);
  #endif
  #if MI_PAC_KEY
  x = x ^ mi_ptr_pac_slot_key(salt);
  #endif
  return (mi_ptr_pac_t)x;
}

// Decode a value `x` stored at address `salt`; `valid` is false if `x` is 0 or not a valid encoding
// (in which case `_mi_ptr_pac_invalid` should be called)
static inline void* mi_ptr_pac_decode_valid(mi_ptr_pac_t x, mi_addr_t salt, bool* valid) {
  #if MI_PAC_GA
  // no special case for 0: it either fails the authentication, or decodes as NULL
  const mi_addr_t q = (mi_addr_t)x & ~MI_PAC_HI_MASK;
  *valid = (((mi_pac_ga(q, salt) ^ (mi_addr_t)x) & MI_PAC_HI_MASK) == 0);
  #else
  #if MI_PAC_IB
  if (x==0) { *valid = false; return NULL; }  // as the authentication of 0 may trap with FPAC
  #endif
  mi_addr_t q = (mi_addr_t)x;
  #if MI_PAC_KEY
  q = q ^ mi_ptr_pac_slot_key(salt);
  #endif
  #if MI_PAC_IB
  q = mi_pac_auth(q, salt ^ MI_PAC_MODIFIER);
  #endif
  *valid = ((q & MI_PAC_HI_MASK) == 0);
  #endif
  return mi_ptr_pac_retag(q);
}

// Keyed MAC for free list links (see `alloc/page.h:mi_ptr_mac`); only the high 32 bits are used
// (and with memory tagging also bits 24 to 31).
static inline mi_addr_t mi_ptr_pac_mac(mi_addr_t x, mi_addr_t y) {
  #if MI_PAC_USE_GA_MAC
  const mi_addr_t m = mi_pac_ga(x, y);
  return (m | (m >> 32));
  #else
  return mi_mul_xor64(x, y ^ mi_ptr_pac_key());
  #endif
}

#else // !MI_PAC

static inline void* _mi_ptr_pac_invalid(mi_ptr_pac_t x, mi_addr_t salt) {
  MI_UNUSED(x); MI_UNUSED(salt);
  return NULL;
}

static inline mi_ptr_pac_t mi_ptr_pac_encode(const void* p, mi_addr_t salt) {
  MI_UNUSED(salt);
  return (mi_ptr_pac_t)p;
}

static inline void* mi_ptr_pac_decode_valid(mi_ptr_pac_t x, mi_addr_t salt, bool* valid) {
  MI_UNUSED(salt);
  *valid = true;
  return (void*)x;
}

#if MI_SIZE_BITS == 64
static inline mi_addr_t mi_ptr_pac_mac(mi_addr_t x, mi_addr_t y) {
  return mi_mul_xor64(x, y);
}
#endif

#endif // MI_PAC

// Decode a value `x` stored at address `salt`; returns NULL (and reports `EFAULT`) if `x` is not a valid encoding
static inline void* mi_ptr_pac_decode(mi_ptr_pac_t x, mi_addr_t salt) {
  bool valid;
  void* const p = mi_ptr_pac_decode_valid(x, salt, &valid);
  if mi_unlikely(!valid) return _mi_ptr_pac_invalid(x, salt);
  return p;
}

#endif // MI_PAC_H
