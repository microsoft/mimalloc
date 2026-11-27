use crate::*;
use std::ffi::CString;
use std::ffi::c_void;
use std::ptr::NonNull;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::Ordering;
pub fn mi_meta_page_of_ptr(p: *mut c_void, block_idx: *mut usize) -> *mut crate::mi_meta_page_t::mi_meta_page_t {
    if p.is_null() {
        return std::ptr::null_mut();
    }
    
    // Convert p to &mut () for mi_align_down_ptr
    let p_as_mut_void = unsafe { &mut *(p as *mut ()) };
    
    // Calculate aligned pointer using mi_align_down_ptr
    let aligned_ptr = mi_align_down_ptr(Some(p_as_mut_void), 1 << (13 + 3));
    
    if aligned_ptr.is_none() {
        return std::ptr::null_mut();
    }
    
    let aligned_ptr = aligned_ptr.unwrap() as *mut () as *mut u8;
    let guard_page_size = _mi_os_secure_guard_page_size();
    
    // Calculate the mpage pointer
    let mpage = unsafe {
        (aligned_ptr.add(guard_page_size)) as *mut crate::mi_meta_page_t::mi_meta_page_t
    };
    
    // Calculate block index if requested
    if !block_idx.is_null() {
        let p_addr = p as *mut u8 as usize;
        let mpage_addr = mpage as *mut u8 as usize;
        let offset = p_addr.wrapping_sub(mpage_addr);
        unsafe {
            *block_idx = offset / (1 << (16 - (6 + 3)));
        }
    }
    
    mpage
}
pub fn mi_meta_block_start(
    mpage: *mut crate::mi_meta_page_t::mi_meta_page_t,
    block_idx: usize,
) -> *mut c_void {
    let guard_page_size = _mi_os_secure_guard_page_size();
    
    // Assertion 1: Check alignment
    {
        let base = unsafe { 
            (mpage as *mut u8).sub(guard_page_size) as *mut c_void 
        };
        let is_aligned = _mi_is_aligned(
            unsafe { Some(&mut *base) },
            1 << 16
        );
        if !is_aligned {
            let assertion = CString::new(
                "_mi_is_aligned((uint8_t*)mpage - _mi_os_secure_guard_page_size(), MI_META_PAGE_ALIGN)"
            ).unwrap();
            let file = CString::new(
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/arena-meta.c"
            ).unwrap();
            let func = CString::new("mi_meta_block_start").unwrap();
            // Use explicit module path to disambiguate
            super_function_unit5::_mi_assert_fail(
                assertion.as_ptr(),
                file.as_ptr(),
                62,
                func.as_ptr(),
            );
        }
    }

    // Assertion 2: Check block index bounds
    if block_idx >= 512 {
        let assertion = CString::new("block_idx < MI_META_BLOCKS_PER_PAGE").unwrap();
        let file = CString::new(
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/arena-meta.c"
        ).unwrap();
        let func = CString::new("mi_meta_block_start").unwrap();
        super_function_unit5::_mi_assert_fail(
            assertion.as_ptr(),
            file.as_ptr(),
            63,
            func.as_ptr(),
        );
    }

    // Calculate pointer
    let base = unsafe { 
        (mpage as *mut u8).sub(guard_page_size) as *mut c_void 
    };
    let p = unsafe {
        (base as *mut u8).add(block_idx * 128) as *mut c_void
    };

    // Assertion 3: Check that mpage matches the page of the calculated pointer
    {
        let mut dummy_block_idx: usize = 0;
        let page_of_ptr = mi_meta_page_of_ptr(p, &mut dummy_block_idx as *mut usize);
        if mpage != page_of_ptr {
            let assertion = CString::new("mpage == mi_meta_page_of_ptr(p,NULL)").unwrap();
            let file = CString::new(
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/arena-meta.c"
            ).unwrap();
            let func = CString::new("mi_meta_block_start").unwrap();
            super_function_unit5::_mi_assert_fail(
                assertion.as_ptr(),
                file.as_ptr(),
                65,
                func.as_ptr(),
            );
        }
    }

    p
}

pub static mi_meta_pages: std::sync::atomic::AtomicPtr<crate::mi_meta_page_t::mi_meta_page_t> = 
    std::sync::atomic::AtomicPtr::new(std::ptr::null_mut());

pub fn mi_meta_page_zalloc() -> Option<*mut crate::mi_meta_page_t::mi_meta_page_t> {
    // Define constants from the C code
    const MI_META_PAGE_ALIGN: usize = 1 << (13 + 3); // 65536
    const MI_META_BLOCK_SIZE: usize = 1 << (16 - (6 + 3)); // 128
    const MI_META_BLOCKS_PER_PAGE: usize = MI_META_PAGE_ALIGN / MI_META_BLOCK_SIZE; // 512
    
    let mut memid = crate::MiMemid {
        mem: crate::MiMemidMem::Os(crate::MiMemidOsInfo {
            base: None,
            size: 0,
        }),
        memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_OS,
        is_pinned: false,
        initially_committed: false,
        initially_zero: false,
    };
    
    let mut subproc_guard = crate::_mi_subproc().lock().unwrap();
    let subproc = &mut *subproc_guard;
    
    // Allocate aligned memory
    let base_ptr = crate::_mi_arenas_alloc_aligned(
        subproc,
        MI_META_PAGE_ALIGN,
        MI_META_PAGE_ALIGN,
        0,
        true,
        true,
        None,
        0,
        -1,
        &mut memid,
    );
    
    if base_ptr.is_none() {
        return None;
    }
    
    let base_ptr = base_ptr.unwrap();
    
    // Check alignment
    if !crate::_mi_is_aligned(Some(unsafe { &mut *(base_ptr as *mut std::ffi::c_void) }), MI_META_PAGE_ALIGN) {
        let assertion = std::ffi::CString::new("_mi_is_aligned(base,MI_META_PAGE_ALIGN)").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/arena-meta.c").unwrap();
        let func = std::ffi::CString::new("mi_meta_page_zalloc").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 78, func.as_ptr());
        return None;
    }
    
    // Zero memory if not initially zero
    if !memid.initially_zero {
        let slice = unsafe { std::slice::from_raw_parts_mut(base_ptr as *mut u8, MI_META_PAGE_ALIGN) };
        crate::_mi_memzero_aligned(slice, MI_META_PAGE_ALIGN);
    }
    
    // Calculate mpage pointer
    let guard_offset = crate::_mi_os_secure_guard_page_size();
    let mpage_ptr = unsafe { (base_ptr as *mut u8).add(guard_offset) } as *mut crate::mi_meta_page_t::mi_meta_page_t;
    
    // Initialize the meta page
    unsafe {
        (*mpage_ptr).memid = memid;
        
        // Initialize bitmap
        crate::mi_bbitmap_init(
            &mut (*mpage_ptr).blocks_free,
            MI_META_BLOCKS_PER_PAGE,
            true,
        );
        
        // Calculate sizes
        let offset_of_blocks_free = std::mem::offset_of!(crate::mi_meta_page_t::mi_meta_page_t, blocks_free);
        let bitmap_size = crate::mi_bbitmap_size(MI_META_BLOCKS_PER_PAGE, Option::None);
        let mpage_size = offset_of_blocks_free + bitmap_size;
        
        let info_blocks = crate::_mi_divide_up(mpage_size, MI_META_BLOCK_SIZE);
        let guard_blocks = crate::_mi_divide_up(guard_offset, MI_META_BLOCK_SIZE);
        
        // Validate the blocks fit
        if !(info_blocks + (2 * guard_blocks) < MI_META_BLOCKS_PER_PAGE) {
            let assertion = std::ffi::CString::new("info_blocks + 2*guard_blocks < MI_META_BLOCKS_PER_PAGE").unwrap();
            let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/arena-meta.c").unwrap();
            let func = std::ffi::CString::new("mi_meta_page_zalloc").unwrap();
            crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 96, func.as_ptr());
            return None;
        }
        
        // Set free blocks in bitmap
        let free_start = info_blocks + guard_blocks;
        let free_count = MI_META_BLOCKS_PER_PAGE - info_blocks - (2 * guard_blocks);
        crate::mi_bbitmap_unsafe_setN(&mut (*mpage_ptr).blocks_free, free_start, free_count);
        
        // Atomically insert into global list
        let mut old = mi_meta_pages.load(Ordering::Acquire);
        loop {
            (*mpage_ptr).next.store(old, Ordering::Release);
            match mi_meta_pages.compare_exchange_weak(
                old,
                mpage_ptr,
                Ordering::AcqRel,
                Ordering::Acquire,
            ) {
                Ok(_) => break,
                Err(x) => old = x,
            }
        }
    }
    
    Some(mpage_ptr)
}
// The struct mi_meta_page_t is already defined via the import at line 2.
// Therefore, we should not redefine it here.
// The original C code for mi_meta_page_next function is:
// static mi_meta_page_t *mi_meta_page_next(mi_meta_page_t *mpage)
// {
//   return atomic_load_explicit(&mpage->next, memory_order_acquire);
// }

pub fn mi_meta_page_next(mpage: *mut crate::mi_meta_page_t::mi_meta_page_t) -> *mut crate::mi_meta_page_t::mi_meta_page_t {
    unsafe {
        if mpage.is_null() {
            std::ptr::null_mut()
        } else {
            (*mpage).next.load(std::sync::atomic::Ordering::Acquire)
        }
    }
}
static MI_META_PAGES: AtomicPtr<crate::mi_meta_page_t::mi_meta_page_t> = 
    AtomicPtr::new(std::ptr::null_mut());

pub fn _mi_meta_zalloc(size: usize, pmemid: &mut crate::mi_memid_t) -> Option<NonNull<std::ffi::c_void>> {
    // Assert that pmemid is not null (translated from line 3)
    // In Rust, we don't need to check for null pointer since &mut guarantees non-null
    // We'll just assert that the pointer/reference is valid by checking it's not a default value
    // But actually, the original C code checks pmemid != NULL, which in Rust is always true for &mut
    
    let size = crate::_mi_align_up(size, 1 << (16 - (6 + 3)));
    
    if size == 0 || size > (((1 << (6 + 3)) / 8) * (1 << (16 - (6 + 3)))) {
        return None;
    }
    
    let block_count = crate::_mi_divide_up(size, 1 << (16 - (6 + 3)));
    
    assert!(block_count > 0 && block_count < (1 << (6 + 3)));
    
    let mpage0 = MI_META_PAGES.load(Ordering::Acquire);
    let mut mpage = mpage0;
    let mut mpage_idx = 0;
    
    // Loop through meta pages to find free blocks
    while !mpage.is_null() {
        let mut block_idx = 0;
        if crate::mi_bbitmap_try_find_and_clearN(
            unsafe { &mut (*mpage).blocks_free },
            block_count,
            0,
            &mut block_idx
        ) {
            *pmemid = crate::_mi_memid_create_meta(mpage as *mut std::ffi::c_void, block_idx, block_count);
            return Some(NonNull::new(crate::mi_meta_block_start(mpage, block_idx)).unwrap());
        } else {
            // Get the next meta page
            let next_mpage = unsafe { (*mpage).next.load(Ordering::Acquire) };
            if !next_mpage.is_null() {
                mpage = next_mpage;
                mpage_idx += 1;
            } else {
                break;
            }
        }
    }
    
    // If meta pages changed during our search, retry
    if MI_META_PAGES.load(Ordering::Acquire) != mpage0 {
        return _mi_meta_zalloc(size, pmemid);
    }
    
    // Allocate new meta page
    let new_mpage = crate::mi_meta_page_zalloc();
    
    if let Some(mpage_ptr) = new_mpage {
        let mut block_idx = 0;
        if crate::mi_bbitmap_try_find_and_clearN(
            unsafe { &mut (*mpage_ptr).blocks_free },
            block_count,
            0,
            &mut block_idx
        ) {
            *pmemid = crate::_mi_memid_create_meta(mpage_ptr as *mut std::ffi::c_void, block_idx, block_count);
            return Some(NonNull::new(crate::mi_meta_block_start(mpage_ptr, block_idx)).unwrap());
        }
    }
    
    // Fallback to OS allocation
    crate::_mi_os_alloc(size, pmemid)
}

pub fn _mi_meta_is_meta_page(p: Option<*mut c_void>) -> bool {
    // Load the head of the meta pages linked list
    let mpage0 = crate::mi_meta_pages.load(Ordering::Acquire);
    let mut mpage_idx = mpage0;
    
    // Convert input pointer to raw pointer for comparison
    let p_ptr = p.unwrap_or(std::ptr::null_mut());
    
    // Traverse the linked list
    while !mpage_idx.is_null() {
        // Check if current node pointer matches input pointer
        if mpage_idx as *mut c_void == p_ptr {
            return true;
        }
        
        // Move to next node using the provided function
        mpage_idx = crate::mi_meta_page_next(mpage_idx);
    }
    
    false
}
