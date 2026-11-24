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
#![warn(unsafe_op_in_unsafe_fn)]

#[cfg(any(feature = "nightly", allocator_api_stable))]
#[allow(clippy::incompatible_msrv)]
mod allocator;
mod arena;
#[cfg(any(feature = "nightly", allocator_api_stable))]
pub use allocator::MiMallocExcess;
mod heap;
pub use mimalloc_sys as sys;

pub use arena::{Arena, ExternalMemory, ReserveOptions};
pub use heap::Heap;

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::{c_char, c_long, CStr};
use core::mem::size_of;
use core::ptr::{self, NonNull};

// As `MI_MAX_ALIGN_SIZE`
pub(crate) const MAX_ALIGN_SIZE: usize = 16;

// The size for which `mi_malloc` returns an `align`-aligned block, or `None` if `mi_malloc_aligned` is needed
#[inline(always)]
pub(crate) const fn malloc_aligned_size(size: usize, align: usize) -> Option<usize> {
    if align > MAX_ALIGN_SIZE {
        None
    } else if size < align {
        Some(align)
    } else {
        Some(size)
    }
}

// As `_mi_wsize_from_size`
#[inline(always)]
pub(crate) const fn wsize_from_size(size: usize) -> usize {
    (size + size_of::<usize>() - 1) / size_of::<usize>()
}

// As `NonNull::slice_from_raw_parts` (stable since Rust 1.70)
#[inline(always)]
pub(crate) fn nonnull_slice(ptr: NonNull<u8>, len: usize) -> NonNull<[u8]> {
    unsafe { NonNull::new_unchecked(ptr::slice_from_raw_parts_mut(ptr.as_ptr(), len)) }
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
        match malloc_aligned_size(new_size, align) {
            Some(size) => sys::mi_realloc(ptr.cast(), size),
            None => sys::mi_realloc_aligned(ptr.cast(), new_size, align),
        }
    }
    .cast()
}

unsafe impl GlobalAlloc for MiMalloc {
    #[inline]
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe {
            match malloc_aligned_size(layout.size(), layout.align()) {
                Some(size) if size <= sys::MI_SMALL_SIZE_MAX => {
                    sys::mi_wmalloc_small(wsize_from_size(size))
                }
                Some(size) => sys::mi_malloc(size),
                None => sys::mi_malloc_aligned(layout.size(), layout.align()),
            }
        }
        .cast()
    }

    #[inline]
    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        unsafe {
            match malloc_aligned_size(layout.size(), layout.align()) {
                Some(size) if size <= sys::MI_SMALL_SIZE_MAX => {
                    sys::mi_wzalloc_small(wsize_from_size(size))
                }
                Some(size) => sys::mi_zalloc(size),
                None => sys::mi_zalloc_aligned(layout.size(), layout.align()),
            }
        }
        .cast()
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
    unsafe { sys::mi_version() as u32 }
}

/// The usable size of an allocation (at least the requested size).
///
/// # Safety
/// `ptr` must be null or point into a live mimalloc allocation.
pub unsafe fn usable_size(ptr: *const u8) -> usize {
    unsafe { sys::mi_usable_size(ptr.cast()) }
}

/// The size that would be allocated for a request of `size` bytes.
///
/// Request this size upfront to use the full capacity of a block, e.g. for vectors:
/// `Allocator::allocate` only reports the requested size.
pub fn good_size(size: usize) -> usize {
    unsafe { sys::mi_good_size(size) }
}

/// Return unused memory to the OS; with `force` all thread-local caches are freed as well.
pub fn collect(force: bool) {
    unsafe { sys::mi_collect(force) }
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
    unsafe { sys::mi_option_get(option) }
}

/// Set the value of an option (see `sys::mi_option_*`).
pub fn option_set(option: sys::mi_option_t, value: c_long) {
    unsafe { sys::mi_option_set(option, value) }
}

/// Is an option enabled (i.e. non-zero)?
pub fn option_is_enabled(option: sys::mi_option_t) -> bool {
    unsafe { sys::mi_option_is_enabled(option) }
}

/// Enable or disable an option.
pub fn option_set_enabled(option: sys::mi_option_t, enable: bool) {
    unsafe { sys::mi_option_set_enabled(option, enable) }
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
