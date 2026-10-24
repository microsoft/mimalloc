/* ----------------------------------------------------------------------------
Copyright (c) 2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
Simulate a CHERI purecap target on 64-bit Linux (clang only) to test
the CHERI code paths. Pointers are not actually 16 bytes, and bounds and permissions
are not enforced, but an address with bit 62 set is treated as "untagged".
Use as:
  -include test/cheri-sim.h -Wno-builtin-macro-redefined -U__SIZEOF_POINTER__ -D__SIZEOF_POINTER__=16
-----------------------------------------------------------------------------*/
#pragma once
#define __CHERI_PURE_CAPABILITY__ 1
#define __PTRADDR_TYPE__ unsigned long
#include <stdint.h>
#include <stdlib.h>

#define __builtin_cheri_address_set(c,a) ((void)(c), (void*)(uintptr_t)(a))
#define __builtin_cheri_address_get(p)   ((unsigned long)(uintptr_t)(p))
#define __builtin_cheri_tag_get(p)       ((((uintptr_t)(p)) & (1ULL<<62)) == 0)

static inline void* mi_sim_trap_untagged(const void* p) {
  if (p==NULL || !__builtin_cheri_tag_get(p)) abort();
  return (void*)p;
}
#define __builtin_cheri_bounds_set(p,n)  ((void)(n), mi_sim_trap_untagged(p))
#define __builtin_cheri_perms_and(p,m)   ((void)(m), mi_sim_trap_untagged(p))
#define __builtin_cheri_base_get(p)      ((void)(p), 0UL)
#define __builtin_cheri_length_get(p)    ((void)(p), ~0UL)   // bounds are not modelled
#define __builtin_cheri_offset_get(p)    ((void)(p), 0UL)

// exact CHERI-RISC-V 128 (MW=14) representability (Morello is about 4x more lax)
static inline unsigned long mi_sim_repr_mask(unsigned long n) {
  if (n == 0) return ~0UL;
  const int e = (64 - __builtin_clzl(n)) - 12;
  if (e <= 0) return ~0UL;
  unsigned long a = 1UL << (e+2);
  const unsigned long r = (n + a - 1) & ~(a-1);
  if ((64 - __builtin_clzl(r)) - 12 > e) { a <<= 1; }
  return ~(a-1);
}
#define __builtin_cheri_representable_alignment_mask(n)  mi_sim_repr_mask(n)
#define __builtin_cheri_round_representable_length(n)    (((n) + ~mi_sim_repr_mask(n)) & mi_sim_repr_mask(n))

#define __CHERI_CAP_PERMISSION_PERMIT_EXECUTE__           2
#define __CHERI_CAP_PERMISSION_PERMIT_SEAL__              128
#define __CHERI_CAP_PERMISSION_PERMIT_UNSEAL__            512
#define __CHERI_CAP_PERMISSION_ACCESS_SYSTEM_REGISTERS__  1024

