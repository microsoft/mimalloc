/**
 * @file gcc-4.8.5-atomic.h
 *
 * @author shadow-yuan (shadow_yuan@qq.com)
 *
 * @brief because gcc-4.8.5 does not include the file stdatomic.h
 *       so add this file to pass the compilation
 *
 */
#ifndef MI_FOR_GCC_485_ATOMIC_H_
#define MI_FOR_GCC_485_ATOMIC_H_

#define memory_order_relaxed __ATOMIC_RELAXED
#define memory_order_consume __ATOMIC_CONSUME
#define memory_order_acquire __ATOMIC_ACQUIRE
#define memory_order_release __ATOMIC_RELEASE
#define memory_order_acq_rel __ATOMIC_ACQ_REL
#define memory_order_seq_cst __ATOMIC_SEQ_CST

#define _Atomic(x) x

#define __has_include(x) (1)

#define atomic_load_explicit(p, m)     __atomic_load_n(p, m)
#define atomic_store_explicit(p, x, m) __atomic_store_n(p, x, m)

#define atomic_exchange(p, x)             __atomic_exchange_n(p, x, memory_order_seq_cst)
#define atomic_exchange_explicit(p, x, m) __atomic_exchange_n(p, x, m)

#define atomic_compare_exchange_weak_explicit(p, expected, desired, mem_success, mem_fail)         \
    __atomic_compare_exchange_n(p, expected, desired, 1, mem_success, mem_fail)

#define atomic_compare_exchange_strong_explicit(p, expected, desired, mem_success, mem_fail)       \
    __atomic_compare_exchange_n(p, expected, desired, 0, mem_success, mem_fail)

#define atomic_fetch_add_explicit(p, x, m) __atomic_fetch_add(p, x, m)
#define atomic_fetch_sub_explicit(p, x, m) __atomic_fetch_sub(p, x, m)
#define atomic_fetch_and_explicit(p, x, m) __atomic_fetch_and(p, x, m)
#define atomic_fetch_or_explicit(p, x, m)  __atomic_fetch_or(p, x, m)

#define ATOMIC_VAR_INIT(x) (x)

#endif // MI_FOR_GCC_485_ATOMIC_H_
