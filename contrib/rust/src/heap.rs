/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use crate::{Arena, MiString, free_sized, is_malloc_aligned, sys};
use core::alloc::Layout;
use core::ptr::{self, NonNull};

/// A first-class mimalloc heap.
///
/// A heap can allocate from any thread, and its blocks can be freed from any thread
/// (also through [`MiMalloc`](crate::MiMalloc)). Dropping the heap deletes it, and any
/// live blocks are migrated to the main heap.
pub struct Heap {
    raw: NonNull<sys::mi_heap_t>,
}

unsafe impl Send for Heap {}
unsafe impl Sync for Heap {}

impl Heap {
    /// Create a new heap; panics if this fails.
    pub fn new() -> Self {
        Self::try_new().expect("mimalloc: unable to create a heap")
    }

    /// Create a new heap.
    pub fn try_new() -> Option<Self> {
        NonNull::new(sys::mi_heap_new()).map(|raw| Heap { raw })
    }

    /// Create a new heap that only allocates in the given arena; panics if this fails.
    ///
    /// When the arena is full, allocation returns null; the heap never falls back to other memory.
    pub fn new_in_arena(arena: Arena) -> Self {
        Self::try_new_in_arena(arena).expect("mimalloc: unable to create a heap in the arena")
    }

    /// Create a new heap that only allocates in the given arena.
    pub fn try_new_in_arena(arena: Arena) -> Option<Self> {
        NonNull::new(unsafe { sys::mi_heap_new_in_arena(arena.as_raw()) }).map(|raw| Heap { raw })
    }

    pub fn as_ptr(&self) -> *mut sys::mi_heap_t {
        self.raw.as_ptr()
    }

    /// Allocate a block for `layout`; returns null when out of memory.
    #[inline]
    pub fn alloc(&self, layout: Layout) -> *mut u8 {
        let (heap, size) = (self.as_ptr(), layout.size());
        unsafe {
            if !is_malloc_aligned(size, layout.align()) {
                sys::mi_heap_malloc_aligned(heap, size, layout.align())
            } else if size <= sys::MI_SMALL_SIZE_MAX {
                sys::mi_heap_malloc_small(heap, size)
            } else {
                sys::mi_heap_malloc(heap, size)
            }
        }
        .cast()
    }

    /// Allocate a zero-initialized block for `layout`; returns null when out of memory.
    #[inline]
    pub fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let (heap, size) = (self.as_ptr(), layout.size());
        unsafe {
            if !is_malloc_aligned(size, layout.align()) {
                sys::mi_heap_zalloc_aligned(heap, size, layout.align())
            } else if size <= sys::MI_SMALL_SIZE_MAX {
                sys::mi_heap_zalloc_small(heap, size)
            } else {
                sys::mi_heap_zalloc(heap, size)
            }
        }
        .cast()
    }

    /// Reallocate a block in this heap; returns null when out of memory (and `ptr` stays valid).
    ///
    /// # Safety
    /// As [`GlobalAlloc::realloc`](core::alloc::GlobalAlloc::realloc) where `ptr` can be from any mimalloc heap.
    #[inline]
    pub unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        unsafe { realloc_in(self.as_ptr(), ptr, layout.align(), new_size) }
    }

    /// Free a block.
    ///
    /// # Safety
    /// As [`GlobalAlloc::dealloc`](core::alloc::GlobalAlloc::dealloc) where `ptr` can be from any mimalloc heap.
    #[inline]
    pub unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { free_sized(ptr, layout) }
    }

    /// Does this heap contain the block `ptr` points into?
    pub fn contains(&self, ptr: *const u8) -> bool {
        unsafe { sys::mi_heap_contains(self.as_ptr(), ptr.cast()) }
    }

    /// Return unused memory of this heap to the OS.
    pub fn collect(&self, force: bool) {
        unsafe { sys::mi_heap_collect(self.as_ptr(), force) }
    }

    /// The heap statistics in JSON format.
    pub fn stats_json(&self) -> Option<MiString> {
        unsafe {
            MiString::from_raw(sys::mi_heap_stats_get_json(
                self.as_ptr(),
                0,
                ptr::null_mut(),
            ))
        }
    }

    /// Destroy the heap and free all its live blocks at once.
    ///
    /// # Safety
    /// No block of this heap may be used afterwards.
    pub unsafe fn destroy(self) {
        let raw = self.as_ptr();
        core::mem::forget(self);
        unsafe { sys::mi_heap_destroy(raw) }
    }
}

#[inline(always)]
pub(crate) unsafe fn realloc_in(
    heap: *mut sys::mi_heap_t,
    ptr: *mut u8,
    align: usize,
    new_size: usize,
) -> *mut u8 {
    unsafe {
        if is_malloc_aligned(new_size, align) {
            sys::mi_heap_realloc(heap, ptr.cast(), new_size)
        } else {
            sys::mi_heap_realloc_aligned(heap, ptr.cast(), new_size, align)
        }
    }
    .cast()
}

impl Default for Heap {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Heap {
    fn drop(&mut self) {
        unsafe { sys::mi_heap_delete(self.as_ptr()) }
    }
}

impl core::fmt::Debug for Heap {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_tuple("Heap").field(&self.raw).finish()
    }
}
