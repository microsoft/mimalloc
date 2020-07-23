/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_ATOMIC_H
#define MIMALLOC_ATOMIC_H

// ------------------------------------------------------
// Atomics
// We need to be portable between C, C++, and MSVC.
// ------------------------------------------------------

#if defined(_MSC_VER)
#define _Atomic(tp)         tp
#define ATOMIC_VAR_INIT(x)  x
#elif defined(__cplusplus)
#include <atomic>
#define  _Atomic(tp)        std::atomic<tp>
#else
#include <stdatomic.h>
#endif

// ------------------------------------------------------
// Atomic operations specialized for mimalloc
// ------------------------------------------------------

// Atomically add a value; returns the previous value. Memory ordering is relaxed.
static inline uintptr_t mi_atomic_add(volatile _Atomic(uintptr_t)* p, uintptr_t add);

// Atomically "and" a value; returns the previous value. Memory ordering is relaxed.
static inline uintptr_t mi_atomic_and(volatile _Atomic(uintptr_t)* p, uintptr_t x);

// Atomically "or" a value; returns the previous value. Memory ordering is relaxed.
static inline uintptr_t mi_atomic_or(volatile _Atomic(uintptr_t)* p, uintptr_t x);

// Atomically compare and exchange a value; returns `true` if successful.
// May fail spuriously. Memory ordering as release on success, and relaxed on failure.
// (Note: expected and desired are in opposite order from atomic_compare_exchange)
static inline bool mi_atomic_cas_weak(volatile _Atomic(uintptr_t)* p, uintptr_t desired, uintptr_t expected);

// Atomically compare and exchange a value; returns `true` if successful.
// Memory ordering is acquire-release
// (Note: expected and desired are in opposite order from atomic_compare_exchange)
static inline bool mi_atomic_cas_strong(volatile _Atomic(uintptr_t)* p, uintptr_t desired, uintptr_t expected);

// Atomically exchange a value. Memory ordering is acquire-release.
static inline uintptr_t mi_atomic_exchange(volatile _Atomic(uintptr_t)* p, uintptr_t exchange);

// Atomically read a value. Memory ordering is relaxed.
static inline uintptr_t mi_atomic_read_relaxed(const volatile _Atomic(uintptr_t)* p);

// Atomically read a value. Memory ordering is acquire.
static inline uintptr_t mi_atomic_read(const volatile _Atomic(uintptr_t)* p);

// Atomically write a value. Memory ordering is release.
static inline void mi_atomic_write(volatile _Atomic(uintptr_t)* p, uintptr_t x);

// Yield
static inline void mi_atomic_yield(void);

// Atomically add a 64-bit value; returns the previous value.
// Note: not using _Atomic(int64_t) as it is only used for statistics.
static inline void mi_atomic_addi64(volatile int64_t* p, int64_t add);

// Atomically update `*p` with the maximum of `*p` and `x` as a 64-bit value.
// Returns the previous value. Note: not using _Atomic(int64_t) as it is only used for statistics.
static inline void mi_atomic_maxi64(volatile int64_t* p, int64_t x);

// Atomically read a 64-bit value
// Note: not using _Atomic(int64_t) as it is only used for statistics.
static inline int64_t mi_atomic_readi64(volatile int64_t* p);

// Atomically subtract a value; returns the previous value.
static inline uintptr_t mi_atomic_sub(volatile _Atomic(uintptr_t)* p, uintptr_t sub) {
  return mi_atomic_add(p, (uintptr_t)(-((intptr_t)sub)));
}

// Atomically increment a value; returns the incremented result.
static inline uintptr_t mi_atomic_increment(volatile _Atomic(uintptr_t)* p) {
  return mi_atomic_add(p, 1);
}

// Atomically decrement a value; returns the decremented result.
static inline uintptr_t mi_atomic_decrement(volatile _Atomic(uintptr_t)* p) {
  return mi_atomic_sub(p, 1);
}

// Atomically add a signed value; returns the previous value.
static inline intptr_t mi_atomic_addi(volatile _Atomic(intptr_t)* p, intptr_t add) {
  return (intptr_t)mi_atomic_add((volatile _Atomic(uintptr_t)*)p, (uintptr_t)add);
}

// Atomically subtract a signed value; returns the previous value.
static inline intptr_t mi_atomic_subi(volatile _Atomic(intptr_t)* p, intptr_t sub) {
  return (intptr_t)mi_atomic_addi(p,-sub);
}

// Atomically read a pointer; Memory order is relaxed (i.e. no fence, only atomic).
#define mi_atomic_read_ptr_relaxed(T,p)  \
  (T*)(mi_atomic_read_relaxed((const volatile _Atomic(uintptr_t)*)(p)))

// Atomically read a pointer; Memory order is acquire.
#define mi_atomic_read_ptr(T,p) \
  (T*)(mi_atomic_read((const volatile _Atomic(uintptr_t)*)(p)))

// Atomically write a pointer; Memory order is acquire.
#define mi_atomic_write_ptr(T,p,x) \
  mi_atomic_write((volatile _Atomic(uintptr_t)*)(p), (uintptr_t)((T*)x))

// Atomically compare and exchange a pointer; returns `true` if successful. May fail spuriously.
// Memory order is release. (like a write)
// (Note: expected and desired are in opposite order from atomic_compare_exchange)
#define mi_atomic_cas_ptr_weak(T,p,desired,expected) \
  mi_atomic_cas_weak((volatile _Atomic(uintptr_t)*)(p), (uintptr_t)((T*)(desired)), (uintptr_t)((T*)(expected)))
    
// Atomically compare and exchange a pointer; returns `true` if successful. Memory order is acquire_release.
// (Note: expected and desired are in opposite order from atomic_compare_exchange)
#define mi_atomic_cas_ptr_strong(T,p,desired,expected) \
  mi_atomic_cas_strong((volatile _Atomic(uintptr_t)*)(p),(uintptr_t)((T*)(desired)), (uintptr_t)((T*)(expected))) 

// Atomically exchange a pointer value.
#define mi_atomic_exchange_ptr(T,p,exchange) \
  (T*)mi_atomic_exchange((volatile _Atomic(uintptr_t)*)(p), (uintptr_t)((T*)exchange))


#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#ifdef _WIN64
typedef LONG64   msc_intptr_t;
#define MI_64(f) f##64
#else
typedef LONG     msc_intptr_t;
#define MI_64(f) f
#endif
static inline uintptr_t mi_atomic_add(volatile _Atomic(uintptr_t)* p, uintptr_t add) {
  return (uintptr_t)MI_64(_InterlockedExchangeAdd)((volatile msc_intptr_t*)p, (msc_intptr_t)add);
}
static inline uintptr_t mi_atomic_and(volatile _Atomic(uintptr_t)* p, uintptr_t x) {
  return (uintptr_t)MI_64(_InterlockedAnd)((volatile msc_intptr_t*)p, (msc_intptr_t)x);
}
static inline uintptr_t mi_atomic_or(volatile _Atomic(uintptr_t)* p, uintptr_t x) {
  return (uintptr_t)MI_64(_InterlockedOr)((volatile msc_intptr_t*)p, (msc_intptr_t)x);
}
static inline bool mi_atomic_cas_strong(volatile _Atomic(uintptr_t)* p, uintptr_t desired, uintptr_t expected) {
  return (expected == (uintptr_t)MI_64(_InterlockedCompareExchange)((volatile msc_intptr_t*)p, (msc_intptr_t)desired, (msc_intptr_t)expected));
}
static inline bool mi_atomic_cas_weak(volatile _Atomic(uintptr_t)* p, uintptr_t desired, uintptr_t expected) {
  return mi_atomic_cas_strong(p,desired,expected);
}
static inline uintptr_t mi_atomic_exchange(volatile _Atomic(uintptr_t)* p, uintptr_t exchange) {
  return (uintptr_t)MI_64(_InterlockedExchange)((volatile msc_intptr_t*)p, (msc_intptr_t)exchange);
}
static inline uintptr_t mi_atomic_read(volatile _Atomic(uintptr_t) const* p) {
  return *p;
}
static inline uintptr_t mi_atomic_read_relaxed(volatile _Atomic(uintptr_t) const* p) {
  return *p;
}
static inline void mi_atomic_write(volatile _Atomic(uintptr_t)* p, uintptr_t x) {
  #if defined(_M_IX86) || defined(_M_X64)
  *p = x;
  #else
  mi_atomic_exchange(p,x);
  #endif
}
static inline void mi_atomic_yield(void) {
  YieldProcessor();
}
static inline void mi_atomic_addi64(volatile _Atomic(int64_t)* p, int64_t add) {
  #ifdef _WIN64
  mi_atomic_addi(p,add);
  #else
  int64_t current;
  int64_t sum;
  do {
    current = *p;
    sum = current + add;
  } while (_InterlockedCompareExchange64(p, sum, current) != current);
  #endif
}

static inline void mi_atomic_maxi64(volatile _Atomic(int64_t)*p, int64_t x) {
  int64_t current;
  do {
    current = *p;
  } while (current < x && _InterlockedCompareExchange64(p, x, current) != current);
}

static inline int64_t mi_atomic_readi64(volatile _Atomic(int64_t)*p) {
  #ifdef _WIN64
  return *p;
  #else
  int64_t current;
  do {
    current = *p;
  } while (_InterlockedCompareExchange64(p, current, current) != current);
  return current;
  #endif
}

#else
#ifdef __cplusplus
#define  MI_USING_STD   using namespace std;
#else
#define  MI_USING_STD
#endif
static inline uintptr_t mi_atomic_add(volatile _Atomic(uintptr_t)* p, uintptr_t add) {
  MI_USING_STD
  return atomic_fetch_add_explicit(p, add, memory_order_relaxed);
}
static inline uintptr_t mi_atomic_and(volatile _Atomic(uintptr_t)* p, uintptr_t x) {
  MI_USING_STD
  return atomic_fetch_and_explicit(p, x, memory_order_acq_rel);
}
static inline uintptr_t mi_atomic_or(volatile _Atomic(uintptr_t)* p, uintptr_t x) {
  MI_USING_STD
  return atomic_fetch_or_explicit(p, x, memory_order_acq_rel);
}
static inline bool mi_atomic_cas_weak(volatile _Atomic(uintptr_t)* p, uintptr_t desired, uintptr_t expected) {
  MI_USING_STD
  return atomic_compare_exchange_weak_explicit(p, &expected, desired, memory_order_acq_rel, memory_order_acquire);
}
static inline bool mi_atomic_cas_strong(volatile _Atomic(uintptr_t)* p, uintptr_t desired, uintptr_t expected) {
  MI_USING_STD
  return atomic_compare_exchange_strong_explicit(p, &expected, desired, memory_order_acq_rel, memory_order_acquire);
}
static inline uintptr_t mi_atomic_exchange(volatile _Atomic(uintptr_t)* p, uintptr_t exchange) {
  MI_USING_STD
  return atomic_exchange_explicit(p, exchange, memory_order_acq_rel);
}
static inline uintptr_t mi_atomic_read_relaxed(const volatile _Atomic(uintptr_t)* p) {
  MI_USING_STD
  return atomic_load_explicit((volatile _Atomic(uintptr_t)*) p, memory_order_relaxed);
}
static inline uintptr_t mi_atomic_read(const volatile _Atomic(uintptr_t)* p) {
  MI_USING_STD
  return atomic_load_explicit((volatile _Atomic(uintptr_t)*) p, memory_order_acquire);
}
static inline void mi_atomic_write(volatile _Atomic(uintptr_t)* p, uintptr_t x) {
  MI_USING_STD
  return atomic_store_explicit(p, x, memory_order_release);
}
static inline void mi_atomic_addi64(volatile int64_t* p, int64_t add) {
  MI_USING_STD
  atomic_fetch_add_explicit((volatile _Atomic(int64_t)*)p, add, memory_order_relaxed);
}
static inline int64_t mi_atomic_readi64(volatile int64_t* p) {
  MI_USING_STD
  return atomic_load_explicit((volatile _Atomic(int64_t)*) p, memory_order_relaxed);
}
static inline void mi_atomic_maxi64(volatile int64_t* p, int64_t x) {
  MI_USING_STD
  int64_t current;
  do {
    current = mi_atomic_readi64(p);
  } while (current < x && !atomic_compare_exchange_weak_explicit((volatile _Atomic(int64_t)*)p, &current, x, memory_order_acq_rel, memory_order_relaxed));
}

#if defined(__cplusplus)
  #include <thread>
  static inline void mi_atomic_yield(void) {
    std::this_thread::yield();
  }
#elif (defined(__GNUC__) || defined(__clang__)) && \
      (defined(__x86_64__) || defined(__i386__) || defined(__arm__) || defined(__aarch64__))
#if defined(__x86_64__) || defined(__i386__)
  static inline void mi_atomic_yield(void) {
    asm volatile ("pause" ::: "memory");
  }
#elif defined(__arm__) || defined(__aarch64__)
  static inline void mi_atomic_yield(void) {
    asm volatile("yield");
  }
#endif
#elif defined(__wasi__)
  #include <sched.h>
  static inline void mi_atomic_yield(void) {
    sched_yield();
  }
#else
  #include <unistd.h>
  static inline void mi_atomic_yield(void) {
    sleep(0);
  }
#endif

#endif

#endif // __MIMALLOC_ATOMIC_H
