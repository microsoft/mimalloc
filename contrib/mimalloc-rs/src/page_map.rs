use crate::*;
use crate::mi_memkind_t::mi_memkind_t::MI_MEM_NONE;
use crate::mi_submap_t::mi_submap_t;
use std::ffi::CStr;
use std::ffi::CString;
use std::ffi::c_void;
use std::os::raw::c_char;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
pub type mi_bfield_t = usize;

pub static MI_PAGE_MAP_COMMIT: AtomicUsize = AtomicUsize::new(0);

pub fn mi_page_map_is_committed(idx: usize, pbit_idx: Option<&mut usize>) -> bool {
    let commit = MI_PAGE_MAP_COMMIT.load(Ordering::Relaxed);
    let bit_idx = idx / ((1_usize << ((47 - 13) - (13 + 3))) / (1_usize << (3 + 3)));
    
    if bit_idx >= (1_usize << (3 + 3)) {
        _mi_assert_fail("bit_idx < MI_BFIELD_BITS", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c", 209, "mi_page_map_is_committed");
    }
    
    if let Some(pbit_idx_ref) = pbit_idx {
        *pbit_idx_ref = bit_idx;
    }
    
    (commit & (1_usize << bit_idx)) != 0
}

pub fn _mi_assert_fail(assertion: &str, file: &str, line: u32, func: &str) {
    let msg = format!("Assertion failed: {}, file: {}, line: {}, function: {}", assertion, file, line, func);
    panic!("{}", msg);
}
pub fn _mi_safe_ptr_page(p: *const ()) -> Option<Box<mi_page_t>> {

    if p.is_null() {
        return Option::None;
    }

    // Check if p >= mi_page_map_max_address
    let max_addr = MI_PAGE_MAP_MAX_ADDRESS.load(Ordering::Relaxed);
    if !max_addr.is_null() && p >= max_addr as *const () {
        return Option::None;
    }

    let mut sub_idx: usize = 0;
    let idx = _mi_page_map_index(p, Some(&mut sub_idx));

    if !mi_page_map_is_committed(idx, Option::None) {
        return Option::None;
    }

    let page_map = _MI_PAGE_MAP.load(Ordering::Relaxed);
    if page_map.is_null() {
        return Option::None;
    }

    // Calculate the sub array pointer
    let sub_ptr = unsafe { page_map.add(idx) };
    let sub = unsafe { *sub_ptr };
    if sub.is_null() {
        return Option::None;
    }

    let page_ptr = unsafe { sub.add(sub_idx) };
    let page = unsafe { *page_ptr };
    
    if page.is_null() {
        Option::None
    } else {
        // Convert raw pointer to Box (assuming ownership semantics)
        Some(unsafe { Box::from_raw(page) })
    }
}

pub fn mi_page_map_cannot_commit() {
    let msg = CStr::from_bytes_with_nul(b"unable to commit the allocation page-map on-demand\n\0")
        .expect("NUL-terminated warning message");
    _mi_warning_message(msg, std::ptr::null_mut());
}
// Import global variables and functions from dependencies

// Import the type alias for mi_submap_t
// Import MI_MEM_NONE directly

// The function from dependencies that we need to call
fn mi_page_map_ensure_committed(idx: usize, sub: &mut Option<mi_submap_t>) -> bool {
    // This function is assumed to exist based on the C code
    // It would be defined elsewhere in the translated codebase
    todo!("Implement mi_page_map_ensure_committed")
}

pub fn mi_page_map_ensure_submap_at(idx: usize, submap: &mut Option<mi_submap_t>) -> bool {
    // Check that submap is not null and contains None (C's NULL check)
    if submap.is_none() {
        // This is a runtime assertion in C, we'll mimic it
        let assertion = b"submap!=NULL && *submap==NULL\0";
        let fname = b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c\0";
        let func = b"mi_page_map_ensure_submap_at\0";
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr() as *const std::os::raw::c_char,
            fname.as_ptr() as *const std::os::raw::c_char,
            313,
            func.as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // Ensure submap contains None (NULL in C)
    if submap.is_some() && submap.as_ref().unwrap().is_some() {
        let assertion = b"submap!=NULL && *submap==NULL\0";
        let fname = b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c\0";
        let func = b"mi_page_map_ensure_submap_at\0";
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr() as *const std::os::raw::c_char,
            fname.as_ptr() as *const std::os::raw::c_char,
            313,
            func.as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    let mut sub: Option<mi_submap_t> = Option::None;
    
    // Try to commit the existing submap
    if !mi_page_map_ensure_committed(idx, &mut sub) {
        return false;
    }
    
    // If sub is None (NULL in C), allocate a new submap
    if sub.is_none() {
        let mut memid = crate::MiMemid {
            mem: crate::MiMemidMem::Os(crate::MiMemidOsInfo {
                base: Option::None,
                size: 0,
            }),
            memkind: MI_MEM_NONE,
            is_pinned: false,
            initially_committed: false,
            initially_zero: false,
        };
        
        // Calculate submap size: (1 << 13) * sizeof(mi_page_t*)
        // In Rust, we need to know the actual size of *mut mi_page_t
        let ptr_size = std::mem::size_of::<*mut crate::mi_page_t>();
        let submap_size = (1usize << 13) * ptr_size;
        
        // Allocate zeroed memory
        unsafe {
            let raw_ptr = crate::_mi_os_zalloc(submap_size, &mut memid);
            
            if raw_ptr.is_null() {
                let msg = std::ffi::CStr::from_bytes_with_nul(b"internal error: unable to extend the page map\0")
                    .expect("NUL-terminated error message");
                crate::_mi_warning_message(msg, std::ptr::null_mut());
                return false;
            }
            
            // Convert raw pointer to mi_submap_t
            // Based on the dependency: mi_submap_t = Option<Box<Vec<Option<Box<MiPage>>>>>
            // We need to interpret the allocated memory as a vector of page pointers
            
            // Create a vector of None values with the appropriate size
            let mut pages_vec = Vec::with_capacity(1 << 13);
            for _ in 0..(1 << 13) {
                pages_vec.push(Option::None);
            }
            
            let boxed_vec = Box::new(pages_vec);
            let new_sub: mi_submap_t = Some(boxed_vec);
            
            // Now do the atomic compare-and-exchange
            // We need to work with the global _MI_PAGE_MAP which is an AtomicPtr<*mut *mut mi_page_t>
            // First, we need to create a raw pointer from our sub value
            let sub_ptr: *mut *mut *mut crate::mi_page_t = match &new_sub {
                Some(boxed_vec) => {
                    // Get the raw pointer to the box
                    let raw_box = Box::into_raw(boxed_vec.clone());
                    raw_box as *mut *mut *mut crate::mi_page_t
                }
                _ => std::ptr::null_mut(),
            };
            
            // Try to atomically set the page map entry
            let expected = std::ptr::null_mut();
            
            // For now, we'll use compare_exchange on the global
            // Note: In the real mimalloc, _MI_PAGE_MAP would be an array, but here it's a single AtomicPtr
            if crate::_MI_PAGE_MAP.compare_exchange(
                expected,
                sub_ptr,
                std::sync::atomic::Ordering::AcqRel,
                std::sync::atomic::Ordering::Acquire,
            ).is_err() {
                // CAS failed, free the memory we allocated
                crate::_mi_os_free(sub_ptr as *mut std::ffi::c_void, submap_size, memid);
                
                // Get the current value (which won the race)
                let current = crate::_MI_PAGE_MAP.load(std::sync::atomic::Ordering::Acquire);
                
                // Convert current back to mi_submap_t
                if !current.is_null() {
                    // This is unsafe - we're assuming the pointer is valid
                    let vec_ptr = current as *mut Vec<Option<Box<crate::mi_submap_t::MiPage>>>;
                    let boxed_vec = unsafe { Box::from_raw(vec_ptr) };
                    sub = Some(Some(boxed_vec));
                }
            } else {
                // CAS succeeded, use our allocated submap
                sub = Some(new_sub);
            }
        }
    }
    
    // Assert that sub is not None (NULL in C)
    if sub.is_none() {
        let assertion = b"sub!=NULL\0";
        let fname = b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c\0";
        let func = b"mi_page_map_ensure_submap_at\0";
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr() as *const std::os::raw::c_char,
            fname.as_ptr() as *const std::os::raw::c_char,
            334,
            func.as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // Set the output parameter
    *submap = sub;
    true
}
pub fn mi_page_map_set_range_prim(
    page: &crate::mi_submap_t::MiPage,
    mut idx: usize,
    mut sub_idx: usize,
    mut slice_count: usize,
) -> bool {
    const SUBMAP_SIZE: usize = 1usize << 13;

    while slice_count > 0 {
        let mut sub: Option<mi_submap_t> = None;
        if !mi_page_map_ensure_committed(idx, &mut sub) {
            return false;
        }

        let sub = sub
            .as_mut()
            .expect("mi_page_map_ensure_committed should set sub on success");

        if sub.is_none() {
            _mi_assert_fail(
                "sub!=NULL",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c",
                346,
                "mi_page_map_set_range_prim",
            );
            return false;
        }

        let sub_box = sub.as_mut().unwrap();
        let sub_vec = sub_box.as_mut(); // &mut Vec<Option<Box<crate::mi_submap_t::MiPage>>>

        while slice_count > 0 && sub_idx < SUBMAP_SIZE {
            if sub_idx < sub_vec.len() {
                sub_vec[sub_idx] = Some(Box::new(page.clone()));
            }
            slice_count -= 1;
            sub_idx += 1;
        }

        idx += 1;
        sub_idx = 0;
    }

    true
}

pub fn mi_page_map_set_range(
    page: Option<&crate::mi_submap_t::MiPage>,
    idx: usize,
    sub_idx: usize,
    slice_count: usize,
) -> bool {
    // Triple negation (!(!(!x))) is equivalent to !x in boolean logic
    // We'll call mi_page_map_set_range_prim with page or a null reference
    let result = match page {
        Some(p) => mi_page_map_set_range_prim(p, idx, sub_idx, slice_count),
        None => {
            // Create a null pointer equivalent
            let null_ptr: *const c_char = std::ptr::null();
            // Cast to the expected type for the function
            let null_page = unsafe { &*(null_ptr as *const crate::mi_submap_t::MiPage) };
            mi_page_map_set_range_prim(null_page, idx, sub_idx, slice_count)
        }
    };

    if !result {
        // If result is false and page is not null, call again with null
        if page.is_some() {
            let null_ptr: *const c_char = std::ptr::null();
            let null_page = unsafe { &*(null_ptr as *const crate::mi_submap_t::MiPage) };
            mi_page_map_set_range_prim(null_page, idx, sub_idx, slice_count);
        }
        return false;
    }
    true
}
pub fn mi_page_map_get_idx(
    page: &mi_page_t, 
    sub_idx: Option<&mut usize>, 
    slice_count: Option<&mut usize>
) -> Option<usize> {
    // Use mutable variables for the calculations
    let mut page_size: usize = 0;
    
    // Call mi_page_area with mutable reference to page_size
    let page_start = mi_page_area(page, Some(&mut page_size))?;
    
    // Check if page_size is greater than 4194304 (2^22)
    // 4194304 = (1 << 3) * (8 * (1 * (1UL << (13 + 3))))
    // 65536 = (1UL << (13 + 3))
    const MAX_SIZE: usize = 4194304;  // 2^22
    const SLICE_SIZE: usize = 65536;   // 2^16
    
    if page_size > MAX_SIZE {
        page_size = MAX_SIZE - SLICE_SIZE;
    }
    
    // Calculate slice count
    // Convert pointers to usize for arithmetic
    let page_start_addr = page_start as usize;
    let page_addr = page as *const mi_page_t as usize;
    let offset = page_start_addr.wrapping_sub(page_addr);
    
    if let Some(slice_count_ref) = slice_count {
        *slice_count_ref = mi_slice_count_of_size(page_size) + (offset / SLICE_SIZE);
    }
    
    // Call _mi_page_map_index with appropriate pointer conversion
    let page_ptr = page as *const mi_page_t as *const ();
    Some(_mi_page_map_index(page_ptr, sub_idx))
}
pub fn _mi_page_map_unregister(page: Option<&mut mi_page_t>) -> () {
    // Check assertions (lines 3-5)
    {
        let page_map = _MI_PAGE_MAP.load(Ordering::Relaxed);
        if page_map.is_null() {
            let assertion = "_mi_page_map != NULL";
            let fname = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c";
            let line = 393;
            let func = "_mi_page_map_unregister";
            _mi_assert_fail(
                assertion,
                fname,
                line,
                func,
            );
        }
    }

    {
        if page.is_none() {
            let assertion = "page != NULL";
            let fname = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c";
            let line = 394;
            let func = "_mi_page_map_unregister";
            _mi_assert_fail(
                assertion,
                fname,
                line,
                func,
            );
        }
    }

    {
        let page_ref = page.as_ref().unwrap();
        let alignment = 1_usize << (13 + 3); // MI_PAGE_ALIGN
        let p = (*page_ref) as *const mi_page_t as *mut std::ffi::c_void;
        if !_mi_is_aligned(Some(unsafe { &mut *(p as *mut std::ffi::c_void) }), alignment) {
            let assertion = "_mi_is_aligned(page, MI_PAGE_ALIGN)";
            let fname = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c";
            let line = 395;
            let func = "_mi_page_map_unregister";
            _mi_assert_fail(
                assertion,
                fname,
                line,
                func,
            );
        }
    }

    // Early return if _mi_page_map is NULL (lines 6-9)
    let page_map = _MI_PAGE_MAP.load(Ordering::Relaxed);
    if page_map.is_null() {
        return;
    }

    // Get index and related values (lines 10-13)
    let mut slice_count: usize = 0;
    let mut sub_idx: usize = 0;
    let page_ref = page.unwrap();
    let idx = mi_page_map_get_idx(page_ref, Some(&mut sub_idx), Some(&mut slice_count));

    match idx {
        Some(idx_val) => {
            mi_page_map_set_range(Option::None, idx_val, sub_idx, slice_count);
        }
        None => {
            // Handle case where mi_page_map_get_idx returns None
            // In original C, this would likely be an error condition
            let assertion = "mi_page_map_get_idx failed";
            let fname = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c";
            let line = 413;
            let func = "_mi_page_map_unregister";
            _mi_assert_fail(
                assertion,
                fname,
                line,
                func,
            );
        }
    }
}
pub fn mi_is_in_heap_region(p: Option<*const ()>) -> bool {
    // Use Option<*const ()> to represent nullable pointer
    // Check if pointer is Some (non-null) and call _mi_safe_ptr_page
    // Return true if result is Some (page found), false otherwise
    p.is_some_and(|ptr| _mi_safe_ptr_page(ptr).is_some())
}
pub fn _mi_page_map_init() -> bool {
    
    // Line 3: Get clamped vbits
    // Use the correct enum variant from the original C code
    let mut vbits = mi_option_get_clamp(
        MiOption::MaxVabits,  // Fixed: use correct variant
        0,
        (1 << 3) * 8
    ) as usize;
    
    if vbits == 0 {
        // Line 6-10: Get virtual address bits and cap at 47
        let bits = _mi_os_virtual_address_bits();
        vbits = if bits >= 48 { 47 } else { bits };
    }
    
    // Line 12: Assert MI_MAX_VABITS >= vbits
    let max_vabits = 47; // Assuming MI_MAX_VABITS is 47
    if !(max_vabits >= vbits) {
        let assertion = CString::new("MI_MAX_VABITS >= vbits").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c").unwrap();
        let func = CString::new("_mi_page_map_init").unwrap();
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr(),
            fname.as_ptr(),
            240,
            func.as_ptr()
        );
    }
    
    // Line 13: Set max address
    let max_address = if vbits >= ((1 << 3) * 8) {
        (usize::MAX - (1 << (13 + 3))) + 1
    } else {
        1 << vbits
    };
    MI_PAGE_MAP_MAX_ADDRESS.store(max_address as *mut (), std::sync::atomic::Ordering::Release);
    
    // Line 14: Set page map count
    let page_map_count = 1 << ((vbits - 13) - (13 + 3));
    MI_PAGE_MAP_COUNT.store(page_map_count, std::sync::atomic::Ordering::Release);
    
    // Line 15: Assert page map count limit
    let max_count = 1 << ((47 - 13) - (13 + 3));
    if !(page_map_count <= max_count) {
        let assertion = CString::new("mi_page_map_count <= MI_PAGE_MAP_COUNT").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c").unwrap();
        let func = CString::new("_mi_page_map_init").unwrap();
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr(),
            fname.as_ptr(),
            243,
            func.as_ptr()
        );
    }
    
    // Line 16-19: Calculate sizes
    let os_page_size = _mi_os_page_size();
    let page_map_size = _mi_align_up(page_map_count * std::mem::size_of::<*mut *mut mi_page_t>(), os_page_size);
    let submap_size = (1 << 13) * std::mem::size_of::<*mut mi_page_t>();
    let reserve_size = page_map_size + submap_size;
    
    // Line 20: Determine commit flag
    // Use the correct enum variant from the original C code
    let commit = (page_map_size <= (64 * 1024) || 
                 mi_option_is_enabled(MiOption::PagemapCommit)) ||  // Fixed: use correct variant
                 _mi_os_has_overcommit();
    
    // Line 21: Allocate memory
    let mut memid = MI_PAGE_MAP_MEMID.lock().unwrap();
    let page_map_ptr = _mi_os_alloc_aligned(
        reserve_size,
        1,
        commit,
        true,
        &mut *memid
    );
    
    // Line 22-26: Check allocation
    if page_map_ptr.is_none() {
        let message = format!("unable to reserve virtual memory for the page map ({} KiB)\n", 
                             page_map_size / 1024);
        let c_msg = CString::new(message).unwrap();
        crate::alloc::_mi_error_message(12, c_msg.as_ptr() as *const c_char);
        return false;
    }
    
    // Convert to raw pointer
    let page_map = page_map_ptr.unwrap().as_ptr() as *mut *mut *mut mi_page_t;
    _MI_PAGE_MAP.store(page_map, std::sync::atomic::Ordering::Release);
    
    // Line 27-31: Zero memory if needed
    if memid.initially_committed && !memid.initially_zero {
        let msg = std::ffi::CStr::from_bytes_with_nul(b"internal: the page map was committed but not zero initialized!\n\0").unwrap();
        _mi_warning_message(msg, std::ptr::null_mut());
        let slice = unsafe { std::slice::from_raw_parts_mut(page_map as *mut u8, page_map_size) };
        _mi_memzero_aligned(slice, page_map_size);
    }
    
    // Line 32: Store commit state
    MI_PAGE_MAP_COMMIT.store(
        if memid.initially_committed { !0 } else { 0 },
        std::sync::atomic::Ordering::Release
    );
    
    // Line 33: Calculate sub0 pointer
    let sub0 = unsafe {
        (page_map as *mut u8).add(page_map_size) as *mut *mut mi_page_t
    };
    
    // Line 34-41: Commit sub0 if needed
    if !memid.initially_committed {
        if !_mi_os_commit(Some(sub0 as *mut ()), submap_size, None) {
            mi_page_map_cannot_commit();
            return false;
        }
    }
    
    // Line 42-45: Zero sub0 if needed
    if !memid.initially_zero {
        let slice = unsafe { std::slice::from_raw_parts_mut(sub0 as *mut u8, submap_size) };
        _mi_memzero_aligned(slice, submap_size);
    }
    
    // Line 46-51: Ensure first entry is committed
    let mut nullsub: Option<mi_submap_t> = None;  // Fixed: wrap in Option
    if !mi_page_map_ensure_committed(0, &mut nullsub) {
        mi_page_map_cannot_commit();
        return false;
    }
    
    // Line 52: Store sub0 in first entry
    unsafe {
        *page_map = sub0;
    }
    _MI_PAGE_MAP.store(page_map, std::sync::atomic::Ordering::Release);
    
    // Line 53: Verify NULL pointer maps to NULL page
    let null_page = unsafe { _mi_ptr_page(std::ptr::null()) };
    if null_page != std::ptr::null_mut() {
        let assertion = CString::new("_mi_ptr_page(NULL)==NULL").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c").unwrap();
        let func = CString::new("_mi_page_map_init").unwrap();
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr(),
            fname.as_ptr(),
            283,
            func.as_ptr()
        );
    }
    
    // Line 54: Return success
    true
}
pub fn _mi_page_map_register(page: Option<&mut mi_page_t>) -> bool {
    // Line 3: Assert page is not NULL
    if page.is_none() {
        _mi_assert_fail(
            "page != NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c",
            379,
            "_mi_page_map_register",
        );
        return false;
    }
    
    // Safe unwrap since we already checked
    let page_ref = page.unwrap();
    
    // Line 4: Assert page is aligned
    // Convert page pointer to c_void pointer for alignment check
    let page_ptr = page_ref as *const mi_page_t as *mut std::ffi::c_void;
    // _mi_is_aligned expects Option<&mut c_void>, so pass None for null check
    if !_mi_is_aligned(Some(unsafe { &mut *(page_ptr) }), 1 << (13 + 3)) {
        _mi_assert_fail(
            "_mi_is_aligned(page, MI_PAGE_ALIGN)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c",
            380,
            "_mi_page_map_register",
        );
        return false;
    }
    
    // Line 5: Assert _mi_page_map is not NULL
    let page_map_ptr = _MI_PAGE_MAP.load(std::sync::atomic::Ordering::Relaxed);
    if page_map_ptr.is_null() {
        _mi_assert_fail(
            "_mi_page_map != NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c",
            381,
            "_mi_page_map_register",
        );
        return false;
    }
    
    // Lines 6-12: Initialize page map if needed
    // Note: This check is redundant with line 5, but keeping it as in original C code
    if page_map_ptr.is_null() {
        if !_mi_page_map_init() {
            return false;
        }
    }
    
    // Line 13: Re-assert _mi_page_map is not NULL
    if _MI_PAGE_MAP.load(std::sync::atomic::Ordering::Relaxed).is_null() {
        _mi_assert_fail(
            "_mi_page_map!=NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c",
            385,
            "_mi_page_map_register",
        );
        return false;
    }
    
    // Lines 14-16: Get index and slice information
    let mut slice_count: usize = 0;
    let mut sub_idx: usize = 0;
    
    let idx_option = mi_page_map_get_idx(
        page_ref,
        Some(&mut sub_idx),
        Some(&mut slice_count),
    );
    
    if let Some(idx) = idx_option {
        // Line 17: Set the page map range
        // According to the dependency, mi_page_map_set_range expects Option<&crate::mi_submap_t::MiPage>
        // We need to cast the page pointer appropriately. Since mi_page_t and crate::mi_submap_t::MiPage
        // might be compatible types, we'll use a transmute to convert the reference.
        // SAFETY: We assume mi_page_t and crate::mi_submap_t::MiPage are compatible types
        // as indicated by the dependency signature
        let page_as_submap: &crate::mi_submap_t::MiPage = unsafe {
            std::mem::transmute(page_ref)
        };
        mi_page_map_set_range(
            Some(page_as_submap),
            idx,
            sub_idx,
            slice_count,
        )
    } else {
        false
    }
}

pub fn _mi_page_map_unsafe_destroy(subproc: Option<&mut mi_subproc_t>) {
    // Assertions converted to runtime checks with the provided function
    if subproc.is_none() {
        _mi_assert_fail(
            "subproc != NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c",
            289,
            "_mi_page_map_unsafe_destroy",
        );
        return;
    }
    
    let subproc = subproc.unwrap();
    
    // Get the current value of _MI_PAGE_MAP
    let page_map_ptr = _MI_PAGE_MAP.load(Ordering::Acquire);
    if page_map_ptr.is_null() {
        _mi_assert_fail(
            "_mi_page_map != NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-map.c",
            290,
            "_mi_page_map_unsafe_destroy",
        );
        return;
    }
    
    let count = MI_PAGE_MAP_COUNT.load(Ordering::Acquire);
    
    // Iterate from idx = 1 to count (exclusive in Rust, matching C's < comparison)
    for idx in 1..count {
        if mi_page_map_is_committed(idx, None) {
            // Access the page map directly instead of calling _mi_page_map_at
            unsafe {
                // page_map_ptr is *mut *mut mi_page_t, so we create a slice of *mut mi_page_t
                let slice = std::slice::from_raw_parts_mut(
                    page_map_ptr as *mut *mut mi_page_t,
                    count
                );
                let sub_ptr = slice[idx];
                
                if !sub_ptr.is_null() {
                    // Convert the raw pointer to a Box for proper memory management
                    let sub = Box::from_raw(sub_ptr as *mut Vec<Option<Box<MiPage>>>);
                    let sub_ptr = Box::into_raw(sub) as *mut c_void;
                    
                    let memid = _mi_memid_create_os(
                        Some(sub_ptr),
                        (1 << 13) * std::mem::size_of::<*mut mi_page_t>(),
                        true,  // 1 in C
                        false, // 0 in C
                        false, // 0 in C
                    );
                    
                    // Access the size field based on the enum variant
                    let size = match &memid.mem {
                        MiMemidMem::Os(os_info) => os_info.size,
                        _ => 0, // Should not happen for OS memory
                    };
                    
                    _mi_os_free_ex(sub_ptr, size, true, memid, Some(subproc));
                    
                    // Clear the entry in the page map (non-atomic as per original C code)
                    slice[idx] = std::ptr::null_mut();
                }
            }
        }
    }
    
    // Free the main page map
    {
        let memid_guard = MI_PAGE_MAP_MEMID.lock().unwrap();
        let memid = &*memid_guard;
        
        // Access the size field based on the enum variant
        let size = match &memid.mem {
            MiMemidMem::Os(os_info) => os_info.size,
            _ => 0, // Should not happen for OS memory
        };
        
        // Create a new MiMemid by manually copying the fields since Clone is not implemented
        let memid_to_pass = MiMemid {
            mem: match &memid.mem {
                MiMemidMem::Os(os_info) => MiMemidMem::Os(MiMemidOsInfo {
                    base: os_info.base.clone(),
                    size: os_info.size,
                }),
                MiMemidMem::Arena(arena_info) => MiMemidMem::Arena(mi_memid_arena_info_t {
                    arena: arena_info.arena,
                    slice_index: arena_info.slice_index,
                    slice_count: arena_info.slice_count,
                }),
                MiMemidMem::Meta(meta_info) => MiMemidMem::Meta(MiMemidMetaInfo {
                    meta_page: meta_info.meta_page,
                    block_index: meta_info.block_index,
                    block_count: meta_info.block_count,
                }),
            },
            memkind: memid.memkind,
            is_pinned: memid.is_pinned,
            initially_committed: memid.initially_committed,
            initially_zero: memid.initially_zero,
        };
        
        _mi_os_free_ex(
            page_map_ptr as *mut c_void,
            size,
            true, // 1 in C
            memid_to_pass,
            Some(subproc),
        );
    }
    
    // Reset global variables
    _MI_PAGE_MAP.store(std::ptr::null_mut(), Ordering::Release);
    MI_PAGE_MAP_COUNT.store(0, Ordering::Release);
    {
        let mut memid_guard = MI_PAGE_MAP_MEMID.lock().unwrap();
        *memid_guard = _mi_memid_none();
    }
    MI_PAGE_MAP_MAX_ADDRESS.store(std::ptr::null_mut(), Ordering::Release);
    MI_PAGE_MAP_COMMIT.store(0, Ordering::Release);
}

pub fn _mi_page_map_unregister_range(start: *const (), size: usize) {
    // Check if _mi_page_map is null (0)
    if crate::_MI_PAGE_MAP.load(std::sync::atomic::Ordering::Relaxed).is_null() {
        return;
    }

    let slice_count = crate::_mi_divide_up(size, 1 << (13 + 3));
    let mut sub_idx = 0;
    let idx = crate::_mi_page_map_index(start, Some(&mut sub_idx));
    
    crate::mi_page_map_set_range(None, idx, sub_idx, slice_count);
}
