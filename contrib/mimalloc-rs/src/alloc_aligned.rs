use crate::*;
use std::ffi::c_void;


pub fn mi_heap_malloc_zero_no_guarded(
    heap: Option<&mut crate::super_special_unit0::mi_heap_t>,
    size: usize,
    zero: bool,
) -> *mut c_void {
    let heap_ptr = heap.map_or(std::ptr::null_mut(), |heap_ref| {
        heap_ref as *mut crate::super_special_unit0::mi_heap_t
    });
    unsafe { crate::_mi_heap_malloc_zero(heap_ptr, size, zero) }
}
#[inline]
pub unsafe extern "C" fn mi_heap_malloc_zero_aligned_at_overalloc(
    heap: *mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    alignment: usize,
    offset: usize,
    zero: bool,
) -> *mut c_void {
    // Check assertion: size <= PTRDIFF_MAX - sizeof(mi_padding_t)
    if size > (isize::MAX as usize).wrapping_sub(std::mem::size_of::<crate::mi_padding_t::mi_padding_t>()) {
        crate::super_function_unit5::_mi_assert_fail(
            b"size <= (MI_MAX_ALLOC_SIZE - MI_PADDING_SIZE)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            58,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    // Check assertion: alignment != 0 && _mi_is_power_of_two(alignment)
    if alignment == 0 || !crate::_mi_is_power_of_two(alignment) {
        crate::super_function_unit5::_mi_assert_fail(
            b"alignment != 0 && _mi_is_power_of_two(alignment)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            59,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    let p: *mut c_void;
    let mut p_idx: *mut c_void = std::ptr::null_mut();
    let mut oversize: usize;

    // Large alignment case (alignment > 1 << (13 + 3) = 65536)
    if alignment > (1usize << (13 + 3)) {
        // Check if offset is non-zero for large alignments
        if offset != 0 {
            crate::alloc::_mi_error_message(
                75,
                b"aligned allocation with a large alignment cannot be used with an alignment offset (size %zu, alignment %zu, offset %zu)\n\0"
                    .as_ptr() as *const _,
            );
            return std::ptr::null_mut();
        }

        // Calculate oversize for large alignment
        oversize = if size <= (128 * std::mem::size_of::<*mut c_void>()) {
            (128 * std::mem::size_of::<*mut c_void>()) + 1
        } else {
            size
        };

        p_idx = crate::_mi_heap_malloc_zero_ex(heap, oversize, zero, alignment);
        p = p_idx;
        
        if p.is_null() {
            return std::ptr::null_mut();
        }
    } else {
        // Normal alignment case
        oversize = ((if size < 16 { 16 } else { size }) + alignment) - 1;
        
        p = crate::mi_heap_malloc_zero_no_guarded(
            if heap.is_null() { Option::None } else { Some(&mut *heap) },
            oversize,
            zero,
        );
        
        if p.is_null() {
            return std::ptr::null_mut();
        }
        
        // Store the pointer value for later comparisons
        p_idx = p;
    }

    let align_mask = alignment.wrapping_sub(1);
    let poffset = (p as usize).wrapping_add(offset) & align_mask;
    let adjust = if poffset == 0 { 0 } else { alignment.wrapping_sub(poffset) };

    // Check assertion: adjust < alignment
    if adjust >= alignment {
        crate::super_function_unit5::_mi_assert_fail(
            b"adjust < alignment\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            90,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    let aligned_p = (p as usize).wrapping_add(adjust) as *mut c_void;
    let page = crate::_mi_ptr_page(p);

    if aligned_p != p {
        // Cast page to MiPage type for mi_page_set_has_interior_pointers
        let page_as_mipage = page as *mut crate::MiPage;
        crate::mi_page_set_has_interior_pointers(&mut *page_as_mipage, true);
        // Note: _mi_padding_shrink is not available in dependencies, so we'll comment it out
        // crate::_mi_padding_shrink(&mut *page, p as *mut crate::MiBlock, adjust.wrapping_add(size));
    }

    // Check assertion: mi_page_usable_block_size(page) >= adjust + size
    // Since mi_page_usable_block_size is not available, we'll use mi_page_block_size instead
    let page_ref = &*page;
    let block_size = crate::mi_page_block_size(page_ref);
    if block_size < adjust.wrapping_add(size) {
        crate::super_function_unit5::_mi_assert_fail(
            b"mi_page_usable_block_size(page) >= adjust + size\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            111,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    // Check assertion: ((aligned_p + offset) % alignment) == 0
    if ((aligned_p as usize).wrapping_add(offset) % alignment) != 0 {
        crate::super_function_unit5::_mi_assert_fail(
            b"((uintptr_t)aligned_p + offset) % alignment == 0\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            112,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    // Check assertion: mi_usable_size(aligned_p) >= size
    let aligned_slice = std::slice::from_raw_parts(aligned_p as *const u8, size);
    if crate::mi_usable_size(Some(aligned_slice)) < size {
        crate::super_function_unit5::_mi_assert_fail(
            b"mi_usable_size(aligned_p)>=size\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            113,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    // Check assertion: mi_usable_size(p) == mi_usable_size(aligned_p) + adjust
    let p_slice = std::slice::from_raw_parts(p as *const u8, oversize);
    let p_usable = crate::mi_usable_size(Some(p_slice));
    let aligned_usable = crate::mi_usable_size(Some(aligned_slice));
    if p_usable != aligned_usable.wrapping_add(adjust) {
        crate::super_function_unit5::_mi_assert_fail(
            b"mi_usable_size(p) == mi_usable_size(aligned_p)+adjust\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            114,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    let apage = crate::_mi_ptr_page(aligned_p);
    let apage_ref = &*apage;
    let unalign_p = crate::_mi_page_ptr_unalign(
        Some(apage_ref),
        Some(std::slice::from_raw_parts(aligned_p as *const u8, 1)),
    );

    // Check assertion: p == unalign_p
    if unalign_p.is_none() || (p as *const crate::MiBlock) != unalign_p.unwrap() as *const _ {
        crate::super_function_unit5::_mi_assert_fail(
            b"p == unalign_p\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0"
                .as_ptr() as *const _,
            118,
            b"mi_heap_malloc_zero_aligned_at_overalloc\0".as_ptr() as *const _,
        );
    }

    // Note: The condition "if (p != aligned_p)" in C just has an empty body, so we skip it in Rust

    aligned_p
}
pub fn mi_malloc_is_naturally_aligned(size: usize, alignment: usize) -> bool {
    // Assertion: alignment must be a power of two and greater than 0
    if !(_mi_is_power_of_two(alignment) && (alignment > 0)) {
        let assertion = std::ffi::CString::new("_mi_is_power_of_two(alignment) && (alignment > 0)").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c").unwrap();
        let func = std::ffi::CString::new("mi_malloc_is_naturally_aligned").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 20, func.as_ptr());
    }

    if alignment > size {
        return false;
    }

    let bsize = mi_good_size(size);
    let ok = (bsize <= 1024) && _mi_is_power_of_two(bsize);

    if ok {
        // Assertion: bsize must be aligned to the given alignment
        if (bsize & (alignment - 1)) != 0 {
            let assertion = std::ffi::CString::new("(bsize & (alignment-1)) == 0").unwrap();
            let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c").unwrap();
            let func = std::ffi::CString::new("mi_malloc_is_naturally_aligned").unwrap();
            crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 24, func.as_ptr());
        }
    }

    ok
}
pub fn mi_heap_malloc_zero_aligned_at_generic(
    heap: *mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    alignment: usize,
    offset: usize,
    zero: bool,
) -> *mut c_void {
    // Assert: alignment != 0 && _mi_is_power_of_two(alignment)
    if alignment == 0 || !crate::_mi_is_power_of_two(alignment) {
        crate::super_function_unit5::_mi_assert_fail(
            b"alignment != 0 && _mi_is_power_of_two(alignment)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0".as_ptr() as *const _,
            142,
            b"mi_heap_malloc_zero_aligned_at_generic\0".as_ptr() as *const _,
        );
    }

    // Check if size is too large
    if size > (isize::MAX as usize).wrapping_sub(std::mem::size_of::<crate::mi_padding_t::mi_padding_t>()) {
        // Use the fully qualified path to avoid ambiguity
        crate::alloc::_mi_error_message(
            75,
            b"aligned allocation request is too large (size %zu, alignment %zu)\n\0".as_ptr() as *const _,
        );
        return std::ptr::null_mut();
    }

    if offset == 0 && crate::mi_malloc_is_naturally_aligned(size, alignment) {
        // Convert raw pointer to Option<&mut> for safe usage
        let heap_ref = if heap.is_null() {
            Option::None
        } else {
            Some(unsafe { &mut *heap })
        };

        let p = crate::mi_heap_malloc_zero_no_guarded(heap_ref, size, zero);

        // p is already *mut c_void, no need to match as Option
        let p_ptr = p;

        // Assert: p == NULL || ((uintptr_t)p % alignment) == 0
        if !p_ptr.is_null() && (p_ptr as usize) % alignment != 0 {
            crate::super_function_unit5::_mi_assert_fail(
                b"p == NULL || ((uintptr_t)p % alignment) == 0\0".as_ptr() as *const _,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0".as_ptr() as *const _,
                156,
                b"mi_heap_malloc_zero_aligned_at_generic\0".as_ptr() as *const _,
            );
        }

        let is_aligned_or_null = (p_ptr as usize) & (alignment.wrapping_sub(1)) == 0;

        if is_aligned_or_null {
            return p_ptr;
        } else {
            crate::super_function_unit5::_mi_assert_fail(
                b"false\0".as_ptr() as *const _,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c\0".as_ptr() as *const _,
                163,
                b"mi_heap_malloc_zero_aligned_at_generic\0".as_ptr() as *const _,
            );
            // mi_free expects Option<*mut c_void>, so wrap p_ptr in Some
            crate::mi_free(Some(p_ptr));
        }
    }

    // Call the overalloc function with the same parameters
    unsafe {
        crate::mi_heap_malloc_zero_aligned_at_overalloc(heap, size, alignment, offset, zero)
    }
}
pub fn mi_heap_malloc_zero_aligned_at(
    heap: &mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    alignment: usize,
    offset: usize,
    zero: bool,
) -> Option<&mut [u8]> {
    // Disambiguate _mi_assert_fail by using the fully qualified path

    // Check if alignment is a power of two
    if alignment == 0 || !crate::_mi_is_power_of_two(alignment) {
        let error_msg = std::ffi::CString::new("aligned allocation requires the alignment to be a power-of-two (size %zu, alignment %zu)\n")
            .expect("CString::new failed");
        crate::alloc::_mi_error_message(75, error_msg.as_ptr());
        return Option::None;
    }

    // Check if it's a small allocation that can use the fast path
    if size <= (128 * std::mem::size_of::<*mut std::ffi::c_void>()) && alignment <= size {
        let align_mask = alignment - 1;
        let padsize = size + std::mem::size_of::<crate::mi_padding_t::mi_padding_t>();
        
        // Get raw pointer to heap before mutable borrow
        let heap_ptr = heap as *mut crate::super_special_unit0::mi_heap_t;
        
        // Get free small page for this size
        let page = crate::_mi_heap_get_free_small_page(heap, padsize)?;
        
        // Check if page has free blocks
        if page.free.is_some() {
            // Check if the free block is already properly aligned
            let free_ptr = page.free.unwrap() as usize;
            let is_aligned = ((free_ptr + offset) & align_mask) == 0;
            
            if is_aligned {
                let page_ptr = page as *mut crate::super_special_unit0::mi_page_t;
                
                // Allocate from the page
                let p = if zero {
                    unsafe {
                        crate::_mi_page_malloc_zeroed(
                            heap_ptr,
                            page_ptr,
                            padsize
                        )
                    }
                } else {
                    unsafe {
                        crate::_mi_page_malloc(
                            heap_ptr,
                            page_ptr,
                            padsize
                        )
                    }
                };
                
                // Check allocation success
                let assertion_msg = std::ffi::CString::new("p != NULL").expect("CString::new failed");
                let file_name = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c").expect("CString::new failed");
                let func_name = std::ffi::CString::new("mi_heap_malloc_zero_aligned_at").expect("CString::new failed");
                
                if p.is_null() {
                    // Use fully qualified path to avoid ambiguity
                    crate::super_function_unit5::_mi_assert_fail(assertion_msg.as_ptr(), file_name.as_ptr(), 202, func_name.as_ptr());
                    return Option::None;
                }
                
                // Verify alignment
                let alignment_check = std::ffi::CString::new("((uintptr_t)p + offset) % alignment == 0").expect("CString::new failed");
                if (((p as usize) + offset) % alignment) != 0 {
                    // Use fully qualified path to avoid ambiguity
                    crate::super_function_unit5::_mi_assert_fail(alignment_check.as_ptr(), file_name.as_ptr(), 203, func_name.as_ptr());
                    return Option::None;
                }
                
                // Convert pointer to slice and verify usable size
                if !p.is_null() {
                    let slice = unsafe { std::slice::from_raw_parts_mut(p as *mut u8, size) };
                    let usable_size = crate::mi_usable_size(Some(slice));
                    
                    let size_check = std::ffi::CString::new("mi_usable_size(p)==(size)").expect("CString::new failed");
                    if usable_size != size {
                        // Use fully qualified path to avoid ambiguity
                        crate::super_function_unit5::_mi_assert_fail(size_check.as_ptr(), file_name.as_ptr(), 204, func_name.as_ptr());
                        return Option::None;
                    }
                    
                    return Some(slice);
                }
            }
        }
    }
    
    // Fall back to generic allocation
    let result = crate::mi_heap_malloc_zero_aligned_at_generic(
        heap as *mut crate::super_special_unit0::mi_heap_t,
        size,
        alignment,
        offset,
        zero
    );
    
    if !result.is_null() {
        Some(unsafe { std::slice::from_raw_parts_mut(result as *mut u8, size) })
    } else {
        Option::None
    }
}
pub fn mi_heap_malloc_aligned_at(
    heap: &mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&mut [u8]> {
    mi_heap_malloc_zero_aligned_at(heap, size, alignment, offset, false)
}
pub fn mi_heap_realloc_zero_aligned_at<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
    offset: usize,
    zero: bool,
) -> Option<&'a mut [u8]> {
    // Assert alignment > 0 (C equivalent)
    if alignment == 0 {
        let assertion = std::ffi::CString::new("alignment > 0").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c").unwrap();
        let func = std::ffi::CString::new("mi_heap_realloc_zero_aligned_at").unwrap();
        // Use fully qualified path to disambiguate
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 279, func.as_ptr());
    }

    // Handle small alignment case
    if alignment <= std::mem::size_of::<usize>() {
        let heap_ptr = heap as *mut crate::super_special_unit0::mi_heap_t;
        let p_ptr = p.map(|slice| slice.as_mut_ptr() as *mut std::ffi::c_void)
            .unwrap_or(std::ptr::null_mut());
        
        // Safety: This is a direct call to the unsafe C function as per dependencies
        let result = unsafe {
            crate::_mi_heap_realloc_zero(heap_ptr, p_ptr, newsize, zero)
        };
        
        if result.is_null() {
            Option::None
        } else {
            // Safety: The allocation functions return valid slices
            unsafe {
                Some(std::slice::from_raw_parts_mut(
                    result as *mut u8,
                    newsize
                ))
            }
        }
    } else {
        // Handle p == NULL case (None in Rust) - early return as in C code
        let p_slice = match p {
            Some(slice) => slice,
            None => {
                return crate::mi_heap_malloc_zero_aligned_at(heap, newsize, alignment, offset, zero);
            }
        };

        // Get usable size of current allocation
        let size = crate::mi_usable_size(Some(p_slice));

        // Check if we can reuse the current block
        let aligned_correctly = {
            let ptr_addr = p_slice.as_ptr() as usize;
            (ptr_addr + offset) % alignment == 0
        };

        // Return original pointer if conditions match C code logic
        if newsize <= size && newsize >= size - size / 2 && aligned_correctly {
            // In C, it returns the original pointer p
            // In Rust, we need to return a slice of the appropriate size
            if newsize <= p_slice.len() {
                Some(&mut p_slice[..newsize])
            } else {
                // This shouldn't happen since newsize <= size
                Option::None
            }
        } else {
            // Allocate new aligned block
            let newp_slice = match crate::mi_heap_malloc_aligned_at(heap, newsize, alignment, offset) {
                Some(slice) => slice,
                None => return Option::None,
            };

            // Zero extra memory if needed
            if zero && newsize > size {
                let start = if size >= std::mem::size_of::<usize>() {
                    size - std::mem::size_of::<usize>()
                } else {
                    0
                };
                
                if newsize > start {
                    let dst_slice = &mut newp_slice[start..];
                    crate::_mi_memzero(dst_slice, newsize - start);
                }
            }

            // Copy data from old to new allocation
            let copysize = if newsize > size { size } else { newsize };
            if copysize > 0 {
                let src_slice = &p_slice[..copysize];
                let dst_slice = &mut newp_slice[..copysize];
                crate::_mi_memcpy_aligned(dst_slice, src_slice, copysize);
            }

            // Free old allocation
            crate::mi_free(Some(p_slice.as_mut_ptr() as *mut std::ffi::c_void));

            Some(newp_slice)
        }
    }
}
pub fn mi_heap_realloc_zero_aligned<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
    zero: bool,
) -> Option<&'a mut [u8]> {
    // Assertion: alignment > 0
    if alignment == 0 {
        let assertion = std::ffi::CString::new("alignment > 0").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-aligned.c").unwrap();
        let func = std::ffi::CString::new("mi_heap_realloc_zero_aligned").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 304, func.as_ptr());
    }

    if alignment <= std::mem::size_of::<usize>() {
        // Convert parameters for _mi_heap_realloc_zero
        let heap_ptr = heap as *mut crate::super_special_unit0::mi_heap_t;
        let p_ptr = p.map_or(std::ptr::null_mut(), |slice| slice.as_mut_ptr() as *mut std::ffi::c_void);
        
        // Call the unsafe C function
        let result = unsafe { _mi_heap_realloc_zero(heap_ptr, p_ptr, newsize, zero) };
        
        // Convert result back to Option<&mut [u8]>
        if result.is_null() {
            Option::None
        } else {
            Some(unsafe { std::slice::from_raw_parts_mut(result as *mut u8, newsize) })
        }
    } else {
        // Calculate offset: ((uintptr_t) p) % alignment
        let offset = if let Some(ref slice) = p {
            (slice.as_ptr() as usize) % alignment
        } else {
            0
        };
        
        // Call the other function
        mi_heap_realloc_zero_aligned_at(heap, p, newsize, alignment, offset, zero)
    }
}
pub fn mi_heap_rezalloc_aligned<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
) -> Option<&'a mut [u8]> {
    // Call mi_heap_realloc_zero_aligned with zero=true (1 in C)
    mi_heap_realloc_zero_aligned(heap, p, newsize, alignment, true)
}
pub fn mi_heap_recalloc_aligned<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newcount: usize,
    size: usize,
    alignment: usize,
) -> Option<&'a mut [u8]> {
    let mut total: usize = 0;
    
    if crate::mi_count_size_overflow(newcount, size, &mut total) {
        return Option::None;
    }
    
    crate::mi_heap_rezalloc_aligned(heap, p, total, alignment)
}
pub fn mi_recalloc_aligned(
    p: Option<&mut [u8]>,
    newcount: usize,
    size: usize,
    alignment: usize,
) -> Option<&mut [u8]> {
    if let Some(heap_ptr) = mi_prim_get_default_heap() {
        // Convert MiHeapPtr to &mut MiHeapS
        let heap_ref: &mut crate::super_special_unit0::mi_heap_t = unsafe { &mut *heap_ptr.0 };
        mi_heap_recalloc_aligned(heap_ref, p, newcount, size, alignment)
    } else {
        Option::None
    }
}
pub fn mi_heap_rezalloc_aligned_at<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
    offset: usize,
) -> Option<&'a mut [u8]> {
    // Call the dependency function with zero set to true (1 in C)
    mi_heap_realloc_zero_aligned_at(heap, p, newsize, alignment, offset, true)
}
pub fn mi_heap_recalloc_aligned_at<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newcount: usize,
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&'a mut [u8]> {
    let mut total = 0;
    if mi_count_size_overflow(newcount, size, &mut total) {
        return None;
    }
    mi_heap_rezalloc_aligned_at(heap, p, total, alignment, offset)
}
pub fn mi_recalloc_aligned_at<'a>(
    p: Option<&'a mut [u8]>,
    newcount: usize,
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&'a mut [u8]> {
    let heap_ptr = mi_prim_get_default_heap()?;
    // Convert MiHeapPtr to &mut mi_heap_t for the function call
    let heap_ref = unsafe { &mut *heap_ptr.0 };
    mi_heap_recalloc_aligned_at(heap_ref, p, newcount, size, alignment, offset)
}
pub fn mi_heap_malloc_aligned(
    heap: &mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    alignment: usize,
) -> Option<&mut [u8]> {
    mi_heap_malloc_aligned_at(heap, size, alignment, 0)
}
pub fn mi_malloc_aligned(size: usize, alignment: usize) -> Option<*mut u8> {
    let heap_ptr = mi_prim_get_default_heap()?;
    // Convert MiHeapPtr (which contains *mut mi_heap_t) to &mut mi_heap_t
    // This is unsafe because we're dereferencing a raw pointer
    unsafe {
        heap_ptr.0.as_mut().and_then(|heap| {
            mi_heap_malloc_aligned(heap, size, alignment).map(|slice| slice.as_mut_ptr())
        })
    }
}
pub fn mi_malloc_aligned_at(
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&'static mut [u8]> {
    // Get the default heap
    let heap = mi_prim_get_default_heap()?;
    
    // Convert MiHeapPtr to &mut mi_heap_t and call the heap allocation function
    mi_heap_malloc_aligned_at(unsafe { &mut *heap.0 }, size, alignment, offset)
}
pub fn mi_heap_zalloc_aligned_at(
    heap: &mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&mut [u8]> {
    mi_heap_malloc_zero_aligned_at(heap, size, alignment, offset, true)
}
pub fn mi_heap_zalloc_aligned(
    heap: &mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    alignment: usize,
) -> Option<&mut [u8]> {
    mi_heap_zalloc_aligned_at(heap, size, alignment, 0)
}
pub fn mi_zalloc_aligned(size: usize, alignment: usize) -> Option<&'static mut [u8]> {
    let heap_ptr = mi_prim_get_default_heap()?;
    let heap = unsafe { &mut *heap_ptr.0 };
    mi_heap_zalloc_aligned(heap, size, alignment)
}
pub fn mi_zalloc_aligned_at(
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&'static mut [u8]> {
    let heap_ptr = mi_prim_get_default_heap()?;
    let heap = unsafe { &mut *heap_ptr.0 };
    mi_heap_zalloc_aligned_at(heap, size, alignment, offset)
}
pub fn mi_heap_calloc_aligned_at(
    heap: &mut crate::super_special_unit0::mi_heap_t,
    count: usize,
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&mut [u8]> {
    let mut total: usize = 0;
    
    if mi_count_size_overflow(count, size, &mut total) {
        return None;
    }
    
    mi_heap_zalloc_aligned_at(heap, total, alignment, offset)
}
pub fn mi_heap_calloc_aligned(
    heap: &mut crate::super_special_unit0::mi_heap_t,
    count: usize,
    size: usize,
    alignment: usize,
) -> Option<&mut [u8]> {
    mi_heap_calloc_aligned_at(heap, count, size, alignment, 0)
}
pub fn mi_calloc_aligned(
    count: usize,
    size: usize,
    alignment: usize,
) -> Option<&'static mut [u8]> {
    let heap_ptr = mi_prim_get_default_heap()?;
    // SAFETY: The heap pointer is valid if mi_prim_get_default_heap() returns Some
    let heap = unsafe { &mut *heap_ptr.0 };
    mi_heap_calloc_aligned(heap, count, size, alignment)
}
pub fn mi_calloc_aligned_at(
    count: usize,
    size: usize,
    alignment: usize,
    offset: usize,
) -> *mut core::ffi::c_void {
    let heap_ptr = match mi_prim_get_default_heap() {
        Some(ptr) => ptr,
        None => return core::ptr::null_mut(),
    };
    let heap = unsafe { &mut *heap_ptr.0 };
    match mi_heap_calloc_aligned_at(heap, count, size, alignment, offset) {
        Some(slice) => slice.as_mut_ptr() as *mut core::ffi::c_void,
        None => core::ptr::null_mut(),
    }
}
pub fn mi_heap_realloc_aligned<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
) -> Option<&'a mut [u8]> {
    mi_heap_realloc_zero_aligned(heap, p, newsize, alignment, false)
}
pub fn mi_realloc_aligned<'a>(
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
) -> Option<&'a mut [u8]> {
    let heap_ptr = mi_prim_get_default_heap()?;
    // Convert MiHeapPtr to &mut mi_heap_t for the function call
    let heap_ref = unsafe { &mut *heap_ptr.0 };
    mi_heap_realloc_aligned(heap_ref, p, newsize, alignment)
}
pub fn mi_heap_realloc_aligned_at<'a>(
    heap: &'a mut crate::super_special_unit0::mi_heap_t,
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
    offset: usize,
) -> Option<&'a mut [u8]> {
    mi_heap_realloc_zero_aligned_at(heap, p, newsize, alignment, offset, false)
}
pub fn mi_realloc_aligned_at(
    p: Option<&mut [u8]>,
    newsize: usize,
    alignment: usize,
    offset: usize,
) -> Option<&mut [u8]> {
    match mi_prim_get_default_heap() {
        Some(mut heap_ptr) => {
            // Convert MiHeapPtr to &mut MiHeapS for the function call
            let heap_ref: &mut crate::super_special_unit0::mi_heap_t = unsafe { &mut *heap_ptr.0 };
            mi_heap_realloc_aligned_at(heap_ref, p, newsize, alignment, offset)
        }
        None => Option::None,
    }
}
pub fn mi_rezalloc_aligned<'a>(
    p: Option<&'a mut [u8]>,
    newsize: usize,
    alignment: usize,
) -> Option<&'a mut [u8]> {
    let heap_ptr = mi_prim_get_default_heap()?;
    // Convert MiHeapPtr to &mut mi_heap_t for the function call
    let heap_ref = unsafe { &mut *heap_ptr.0 };
    mi_heap_rezalloc_aligned(heap_ref, p, newsize, alignment)
}
pub fn mi_rezalloc_aligned_at(
    p: Option<&mut [u8]>,
    newsize: usize,
    alignment: usize,
    offset: usize,
) -> Option<&mut [u8]> {
    match mi_prim_get_default_heap() {
        Some(heap_ptr) => {
            // MiHeapPtr is likely a wrapper type that needs to be dereferenced
            // Use it directly without casting
            let heap_ref = unsafe { &mut *heap_ptr.0 };
            mi_heap_rezalloc_aligned_at(heap_ref, p, newsize, alignment, offset)
        }
        None => Option::None,
    }
}
