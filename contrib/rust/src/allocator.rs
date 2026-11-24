/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use crate::heap::realloc_in;
use crate::{free_sized, malloc_aligned_size, realloc_aligned, sys, Heap, MiMalloc};
use core::alloc::{AllocError, Allocator, GlobalAlloc, Layout};
use core::ptr::{self, NonNull};

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

/// As [`MiMalloc`] but returns the full usable size of the allocated block (using the `mi_u*` functions).
/// This can be useful for vector or buffer allocations to maximise the usable memory and reduce reallocations.
#[derive(Clone, Copy, Debug, Default)]
pub struct MiMallocExcess;

#[inline(always)]
unsafe fn urealloc(ptr: *mut u8, new: Layout) -> (*mut u8, usize) {
    let mut usable = 0;
    let p = unsafe {
        match malloc_aligned_size(new.size(), new.align()) {
            Some(size) => sys::mi_urealloc(ptr.cast(), size, ptr::null_mut(), &mut usable),
            None => sys::mi_urealloc_aligned(
                ptr.cast(),
                new.size(),
                new.align(),
                ptr::null_mut(),
                &mut usable,
            ),
        }
    };
    (p.cast(), usable)
}

unsafe impl Allocator for MiMallocExcess {
    #[inline]
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        let (size, align) = (layout.size(), layout.align());
        let mut usable = 0;
        let p = unsafe {
            if let Some(size) = malloc_aligned_size(size, align) {
                sys::mi_umalloc(size, &mut usable)
            } else {
                sys::mi_umalloc_aligned(size, align, &mut usable)
            }
        };
        block(p.cast(), usable)
    }

    #[inline]
    fn allocate_zeroed(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        let (size, align) = (layout.size(), layout.align());
        let mut usable = 0;
        let p = unsafe {
            if let Some(size) = malloc_aligned_size(size, align) {
                sys::mi_uzalloc(size, &mut usable)
            } else {
                sys::mi_uzalloc_aligned(size, align, &mut usable)
            }
        };
        block(p.cast(), usable)
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
        let (p, usable) = unsafe { urealloc(ptr.as_ptr(), new) };
        block(p, usable)
    }

    #[inline]
    unsafe fn grow_zeroed(
        &self,
        ptr: NonNull<u8>,
        old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let (p, usable) = unsafe { urealloc(ptr.as_ptr(), new) };
        block(unsafe { zero_tail(p, old.size(), usable) }, usable)
    }

    #[inline]
    unsafe fn shrink(
        &self,
        ptr: NonNull<u8>,
        _old: Layout,
        new: Layout,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let (p, usable) = unsafe { urealloc(ptr.as_ptr(), new) };
        block(p, usable)
    }
}
