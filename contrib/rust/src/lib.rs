/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

//! Rust bindings for [mimalloc](https://github.com/microsoft/mimalloc).
//!
//! Use mimalloc as the global allocator:
//!
//! ```rust
//! use mimalloc::MiMalloc;
//!
//! #[global_allocator]
//! static GLOBAL: MiMalloc = MiMalloc;
//! ```
//!
//! First-class heaps are available as [`Heap`], arenas as [`Arena`], and the raw C API is in [`sys`].
//! Both `MiMalloc` and `&Heap` implement the `Allocator` trait on Rust 1.100+
//! (or with the `nightly` feature on older nightly compilers).
#![no_std]
#![cfg_attr(feature = "nightly", feature(allocator_api), allow(stable_features))]

#[cfg(any(feature = "nightly", allocator_api_stable))]
mod allocator;
mod arena;
mod heap;
pub use mimalloc_sys as sys;

pub use arena::{Arena, ExternalMemory, ReserveOptions};
pub use heap::Heap;

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::{CStr, c_char, c_long};
use core::ptr::{self, NonNull};

// As `MI_MAX_ALIGN_SIZE`
const MAX_ALIGN_SIZE: usize = 16;

// Fast check if `mi_malloc(size)` is already `align`-aligned (a subset of `mi_malloc_is_naturally_aligned`)
#[inline(always)]
pub(crate) const fn is_malloc_aligned(size: usize, align: usize) -> bool {
    align <= MAX_ALIGN_SIZE && align <= size
}

/// The mimalloc allocator; use it as the `#[global_allocator]`.
#[derive(Clone, Copy, Debug, Default)]
pub struct MiMalloc;

// As `mi_free_csize_aligned_nonnull`: blocks of at most `MI_SMALL_SIZE_MAX` are always in small pages
#[inline(always)]
pub(crate) unsafe fn free_sized(ptr: *mut u8, layout: Layout) {
    unsafe {
        if layout.align() <= layout.size() && layout.size() <= sys::MI_SMALL_SIZE_MAX {
            sys::mi_free_small_nonnull(ptr.cast())
        } else {
            sys::mi_free(ptr.cast())
        }
    }
}

#[inline(always)]
pub(crate) unsafe fn realloc_aligned(ptr: *mut u8, align: usize, new_size: usize) -> *mut u8 {
    unsafe {
        if is_malloc_aligned(new_size, align) {
            sys::mi_realloc(ptr.cast(), new_size)
        } else {
            sys::mi_realloc_aligned(ptr.cast(), new_size, align)
        }
    }
    .cast()
}

unsafe impl GlobalAlloc for MiMalloc {
    #[inline]
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let size = layout.size();
        if !is_malloc_aligned(size, layout.align()) {
            sys::mi_malloc_aligned(size, layout.align()).cast()
        } else if size <= sys::MI_SMALL_SIZE_MAX {
            unsafe { sys::mi_wmalloc_small(size.div_ceil(size_of::<usize>())) }.cast()
        } else {
            sys::mi_malloc(size).cast()
        }
    }

    #[inline]
    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let size = layout.size();
        if !is_malloc_aligned(size, layout.align()) {
            sys::mi_zalloc_aligned(size, layout.align()).cast()
        } else if size <= sys::MI_SMALL_SIZE_MAX {
            unsafe { sys::mi_wzalloc_small(size.div_ceil(size_of::<usize>())) }.cast()
        } else {
            sys::mi_zalloc(size).cast()
        }
    }

    #[inline]
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { free_sized(ptr, layout) }
    }

    #[inline]
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        unsafe { realloc_aligned(ptr, layout.align(), new_size) }
    }
}

impl MiMalloc {
    /// The mimalloc version, e.g. `30600` for v3.6.0.
    pub fn version(&self) -> u32 {
        version()
    }

    /// The usable size of an allocation (at least the requested size).
    ///
    /// # Safety
    /// `ptr` must be null or point into a live mimalloc allocation.
    pub unsafe fn usable_size(&self, ptr: *const u8) -> usize {
        unsafe { usable_size(ptr) }
    }
}

/// The mimalloc version, e.g. `30600` for v3.6.0.
pub fn version() -> u32 {
    sys::mi_version() as u32
}

/// The usable size of an allocation (at least the requested size).
///
/// # Safety
/// `ptr` must be null or point into a live mimalloc allocation.
pub unsafe fn usable_size(ptr: *const u8) -> usize {
    unsafe { sys::mi_usable_size(ptr.cast()) }
}

/// The size that would be allocated for a request of `size` bytes.
pub fn good_size(size: usize) -> usize {
    sys::mi_good_size(size)
}

/// Return unused memory to the OS; with `force` all thread-local caches are freed as well.
pub fn collect(force: bool) {
    sys::mi_collect(force)
}

/// Print the process statistics (by default to `stderr`).
pub fn stats_print() {
    unsafe { sys::mi_stats_print_out(None, ptr::null_mut()) }
}

/// Reset the process statistics.
pub fn stats_reset() {
    unsafe { sys::mi_stats_reset() }
}

/// The process statistics in JSON format.
pub fn stats_json() -> Option<MiString> {
    unsafe { MiString::from_raw(sys::mi_stats_get_json(0, ptr::null_mut())) }
}

/// The value of an option (see `sys::mi_option_*`).
pub fn option_get(option: sys::mi_option_t) -> c_long {
    sys::mi_option_get(option)
}

/// Set the value of an option (see `sys::mi_option_*`).
pub fn option_set(option: sys::mi_option_t, value: c_long) {
    sys::mi_option_set(option, value)
}

/// Is an option enabled (i.e. non-zero)?
pub fn option_is_enabled(option: sys::mi_option_t) -> bool {
    sys::mi_option_is_enabled(option)
}

/// Enable or disable an option.
pub fn option_set_enabled(option: sys::mi_option_t, enable: bool) {
    sys::mi_option_set_enabled(option, enable)
}

/// A string allocated by mimalloc, freed on drop.
pub struct MiString(NonNull<c_char>);

unsafe impl Send for MiString {}
unsafe impl Sync for MiString {}

impl MiString {
    /// # Safety
    /// `ptr` must be null or a zero-terminated string allocated by mimalloc.
    pub unsafe fn from_raw(ptr: *mut c_char) -> Option<Self> {
        NonNull::new(ptr).map(MiString)
    }

    pub fn as_c_str(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.0.as_ptr()) }
    }

    /// The string, or `""` if it is not valid UTF-8.
    pub fn as_str(&self) -> &str {
        self.as_c_str().to_str().unwrap_or_default()
    }
}

impl Drop for MiString {
    fn drop(&mut self) {
        unsafe { sys::mi_free(self.0.as_ptr().cast()) }
    }
}

impl core::ops::Deref for MiString {
    type Target = str;
    fn deref(&self) -> &str {
        self.as_str()
    }
}

impl core::fmt::Display for MiString {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(self.as_str())
    }
}

impl core::fmt::Debug for MiString {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        core::fmt::Debug::fmt(self.as_c_str(), f)
    }
}
