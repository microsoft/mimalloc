/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

//! Raw bindings to the mimalloc C API (`mimalloc.h` and `mimalloc-stats.h`).
//!
//! See the C header for the documentation of each function. This crate builds and
//! links the mimalloc C library; see the `mimalloc` crate for the global allocator.
#![no_std]
#![allow(non_camel_case_types, non_upper_case_globals)]

use core::ffi::{c_char, c_int, c_long, c_ulonglong, c_void};

pub const MI_MALLOC_VERSION: c_int = 30600;
pub const MI_SMALL_WSIZE_MAX: usize = 128;
pub const MI_SMALL_SIZE_MAX: usize = MI_SMALL_WSIZE_MAX * core::mem::size_of::<usize>();

/// A first-class heap; can be used from any thread.
#[repr(C)]
pub struct mi_heap_t {
    _private: [u8; 0],
}

/// A thread-local heap.
#[repr(C)]
pub struct mi_theap_t {
    _private: [u8; 0],
}

pub type mi_arena_id_t = *mut c_void;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct mi_subproc_id_t {
    pub _mi_subproc_id: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct mi_heap_area_t {
    pub blocks: *mut c_void,
    pub reserved: usize,
    pub committed: usize,
    pub used: usize,
    pub block_size: usize,
    pub full_block_size: usize,
    pub reserved1: *mut c_void,
}

pub type mi_deferred_free_fun =
    unsafe extern "C" fn(force: bool, heartbeat: c_ulonglong, arg: *mut c_void);
pub type mi_output_fun = unsafe extern "C" fn(msg: *const c_char, arg: *mut c_void);
pub type mi_error_fun = unsafe extern "C" fn(err: c_int, arg: *mut c_void);
pub type mi_block_visit_fun = unsafe extern "C" fn(
    heap: *const mi_heap_t,
    area: *const mi_heap_area_t,
    block: *mut c_void,
    block_size: usize,
    arg: *mut c_void,
) -> bool;
pub type mi_heap_visit_fun = unsafe extern "C" fn(heap: *mut mi_heap_t, arg: *mut c_void) -> bool;
pub type mi_commit_fun_t = unsafe extern "C" fn(
    commit: bool,
    start: *mut c_void,
    size: usize,
    is_zero: *mut bool,
    user_arg: *mut c_void,
) -> bool;

pub type mi_option_t = c_int;
pub const mi_option_show_errors: mi_option_t = 0;
pub const mi_option_show_stats: mi_option_t = 1;
pub const mi_option_verbose: mi_option_t = 2;
pub const mi_option_arena_eager_commit: mi_option_t = 4;
pub const mi_option_purge_decommits: mi_option_t = 5;
pub const mi_option_allow_large_os_pages: mi_option_t = 6;
pub const mi_option_reserve_huge_os_pages: mi_option_t = 7;
pub const mi_option_reserve_huge_os_pages_at: mi_option_t = 8;
pub const mi_option_reserve_os_memory: mi_option_t = 9;
pub const mi_option_purge_delay: mi_option_t = 15;
pub const mi_option_use_numa_nodes: mi_option_t = 16;
pub const mi_option_disallow_os_alloc: mi_option_t = 17;
pub const mi_option_os_tag: mi_option_t = 18;
pub const mi_option_max_errors: mi_option_t = 19;
pub const mi_option_max_warnings: mi_option_t = 20;
pub const mi_option_destroy_on_exit: mi_option_t = 22;
pub const mi_option_arena_reserve: mi_option_t = 23;
pub const mi_option_arena_purge_mult: mi_option_t = 24;
pub const mi_option_disallow_arena_alloc: mi_option_t = 26;
pub const mi_option_retry_on_oom: mi_option_t = 27;
pub const mi_option_guarded_min: mi_option_t = 29;
pub const mi_option_guarded_max: mi_option_t = 30;
pub const mi_option_unsafe_guarded_precise: mi_option_t = 31;
pub const mi_option_guarded_sample_rate: mi_option_t = 32;
pub const mi_option_guarded_sample_seed: mi_option_t = 33;
pub const mi_option_generic_collect: mi_option_t = 34;
pub const mi_option_page_reclaim_on_free: mi_option_t = 35;
pub const mi_option_page_full_retain: mi_option_t = 36;
pub const mi_option_page_max_candidates: mi_option_t = 37;
pub const mi_option_max_vabits: mi_option_t = 38;
pub const mi_option_pagemap_commit: mi_option_t = 39;
pub const mi_option_page_commit_on_demand: mi_option_t = 40;
pub const mi_option_page_max_reclaim: mi_option_t = 41;
pub const mi_option_page_cross_thread_max_reclaim: mi_option_t = 42;
pub const mi_option_allow_thp: mi_option_t = 43;
pub const mi_option_arena_purge_min_size: mi_option_t = 44;
pub const mi_option_arena_max_object_size: mi_option_t = 45;
pub const mi_option_arena_is_numa_local: mi_option_t = 46;
pub const mi_option_stats_merge_on_collect: mi_option_t = 47;
pub const mi_option_profile_alloc_interval: mi_option_t = 48;
pub const mi_option_profile_inuse_interval: mi_option_t = 49;
pub const mi_option_profile_time_interval: mi_option_t = 50;
pub const mi_option_profile_sample_rate: mi_option_t = 51;
pub const mi_option_stats_merge_threshold: mi_option_t = 52;
pub const mi_option_arena_purge_immediate_size: mi_option_t = 53;
pub const _mi_option_last: mi_option_t = 54;

unsafe extern "C" {
    // standard malloc interface
    pub safe fn mi_malloc(size: usize) -> *mut c_void;
    pub safe fn mi_calloc(count: usize, size: usize) -> *mut c_void;
    pub fn mi_realloc(p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_expand(p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_free(p: *mut c_void);
    pub fn mi_strdup(s: *const c_char) -> *mut c_char;
    pub fn mi_strndup(s: *const c_char, n: usize) -> *mut c_char;
    pub fn mi_realpath(fname: *const c_char, resolved_name: *mut c_char) -> *mut c_char;

    // extended allocation
    pub fn mi_malloc_small(size: usize) -> *mut c_void;
    pub fn mi_zalloc_small(size: usize) -> *mut c_void;
    pub safe fn mi_zalloc(size: usize) -> *mut c_void;
    pub safe fn mi_mallocn(count: usize, size: usize) -> *mut c_void;
    pub fn mi_reallocn(p: *mut c_void, count: usize, size: usize) -> *mut c_void;
    pub fn mi_reallocf(p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_usable_size(p: *const c_void) -> usize;
    pub safe fn mi_good_size(size: usize) -> usize;
    pub fn mi_free_size(p: *mut c_void, size: usize);
    pub fn mi_free_small(p: *mut c_void);
    pub fn mi_free_small_nonnull(p: *mut c_void);
    pub fn mi_free_small_local(p: *mut c_void);
    pub fn mi_free_small_local_nonnull(p: *mut c_void);
    pub fn mi_thread_id_of(p: *const c_void) -> usize;

    // allocation in machine words (`wsize <= MI_SMALL_WSIZE_MAX`); not exported from a dll
    pub fn mi_wmalloc_small(wsize: usize) -> *mut c_void;
    pub fn mi_wzalloc_small(wsize: usize) -> *mut c_void;

    // aligned allocation
    pub safe fn mi_malloc_aligned(size: usize, alignment: usize) -> *mut c_void;
    pub safe fn mi_malloc_aligned_at(size: usize, alignment: usize, offset: usize) -> *mut c_void;
    pub safe fn mi_zalloc_aligned(size: usize, alignment: usize) -> *mut c_void;
    pub safe fn mi_zalloc_aligned_at(size: usize, alignment: usize, offset: usize) -> *mut c_void;
    pub safe fn mi_calloc_aligned(count: usize, size: usize, alignment: usize) -> *mut c_void;
    pub safe fn mi_calloc_aligned_at(
        count: usize,
        size: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_realloc_aligned(p: *mut c_void, newsize: usize, alignment: usize) -> *mut c_void;
    pub fn mi_realloc_aligned_at(
        p: *mut c_void,
        newsize: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;

    // internals
    pub fn mi_register_deferred_free(deferred_free: Option<mi_deferred_free_fun>, arg: *mut c_void);
    pub fn mi_register_output(out: Option<mi_output_fun>, arg: *mut c_void);
    pub fn mi_register_error(fun: Option<mi_error_fun>, arg: *mut c_void);
    pub safe fn mi_collect(force: bool);
    pub safe fn mi_version() -> c_int;
    pub safe fn mi_options_print();
    pub safe fn mi_process_info_print();
    pub fn mi_options_print_out(out: Option<mi_output_fun>, arg: *mut c_void);
    pub fn mi_process_info_print_out(out: Option<mi_output_fun>, arg: *mut c_void);
    pub fn mi_process_info(
        elapsed_msecs: *mut usize,
        user_msecs: *mut usize,
        system_msecs: *mut usize,
        current_rss: *mut usize,
        peak_rss: *mut usize,
        current_commit: *mut usize,
        peak_commit: *mut usize,
        page_faults: *mut usize,
    );
    pub fn mi_process_init();
    pub fn mi_process_done();
    pub fn mi_thread_init();
    pub fn mi_thread_done();
    pub fn mi_thread_set_in_threadpool();

    // allocation that also returns the allocated block size
    pub fn mi_umalloc(size: usize, block_size: *mut usize) -> *mut c_void;
    pub fn mi_ucalloc(count: usize, size: usize, block_size: *mut usize) -> *mut c_void;
    pub fn mi_urealloc(
        p: *mut c_void,
        newsize: usize,
        block_size_pre: *mut usize,
        block_size_post: *mut usize,
    ) -> *mut c_void;
    pub fn mi_ufree(p: *mut c_void, block_size: *mut usize);
    pub fn mi_umalloc_aligned(size: usize, alignment: usize, block_size: *mut usize)
    -> *mut c_void;
    pub fn mi_uzalloc_aligned(size: usize, alignment: usize, block_size: *mut usize)
    -> *mut c_void;
    pub fn mi_umalloc_small(size: usize, block_size: *mut usize) -> *mut c_void;
    pub fn mi_uzalloc_small(size: usize, block_size: *mut usize) -> *mut c_void;

    // first-class heaps
    pub safe fn mi_heap_new() -> *mut mi_heap_t;
    pub fn mi_heap_delete(heap: *mut mi_heap_t);
    pub fn mi_heap_destroy(heap: *mut mi_heap_t);
    pub fn mi_heap_set_numa_affinity(heap: *mut mi_heap_t, numa_node: c_int);
    pub fn mi_heap_set_debug_fill_preserve(heap: *mut mi_heap_t, preserve: usize);
    pub fn mi_heap_collect(heap: *mut mi_heap_t, force: bool);
    pub safe fn mi_heap_main() -> *mut mi_heap_t;
    pub safe fn mi_heap_of(p: *const c_void) -> *mut mi_heap_t;
    pub fn mi_heap_contains(heap: *const mi_heap_t, p: *const c_void) -> bool;
    pub safe fn mi_any_heap_contains(p: *const c_void) -> bool;

    pub fn mi_heap_malloc(heap: *mut mi_heap_t, size: usize) -> *mut c_void;
    pub fn mi_heap_zalloc(heap: *mut mi_heap_t, size: usize) -> *mut c_void;
    pub fn mi_heap_calloc(heap: *mut mi_heap_t, count: usize, size: usize) -> *mut c_void;
    pub fn mi_heap_mallocn(heap: *mut mi_heap_t, count: usize, size: usize) -> *mut c_void;
    pub fn mi_heap_malloc_small(heap: *mut mi_heap_t, size: usize) -> *mut c_void;
    pub fn mi_heap_zalloc_small(heap: *mut mi_heap_t, size: usize) -> *mut c_void;
    pub fn mi_heap_realloc(heap: *mut mi_heap_t, p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_heap_reallocn(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        count: usize,
        size: usize,
    ) -> *mut c_void;
    pub fn mi_heap_reallocf(heap: *mut mi_heap_t, p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_heap_strdup(heap: *mut mi_heap_t, s: *const c_char) -> *mut c_char;
    pub fn mi_heap_strndup(heap: *mut mi_heap_t, s: *const c_char, n: usize) -> *mut c_char;
    pub fn mi_heap_realpath(
        heap: *mut mi_heap_t,
        fname: *const c_char,
        resolved_name: *mut c_char,
    ) -> *mut c_char;

    pub fn mi_heap_malloc_aligned(
        heap: *mut mi_heap_t,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_heap_malloc_aligned_at(
        heap: *mut mi_heap_t,
        size: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_heap_zalloc_aligned(
        heap: *mut mi_heap_t,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_heap_zalloc_aligned_at(
        heap: *mut mi_heap_t,
        size: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_heap_calloc_aligned(
        heap: *mut mi_heap_t,
        count: usize,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_heap_calloc_aligned_at(
        heap: *mut mi_heap_t,
        count: usize,
        size: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_heap_realloc_aligned(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        newsize: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_heap_realloc_aligned_at(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        newsize: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;

    // zero initialized re-allocation (only valid on memory that was zero initialized)
    pub fn mi_rezalloc(p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_recalloc(p: *mut c_void, newcount: usize, size: usize) -> *mut c_void;
    pub fn mi_rezalloc_aligned(p: *mut c_void, newsize: usize, alignment: usize) -> *mut c_void;
    pub fn mi_rezalloc_aligned_at(
        p: *mut c_void,
        newsize: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_recalloc_aligned(
        p: *mut c_void,
        newcount: usize,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_recalloc_aligned_at(
        p: *mut c_void,
        newcount: usize,
        size: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_heap_rezalloc(heap: *mut mi_heap_t, p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_heap_recalloc(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        newcount: usize,
        size: usize,
    ) -> *mut c_void;
    pub fn mi_heap_rezalloc_aligned(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        newsize: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_heap_rezalloc_aligned_at(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        newsize: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_heap_recalloc_aligned(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        newcount: usize,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_heap_recalloc_aligned_at(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        newcount: usize,
        size: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;

    // visiting heap areas and blocks
    pub fn mi_heap_visit_blocks(
        heap: *mut mi_heap_t,
        visit_blocks: bool,
        visitor: Option<mi_block_visit_fun>,
        arg: *mut c_void,
    ) -> bool;
    pub fn mi_heap_visit_abandoned_blocks(
        heap: *mut mi_heap_t,
        visit_blocks: bool,
        visitor: Option<mi_block_visit_fun>,
        arg: *mut c_void,
    ) -> bool;

    // arenas
    pub safe fn mi_is_redirected() -> bool;
    pub fn mi_reserve_huge_os_pages_interleave(
        pages: usize,
        numa_nodes: usize,
        timeout_msecs: usize,
    ) -> c_int;
    pub fn mi_reserve_huge_os_pages_at(
        pages: usize,
        numa_node: c_int,
        timeout_msecs: usize,
    ) -> c_int;
    pub fn mi_reserve_os_memory(size: usize, commit: bool, allow_large: bool) -> c_int;
    pub fn mi_manage_os_memory(
        start: *mut c_void,
        size: usize,
        is_committed: bool,
        is_pinned: bool,
        is_zero: bool,
        numa_node: c_int,
    ) -> bool;
    pub safe fn mi_debug_show_arenas();
    pub safe fn mi_arenas_print();
    pub safe fn mi_arenas_purge();
    pub safe fn mi_arena_min_alignment() -> usize;
    pub safe fn mi_arena_min_size() -> usize;
    pub safe fn mi_arena_max_object_size() -> usize;
    pub fn mi_arena_area(arena_id: mi_arena_id_t, size: *mut usize) -> *mut c_void;
    pub fn mi_reserve_huge_os_pages_at_ex(
        pages: usize,
        numa_node: c_int,
        timeout_msecs: usize,
        exclusive: bool,
        arena_id: *mut mi_arena_id_t,
    ) -> c_int;
    pub fn mi_reserve_os_memory_ex(
        size: usize,
        commit: bool,
        allow_large: bool,
        exclusive: bool,
        arena_id: *mut mi_arena_id_t,
    ) -> c_int;
    pub fn mi_manage_os_memory_ex(
        start: *mut c_void,
        size: usize,
        is_committed: bool,
        is_pinned: bool,
        is_zero: bool,
        numa_node: c_int,
        exclusive: bool,
        arena_id: *mut mi_arena_id_t,
    ) -> bool;
    pub fn mi_arena_contains(arena_id: mi_arena_id_t, p: *const c_void) -> bool;
    pub fn mi_heap_new_in_arena(arena_id: mi_arena_id_t) -> *mut mi_heap_t;

    // sub-processes
    pub safe fn mi_subproc_current() -> mi_subproc_id_t;
    pub safe fn mi_subproc_new() -> mi_subproc_id_t;
    pub fn mi_subproc_destroy(subproc: mi_subproc_id_t);
    pub fn mi_subproc_add_current_thread(subproc: mi_subproc_id_t);
    pub fn mi_subproc_visit_heaps(
        subproc: mi_subproc_id_t,
        visitor: Option<mi_heap_visit_fun>,
        arg: *mut c_void,
    ) -> bool;

    // thread-local heaps
    pub fn mi_heap_theap(heap: *mut mi_heap_t) -> *mut mi_theap_t;
    pub fn mi_theap_set_default(theap: *mut mi_theap_t) -> *mut mi_theap_t;
    pub safe fn mi_theap_get_default() -> *mut mi_theap_t;
    pub fn mi_theap_collect(theap: *mut mi_theap_t, force: bool);
    pub fn mi_theap_malloc(theap: *mut mi_theap_t, size: usize) -> *mut c_void;
    pub fn mi_theap_zalloc(theap: *mut mi_theap_t, size: usize) -> *mut c_void;
    pub fn mi_theap_calloc(theap: *mut mi_theap_t, count: usize, size: usize) -> *mut c_void;
    pub fn mi_theap_malloc_small(theap: *mut mi_theap_t, size: usize) -> *mut c_void;
    pub fn mi_theap_zalloc_small(theap: *mut mi_theap_t, size: usize) -> *mut c_void;
    pub fn mi_theap_malloc_aligned(
        theap: *mut mi_theap_t,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_theap_zalloc_aligned(
        theap: *mut mi_theap_t,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_theap_realloc(theap: *mut mi_theap_t, p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_theap_rezalloc(theap: *mut mi_theap_t, p: *mut c_void, newsize: usize)
    -> *mut c_void;
    pub fn mi_theap_wmalloc_small(theap: *mut mi_theap_t, wsize: usize) -> *mut c_void;
    pub fn mi_theap_wzalloc_small(theap: *mut mi_theap_t, wsize: usize) -> *mut c_void;
    pub fn mi_theap_delete(theap: *mut mi_theap_t);
    pub fn mi_theap_quiescent_abandon(theap: *mut mi_theap_t);

    // experimental
    pub fn mi_theap_guarded_set_sample_rate(
        theap: *mut mi_theap_t,
        sample_rate: usize,
        seed: usize,
    );
    pub fn mi_theap_guarded_set_size_bound(theap: *mut mi_theap_t, min: usize, max: usize);
    pub fn mi_manage_memory(
        start: *mut c_void,
        size: usize,
        is_committed: bool,
        is_pinned: bool,
        is_zero: bool,
        numa_node: c_int,
        exclusive: bool,
        commit_fun: Option<mi_commit_fun_t>,
        commit_fun_arg: *mut c_void,
        arena_id: *mut mi_arena_id_t,
    ) -> bool;
    pub fn mi_unsafe_heap_page_is_under_utilized(
        heap: *mut mi_heap_t,
        p: *mut c_void,
        perc_threshold: usize,
    ) -> bool;

    // statistics
    pub fn mi_stats_reset();
    pub fn mi_stats_merge();
    pub fn mi_stats_print_out(out: Option<mi_output_fun>, arg: *mut c_void);
    pub fn mi_stats_get_json(buf_size: usize, buf: *mut c_char) -> *mut c_char;
    pub fn mi_heap_stats_get_json(
        heap: *mut mi_heap_t,
        buf_size: usize,
        buf: *mut c_char,
    ) -> *mut c_char;
    pub fn mi_heap_stats_print_out(
        heap: *mut mi_heap_t,
        out: Option<mi_output_fun>,
        arg: *mut c_void,
    );
    pub fn mi_heap_stats_merge_to_subproc(heap: *mut mi_heap_t);
    pub fn mi_subproc_stats_get_json(
        subproc_id: mi_subproc_id_t,
        buf_size: usize,
        buf: *mut c_char,
    ) -> *mut c_char;
    pub fn mi_subproc_stats_print_out(
        subproc_id: mi_subproc_id_t,
        out: Option<mi_output_fun>,
        arg: *mut c_void,
    );
    pub fn mi_subproc_heap_stats_print_out(
        subproc_id: mi_subproc_id_t,
        out: Option<mi_output_fun>,
        arg: *mut c_void,
    );
    pub safe fn mi_stats_get_bin_size(bin: usize) -> usize;
    pub fn mi_theap_stats_merge_to_heap(theap: *mut mi_theap_t);

    // options
    pub safe fn mi_option_is_enabled(option: mi_option_t) -> bool;
    pub safe fn mi_option_enable(option: mi_option_t);
    pub safe fn mi_option_disable(option: mi_option_t);
    pub safe fn mi_option_set_enabled(option: mi_option_t, enable: bool);
    pub safe fn mi_option_set_enabled_default(option: mi_option_t, enable: bool);
    pub safe fn mi_option_get(option: mi_option_t) -> c_long;
    pub safe fn mi_option_get_clamp(option: mi_option_t, min: c_long, max: c_long) -> c_long;
    pub safe fn mi_option_get_size(option: mi_option_t) -> usize;
    pub safe fn mi_option_set(option: mi_option_t, value: c_long);
    pub safe fn mi_option_set_default(option: mi_option_t, value: c_long);

    // posix and other standard interfaces
    pub fn mi_cfree(p: *mut c_void) -> bool;
    pub fn mi__expand(p: *mut c_void, newsize: usize) -> *mut c_void;
    pub fn mi_malloc_size(p: *const c_void) -> usize;
    pub safe fn mi_malloc_good_size(size: usize) -> usize;
    pub fn mi_malloc_usable_size(p: *const c_void) -> usize;
    pub fn mi_posix_memalign(p: *mut *mut c_void, alignment: usize, size: usize) -> c_int;
    pub safe fn mi_memalign(alignment: usize, size: usize) -> *mut c_void;
    pub safe fn mi_valloc(size: usize) -> *mut c_void;
    pub safe fn mi_pvalloc(size: usize) -> *mut c_void;
    pub safe fn mi_aligned_alloc(alignment: usize, size: usize) -> *mut c_void;
    pub fn mi_reallocarray(p: *mut c_void, count: usize, size: usize) -> *mut c_void;
    pub fn mi_reallocarr(p: *mut c_void, count: usize, size: usize) -> c_int;
    pub fn mi_aligned_recalloc(
        p: *mut c_void,
        newcount: usize,
        size: usize,
        alignment: usize,
    ) -> *mut c_void;
    pub fn mi_aligned_offset_recalloc(
        p: *mut c_void,
        newcount: usize,
        size: usize,
        alignment: usize,
        offset: usize,
    ) -> *mut c_void;
    pub fn mi_free_size_aligned(p: *mut c_void, size: usize, alignment: usize);
    pub fn mi_free_aligned(p: *mut c_void, alignment: usize);
}

// keep the malloc overrides when linking statically
#[cfg(feature = "override")]
#[used]
static OVERRIDE: extern "C" fn(usize) -> *mut c_void = mi_malloc;
