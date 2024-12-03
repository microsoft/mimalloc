/* ----------------------------------------------------------------------------
Copyright (c) 2019-2024 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
  Bit operation, and platform dependent definition (MI_INTPTR_SIZE etc)
---------------------------------------------------------------------------- */

#pragma once
#ifndef MI_BITS_H
#define MI_BITS_H


// ------------------------------------------------------
// Size of a pointer.
// We assume that `sizeof(void*)==sizeof(intptr_t)`
// and it holds for all platforms we know of.
//
// However, the C standard only requires that:
//  p == (void*)((intptr_t)p))
// but we also need:
//  i == (intptr_t)((void*)i)
// or otherwise one might define an intptr_t type that is larger than a pointer...
// ------------------------------------------------------

#if INTPTR_MAX > INT64_MAX
# define MI_INTPTR_SHIFT (4)  // assume 128-bit  (as on arm CHERI for example)
#elif INTPTR_MAX == INT64_MAX
# define MI_INTPTR_SHIFT (3)
#elif INTPTR_MAX == INT32_MAX
# define MI_INTPTR_SHIFT (2)
#else
#error platform pointers must be 32, 64, or 128 bits
#endif

#if SIZE_MAX == UINT64_MAX
# define MI_SIZE_SHIFT (3)
typedef int64_t  mi_ssize_t;
#elif SIZE_MAX == UINT32_MAX
# define MI_SIZE_SHIFT (2)
typedef int32_t  mi_ssize_t;
#else
#error platform objects must be 32 or 64 bits
#endif

#if (SIZE_MAX/2) > LONG_MAX
# define MI_ZU(x)  x##ULL
# define MI_ZI(x)  x##LL
#else
# define MI_ZU(x)  x##UL
# define MI_ZI(x)  x##L
#endif

#define MI_INTPTR_SIZE  (1<<MI_INTPTR_SHIFT)
#define MI_INTPTR_BITS  (MI_INTPTR_SIZE*8)

#define MI_SIZE_SIZE  (1<<MI_SIZE_SHIFT)
#define MI_SIZE_BITS  (MI_SIZE_SIZE*8)

#define MI_KiB     (MI_ZU(1024))
#define MI_MiB     (MI_KiB*MI_KiB)
#define MI_GiB     (MI_MiB*MI_KiB)


/* --------------------------------------------------------------------------------
  Architecture
-------------------------------------------------------------------------------- */

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
#define MI_ARCH_X64       1
#elif defined(__i386__) || defined(__i386) || defined(_M_IX86) || defined(_X86_) || defined(__X86__)
#define MI_ARCH_X86       1
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC)
#define MI_ARCH_ARM64     1
#elif defined(__arm__) || defined(_ARM) || defined(_M_ARM)  || defined(_M_ARMT) || defined(__arm)
#define MI_ARCH_ARM32     1
#elif defined(__riscv) || defined(_M_RISCV)
#define MI_ARCH_RISCV     1
#if (LONG_MAX == INT32_MAX)
#define MI_ARCH_RISCV32   1
#else
#define MI_ARCH_RISCV64   1
#endif
#endif

#if MI_ARCH_X64 && defined(__AVX2__)
#include <immintrin.h>
#endif
#if defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
#include <intrin.h>
#endif

#if defined(__AVX2__) && !defined(__BMI2__) // msvc
#define __BMI2__  1
#endif
#if (defined(__AVX2__) || defined(__BMI2__)) && !defined(__BMI1__) // msvc
#define __BMI1__  1
#endif

// Define big endian if needed
// #define MI_BIG_ENDIAN  1


/* --------------------------------------------------------------------------------
  Builtin's
-------------------------------------------------------------------------------- */

#ifndef __has_builtin
#define __has_builtin(x)  0
#endif

#define mi_builtin(name)        __builtin_##name
#define mi_has_builtin(name)    __has_builtin(__builtin_##name)

#if (LONG_MAX == INT32_MAX)
#define mi_builtin32(name)       mi_builtin(name##l)
#define mi_has_builtin32(name)   mi_has_builtin(name##l)
#else
#define mi_builtin32(name)       mi_builtin(name)
#define mi_has_builtin32(name)   mi_has_builtin(name)
#endif
#if (LONG_MAX == INT64_MAX)
#define mi_builtin64(name)       mi_builtin(name##l)
#define mi_has_builtin64(name)   mi_has_builtin(name##l)
#else
#define mi_builtin64(name)       mi_builtin(name##ll)
#define mi_has_builtin64(name)   mi_has_builtin(name##ll)
#endif

#if (MI_SIZE_BITS == 32)
#define mi_builtin_size(name)       mi_builtin32(name)
#define mi_has_builtin_size(name)   mi_has_builtin32(name)
#elif (MI_SIZE_BITS == 64)
#define mi_builtin_size(name)       mi_builtin64(name)
#define mi_has_builtin_size(name)   mi_has_builtin64(name)
#endif


/* --------------------------------------------------------------------------------
  Count trailing/leading zero's
-------------------------------------------------------------------------------- */

size_t _mi_clz_generic(size_t x);
size_t _mi_ctz_generic(size_t x);
uint32_t _mi_ctz_generic32(uint32_t x);

static inline size_t mi_ctz(size_t x) {
  #if defined(__GNUC__) && MI_ARCH_X64 && defined(__BMI1__)  // on x64 tzcnt is defined for 0
    uint64_t r;
    __asm volatile ("tzcnt\t%1, %0" : "=&r"(r) : "r"(x) : "cc");
    return r;
  #elif MI_ARCH_X64 && defined(__BMI1__)
    return (size_t)_tzcnt_u64(x);
  #elif defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
    unsigned long idx;
    #if MI_SIZE_BITS==32
      return (_BitScanForward(&idx, x) ? (size_t)idx : 32);
    #else
      return (_BitScanForward64(&idx, x) ? (size_t)idx : 64);
    #endif
  /*
  // for arm64 and riscv, the builtin_ctz is defined for 0 as well
  #elif defined(__GNUC__) && MI_ARCH_ARM64
    uint64_t r;
    __asm volatile ("rbit\t%0, %1\n\tclz\t%0, %0" : "=&r"(r) : "r"(x) : "cc");
    return r;
  #elif defined(__GNUC__) && MI_ARCH_RISCV
    size_t r;
    __asm volatile ("ctz\t%0, %1" : "=&r"(r) : "r"(x) : );
    return r;
  */
  #elif mi_has_builtin_size(ctz)
    return (x!=0 ? (size_t)mi_builtin_size(ctz)(x) : MI_SIZE_BITS);
  #else
    #define MI_HAS_FAST_BITSCAN  0
    return _mi_ctz_generic(x);
  #endif
}

static inline size_t mi_clz(size_t x) {
  #if defined(__GNUC__) && MI_ARCH_X64 && defined(__BMI1__) // on x64 lzcnt is defined for 0
    uint64_t r;
    __asm volatile ("lzcnt\t%1, %0" : "=&r"(r) : "r"(x) : "cc");
    return r;
  #elif MI_ARCH_X64 && defined(__BMI1__)
    return (size_t)_lzcnt_u64(x);
  #elif defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
    unsigned long idx;
    #if MI_SIZE_BITS==32
      return (_BitScanReverse(&idx, x) ? 31 - (size_t)idx : 32);
    #else
      return (_BitScanReverse64(&idx, x) ? 63 - (size_t)idx : 64);
    #endif
  /*
  // for arm64 and riscv, the builtin_clz is defined for 0 as well
  #elif defined(__GNUC__) && MI_ARCH_ARM64
    uint64_t r;
    __asm volatile ("clz\t%0, %1" : "=&r"(r) : "r"(x) : "cc");
    return r;
  #elif defined(__GNUC__) && MI_ARCH_RISCV
    size_t r;
    __asm volatile ("clz\t%0, %1" : "=&r"(r) : "r"(x) : );
    return r;
  */
  #elif mi_has_builtin_size(clz)
    return (x!=0 ? (size_t)mi_builtin_size(clz)(x) : MI_SIZE_BITS);
  #else
    #define MI_HAS_FAST_BITSCAN  0
    return _mi_clz_generic(x);
  #endif
}

static inline uint32_t mi_ctz32(uint32_t x) {
  #if defined(__GNUC__) && MI_ARCH_X64 && defined(__BMI1__) // on x64 tzcnt is defined for 0
    uint32_t r;
    __asm volatile ("tzcntl\t%1, %0" : "=&r"(r) : "r"(x) : "cc");
    return r;
  #elif MI_ARCH_X64 && defined(__BMI1__)
    return (uint32_t)_tzcnt_u32(x);
  #elif defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
    unsigned long idx;
    return (_BitScanForward(&idx, x) ? (uint32_t)idx : 32);
  #elif mi_has_builtin(ctz) && (INT_MAX == INT32_MAX)
    return (x!=0 ? (uint32_t)mi_builtin(ctz)(x) : 32);
  #elif mi_has_builtin(ctzl) && (LONG_MAX == INT32_MAX)
    return (x!=0 ? (uint32_t)mi_builtin(ctzl)(x) : 32);
  #else
    #define MI_HAS_FAST_BITSCAN  0
    return _mi_ctz_generic32(x);
  #endif
}

#ifndef MI_HAS_FAST_BITSCAN
#define MI_HAS_FAST_BITSCAN 1
#endif



static inline size_t mi_popcount(size_t x) {
#if mi_has_builtin_size(popcount)
  return mi_builtin_size(popcount)(x);
#elif defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
  #if MI_SIZE_BITS==32
  return __popcnt(x);
  #else
  return __popcnt64(x);
  #endif
#elif MI_ARCH_X64 && defined(__BMI1__)
    return (size_t)_mm_popcnt_u64(x);
#else
  #define MI_HAS_FAST_POPCOUNT  0
  error define generic popcount
#endif
}

#ifndef MI_HAS_FAST_POPCOUNT
#define MI_HAS_FAST_POPCOUNT 1
#endif


/* --------------------------------------------------------------------------------
  find trailing/leading zero  (bit scan forward/reverse)
-------------------------------------------------------------------------------- */

// Bit scan forward: find the least significant bit that is set (i.e. count trailing zero's)
// return false if `x==0` (with `*idx` undefined) and true otherwise,
// with the `idx` is set to the bit index (`0 <= *idx < MI_BFIELD_BITS`).
static inline bool mi_bsf(size_t x, size_t* idx) {
  #if defined(__GNUC__) && MI_ARCH_X64 && defined(__BMI1__)
    // on x64 the carry flag is set on zero which gives better codegen
    bool is_zero;
    __asm ( "tzcnt\t%2, %1" : "=@ccc"(is_zero), "=r"(*idx) : "r"(x) : "cc" );
    return !is_zero;
  #else
    *idx = mi_ctz(x);
    return (x!=0);
  #endif
}

// Bit scan forward: find the least significant bit that is set (i.e. count trailing zero's)
// return false if `x==0` (with `*idx` undefined) and true otherwise,
// with the `idx` is set to the bit index (`0 <= *idx < MI_BFIELD_BITS`).
static inline bool mi_bsf32(uint32_t x, uint32_t* idx) {
  #if defined(__GNUC__) && MI_ARCH_X64 && defined(__BMI1__)
    // on x64 the carry flag is set on zero which gives better codegen
    bool is_zero;
    __asm ("tzcntl\t%2, %1" : "=@ccc"(is_zero), "=r"(*idx) : "r"(x) : "cc");
    return !is_zero;
  #else
    *idx = mi_ctz32(x);
    return (x!=0);
  #endif
}


// Bit scan reverse: find the most significant bit that is set
// return false if `x==0` (with `*idx` undefined) and true otherwise,
// with the `idx` is set to the bit index (`0 <= *idx < MI_BFIELD_BITS`).
static inline bool mi_bsr(size_t x, size_t* idx) {
  #if defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
    unsigned long i;
    #if MI_SIZE_BITS==32
      return (_BitScanReverse(&i, x) ? (*idx = i, true) : false);
    #else
      return (_BitScanReverse64(&i, x) ? (*idx = i, true) : false);
    #endif
  #else
    const size_t r = mi_clz(x);
    *idx = (~r & (MI_SIZE_BITS - 1));
    return (x!=0);
  #endif
}



/* --------------------------------------------------------------------------------
  rotate
-------------------------------------------------------------------------------- */

static inline size_t mi_rotr(size_t x, size_t r) {
  #if (mi_has_builtin(rotateright64) && MI_SIZE_BITS==64)
    return mi_builtin(rotateright64)(x,r);
  #elif (mi_has_builtin(rotateright32) && MI_SIZE_BITS==32)
    return mi_builtin(rotateright32)(x,r);
  #elif defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
    #if MI_SIZE_BITS==32
    return _lrotr(x,(int)r);
    #else
    return _rotr64(x,(int)r);
    #endif
  #else
    // The term `(-rshift)&(BITS-1)` is written instead of `BITS - rshift` to
    // avoid UB when `rshift==0`. See <https://blog.regehr.org/archives/1063>
    const unsigned int rshift = (unsigned int)(r) & (MI_SIZE_BITS-1);
    return ((x >> rshift) | (x << ((-rshift) & (MI_SIZE_BITS-1))));
  #endif
}

static inline uint32_t mi_rotr32(uint32_t x, uint32_t r) {
  #if mi_has_builtin(rotateright32)
    return mi_builtin(rotateright32)(x, r);
  #elif defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
    return _lrotr(x, (int)r);
  #else
    // The term `(-rshift)&(BITS-1)` is written instead of `BITS - rshift` to
    // avoid UB when `rshift==0`. See <https://blog.regehr.org/archives/1063>
    const unsigned int rshift = (unsigned int)(r) & 31;
    return ((x >> rshift) | (x << ((-rshift) & 31)));
  #endif
}

static inline size_t mi_rotl(size_t x, size_t r) {
  #if (mi_has_builtin(rotateleft64) && MI_SIZE_BITS==64)
    return mi_builtin(rotateleft64)(x,r);
  #elif (mi_has_builtin(rotateleft32) && MI_SIZE_BITS==32)
    return mi_builtin(rotateleft32)(x,r);
  #elif defined(_MSC_VER) && (MI_ARCH_X64 || MI_ARCH_X86 || MI_ARCH_ARM64 || MI_ARCH_ARM32)
    #if MI_SIZE_BITS==32
    return _lrotl(x,(int)r);
    #else
    return _rotl64(x,(int)r);
    #endif
  #else
    // The term `(-rshift)&(BITS-1)` is written instead of `BITS - rshift` to
    // avoid UB when `rshift==0`. See <https://blog.regehr.org/archives/1063>
    const unsigned int rshift = (unsigned int)(r) & (MI_SIZE_BITS-1);
    return ((x << rshift) | (x >> ((-rshift) & (MI_SIZE_BITS-1))));
  #endif
}



#endif // MI_BITS_H
