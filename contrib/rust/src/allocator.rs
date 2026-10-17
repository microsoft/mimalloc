/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use crate::heap::realloc_in;
use crate::{Heap, MiMalloc, free_sized, realloc_aligned};
use core::alloc::{AllocError, Allocator, GlobalAlloc, Layout};
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
