/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use crate::heap::realloc_in;
use crate::{free_sized, realloc_aligned, sys, Heap, MiMalloc, MAX_ALIGN_SIZE};
use core::alloc::{AllocError, Allocator, GlobalAlloc, Layout};
use core::mem::size_of;
use core::ptr::NonNull;

#[inline(always)]
fn block(ptr: *mut u8, size: usize) -> Result<NonNull<[u8]>, AllocError> {
    NonNull::new(ptr)
        .map(|p| NonNull::slice_from_raw_parts(p, size))
        .ok_or(AllocError)
}

#[inline(always)]
unsafe fn zero_tail(ptr: *mut u8, old_size: usize, new_size: usize) -> *mut u8 {
    if !ptr.is_null() {
        unsafe { ptr.add(old_size).write_bytes(0, new_size - old_size) };
    }
    ptr
}

// Reports the requested size to stay as fast as `GlobalAlloc`; use `good_size` upfront,
// or `MiMallocExcess`, to get the full usable size (issue #1513).
unsafe impl Allocator for MiMalloc {
    #[inline]
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        block(unsafe { self.alloc(layout) }, layout.size())
    }

    #[inline]
    fn allocate_zeroed(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        block(unsafe { self.alloc_zeroed(layout) }, layout.size())
    }

    #[inline]
    unsafe fn deallocate(&self, ptr: NonNull<u8>, layout: Layout) {
        unsafe { free_sized(ptr.as_ptr(), layout) }
    }

    #[inline]
    unsafe fn grow(
        &self,
        ptr: NonNull<u8>,
        _old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        block(
            unsafe { realloc_aligned(ptr.as_ptr(), new.align(), new.size()) },
            new.size(),
        )
    }

    #[inline]
    unsafe fn grow_zeroed(
        &self,
        ptr: NonNull<u8>,
        old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let p = unsafe { realloc_aligned(ptr.as_ptr(), new.align(), new.size()) };
        block(unsafe { zero_tail(p, old.size(), new.size()) }, new.size())
    }

    #[inline]
    unsafe fn shrink(
        &self,
        ptr: NonNull<u8>,
        _old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        block(
            unsafe { realloc_aligned(ptr.as_ptr(), new.align(), new.size()) },
            new.size(),
        )
    }
}

unsafe impl Allocator for &Heap {
    #[inline]
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        block(self.alloc(layout), layout.size())
    }

    #[inline]
    fn allocate_zeroed(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        block(self.alloc_zeroed(layout), layout.size())
    }

    #[inline]
    unsafe fn deallocate(&self, ptr: NonNull<u8>, layout: Layout) {
        unsafe { free_sized(ptr.as_ptr(), layout) }
    }

    #[inline]
    unsafe fn grow(
        &self,
        ptr: NonNull<u8>,
        _old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        block(
            unsafe { realloc_in(self.as_ptr(), ptr.as_ptr(), new.align(), new.size()) },
            new.size(),
        )
    }

    #[inline]
    unsafe fn grow_zeroed(
        &self,
        ptr: NonNull<u8>,
        old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let p = unsafe { realloc_in(self.as_ptr(), ptr.as_ptr(), new.align(), new.size()) };
        block(unsafe { zero_tail(p, old.size(), new.size()) }, new.size())
    }

    #[inline]
    unsafe fn shrink(
        &self,
        ptr: NonNull<u8>,
        _old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        block(
            unsafe { realloc_in(self.as_ptr(), ptr.as_ptr(), new.align(), new.size()) },
            new.size(),
        )
    }
}

/// As [`MiMalloc`] but `allocate` returns the full usable size by requesting `good_size` bytes.
/// This can be useful for vector or buffer allocations to maximise the usable memory and reduce reallocations.
#[derive(Clone, Copy, Debug, Default)]
pub struct MiMallocExcess;

// As `mi_good_size` for sizes up to `MI_SMALL_SIZE_MAX` (in words, see `mi_bin`); a mismatch only wastes space as we allocate the returned size.
// (If the size classes ever change, we catch this in the tests where we verify `small_good_wsize` against `mi_good_size`.)
#[inline(always)]
pub(crate) const fn small_good_wsize(size: usize) -> usize {
    const ALIGN_WSIZE: usize = MAX_ALIGN_SIZE / size_of::<usize>();
    let wsize = crate::wsize_from_size(size);
    if wsize <= 1 {
        1
    } else if size <= 16 {
        (wsize + 1) & !1
    } else if size <= 64 {
        (wsize + ALIGN_WSIZE - 1) & !(ALIGN_WSIZE - 1)
    } else {
        // 4 size classes per power of two
        let w = wsize - 1;
        let shift = (usize::BITS - 1 - w.leading_zeros()) - 2;
        ((w >> shift) + 1) << shift
    }
}

#[inline(always)]
fn small_alloc(layout: Layout, zero: bool) -> Option<Result<NonNull<[u8]>, AllocError>> {
    if layout.align() > MAX_ALIGN_SIZE || layout.size() > sys::MI_SMALL_SIZE_MAX {
        return None;
    }
    let wsize = small_good_wsize(layout.size());
    let p = unsafe {
        if zero {
            sys::mi_wzalloc_small(wsize)
        } else {
            sys::mi_wmalloc_small(wsize)
        }
    };
    Some(block(p.cast(), wsize * size_of::<usize>()))
}

#[inline(always)]
fn good_layout(layout: Layout) -> Layout {
    let size = crate::good_size(layout.size());
    unsafe { Layout::from_size_align_unchecked(size, layout.align()) }
}

unsafe impl Allocator for MiMallocExcess {
    #[inline]
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        if let Some(r) = small_alloc(layout, false) {
            return r;
        }
        let layout = good_layout(layout);
        block(unsafe { MiMalloc.alloc(layout) }, layout.size())
    }

    #[inline]
    fn allocate_zeroed(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        if let Some(r) = small_alloc(layout, true) {
            return r;
        }
        let layout = good_layout(layout);
        block(unsafe { MiMalloc.alloc_zeroed(layout) }, layout.size())
    }

    #[inline]
    unsafe fn deallocate(&self, ptr: NonNull<u8>, layout: Layout) {
        unsafe { free_sized(ptr.as_ptr(), layout) }
    }

    #[inline]
    unsafe fn grow(
        &self,
        ptr: NonNull<u8>,
        _old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let size = crate::good_size(new.size());
        block(
            unsafe { realloc_aligned(ptr.as_ptr(), new.align(), size) },
            size,
        )
    }

    #[inline]
    unsafe fn grow_zeroed(
        &self,
        ptr: NonNull<u8>,
        old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let size = crate::good_size(new.size());
        let p = unsafe { realloc_aligned(ptr.as_ptr(), new.align(), size) };
        block(unsafe { zero_tail(p, old.size(), size) }, size)
    }

    #[inline]
    unsafe fn shrink(
        &self,
        ptr: NonNull<u8>,
        _old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let size = crate::good_size(new.size());
        block(
            unsafe { realloc_aligned(ptr.as_ptr(), new.align(), size) },
            size,
        )
    }
}
