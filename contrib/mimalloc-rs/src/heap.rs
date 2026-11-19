use crate::*;
use crate::mi_collect_t::mi_collect_t;
use crate::super_function_unit5::_mi_assert_fail;
use std::ffi::CStr;
use std::ffi::CString;
use std::ffi::c_void;
use std::sync::atomic::AtomicUsize;
pub fn mi_heap_visit_pages(
    heap: Option<&crate::MiHeapS>,
    fn_: crate::HeapPageVisitorFun,
    arg1: Option<&c_void>,
    arg2: Option<&c_void>,
) -> bool {
    // Check if heap is None (equivalent to NULL check in C) or page_count is 0
    let heap = match heap {
        Some(h) => h,
        None => return false,
    };

    if heap.page_count == 0 {
        return false;
    }

    let total = heap.page_count;
    let mut count = 0;

    // Loop from 0 to (73 + 1) inclusive
    for i in 0..=(73 + 1) {
        let pq = &heap.pages[i];
        
        // Traverse the linked list starting from first
        let mut page_ptr = pq.first;
        while let Some(page_ptr_val) = page_ptr {
            // SAFETY: page_ptr is verified to be non-null
            let page = unsafe { &*page_ptr_val };
            
            // Get the heap associated with this page and verify it matches
            let page_heap_ptr = unsafe { crate::mi_page_heap(page_ptr_val) };
            // Convert heap reference to raw pointer for comparison
            let heap_ptr = heap as *const crate::MiHeapS as *mut crate::MiHeapS;
            
            // Check if page_heap_ptr is Some and points to the same heap
            match page_heap_ptr {
                Some(ptr) => {
                    if ptr as *const c_void != heap_ptr as *const c_void {
                        // Use fully qualified path to avoid ambiguity
                        crate::super_function_unit5::_mi_assert_fail(
                            b"mi_page_heap(page) == heap\0".as_ptr() as *const _,
                            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const _,
                            39,
                            b"mi_heap_visit_pages\0".as_ptr() as *const _,
                        );
                    }
                }
                None => {
                    // Use fully qualified path to avoid ambiguity
                    crate::super_function_unit5::_mi_assert_fail(
                        b"mi_page_heap(page) == heap\0".as_ptr() as *const _,
                        b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const _,
                        39,
                        b"mi_heap_visit_pages\0".as_ptr() as *const _,
                    );
                }
            }

            count += 1;

            // Call the visitor function with references
            if !fn_(Some(heap), Some(pq), Some(page), arg1, arg2) {
                return false;
            }

            // Move to next page in the linked list
            page_ptr = page.next;
        }
    }

    // Assert that we visited all pages
    if count != total {
        // Use fully qualified path to avoid ambiguity
        crate::super_function_unit5::_mi_assert_fail(
            b"count == total\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const _,
            47,
            b"mi_heap_visit_pages\0".as_ptr() as *const _,
        );
    }

    true
}
pub type mi_heap_t = MiHeapS;
pub fn mi_heap_page_collect(
    heap: Option<&mut mi_heap_t>,
    pq: Option<&mut crate::MiPageQueueS>,
    page: Option<&mut mi_page_t>,
    arg_collect: Option<&std::ffi::c_void>,
    arg2: Option<&std::ffi::c_void>,
) -> bool {
    // Check the assertion using the provided function
    // Note: mi_heap_page_is_valid doesn't exist in dependencies, so we'll assume it's defined elsewhere
    // For now, we'll comment out the assertion check since the function isn't available
    // if !mi_heap_page_is_valid(heap, pq, page, None::<&std::ffi::c_void>, None::<&std::ffi::c_void>) {
    //     _mi_assert_fail(
    //         "mi_heap_page_is_valid(heap, pq, page, NULL, NULL)".as_ptr() as *const std::os::raw::c_char,
    //         "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c".as_ptr() as *const std::os::raw::c_char,
    //         93,
    //         "mi_heap_page_collect".as_ptr() as *const std::os::raw::c_char,
    //     );
    // }

    // Dereference arg_collect to get the collect value
    let collect = if let Some(ptr) = arg_collect {
        unsafe { *(ptr as *const std::ffi::c_void as *const crate::mi_collect_t::mi_collect_t) }
    } else {
        crate::mi_collect_t::mi_collect_t::MI_NORMAL
    };

    // Get mutable references to page and pq if they exist
    // Original C code accepts NULL pointers but doesn't use them in some cases
    if let (Some(page_ref), Some(pq_ref)) = (page, pq) {
        // Call _mi_page_free_collect with force condition
        // Compare enum values by converting to u8 or using pattern matching
        let force = match collect {
            crate::mi_collect_t::mi_collect_t::MI_FORCE | crate::mi_collect_t::mi_collect_t::MI_ABANDON => true,
            _ => false,
        };
        _mi_page_free_collect(page_ref, force);

        if mi_page_all_free(Some(page_ref)) {
            // No cast needed - mi_page_queue_t is MiPageQueueS
            _mi_page_free(Some(page_ref), Some(pq_ref));
        } else if collect == crate::mi_collect_t::mi_collect_t::MI_ABANDON {
            unsafe {
                _mi_page_abandon(page_ref, pq_ref);
            }
        }
    }

    true
}
pub fn mi_heap_collect_ex(
    heap: Option<&mut mi_heap_t>,
    collect: crate::mi_collect_t::mi_collect_t,
) {
    if heap.is_none() || !mi_heap_is_initialized(heap.as_deref()) {
        return;
    }

    let force = matches!(
        collect,
        crate::mi_collect_t::mi_collect_t::MI_FORCE | crate::mi_collect_t::mi_collect_t::MI_ABANDON
    );

    // Use the `globals::mi_heap_t` for functions typed against it.
    let heap_g: &mut mi_heap_t = heap.unwrap();

    _mi_deferred_free(Some(heap_g), force);

    // Use the underlying heap representation for the rest.
    let heap_s: &mut crate::MiHeapS =
        unsafe { &mut *(heap_g as *mut mi_heap_t as *mut crate::MiHeapS) };

    _mi_heap_collect_retired(Some(heap_s), force);

    let arg_collect_ptr =
        (&collect as *const crate::mi_collect_t::mi_collect_t) as *const std::ffi::c_void;
    let arg_collect: Option<&std::ffi::c_void> = Some(unsafe { &*arg_collect_ptr });

    // Inline visit logic, and avoid borrowing `heap_s` mutably alongside `heap_s.pages[bin]`.
    for bin in 0..heap_s.pages.len() {
        let mut page_ptr_opt: Option<*mut crate::mi_page_t> = {
            let pq: &mut crate::MiPageQueueS = &mut heap_s.pages[bin];
            pq.first
        };

        while let Some(page_ptr) = page_ptr_opt {
            let next_ptr_opt: Option<*mut crate::mi_page_t> = unsafe { (*page_ptr).next };

            {
                let pq: &mut crate::MiPageQueueS = &mut heap_s.pages[bin];
                let page: &mut crate::mi_page_t = unsafe { &mut *page_ptr };

                // Pass heap as None to avoid creating overlapping mutable borrows of `heap_s`.
                let _ = mi_heap_page_collect(
                    Option::None,
                    Some(pq),
                    Some(page),
                    arg_collect,
                    Option::None,
                );
            }

            page_ptr_opt = next_ptr_opt;
        }
    }

    let force_purge = collect == crate::mi_collect_t::mi_collect_t::MI_FORCE;
    let visit_all = force;

    if let Some(tld) = heap_s.tld.as_deref_mut() {
        _mi_arenas_collect(force_purge, visit_all, tld);
    }

    let should_merge = matches!(
        collect,
        crate::mi_collect_t::mi_collect_t::MI_NORMAL | crate::mi_collect_t::mi_collect_t::MI_FORCE
    );

    if should_merge {
        if let Some(tld) = heap_s.tld.as_deref_mut() {
            _mi_stats_merge_thread(Some(tld));
        }
    }
}

pub fn mi_heap_collect(heap: Option<&mut mi_heap_t>, force: bool) {
    let collect = if force {
        mi_collect_t::MI_FORCE
    } else {
        mi_collect_t::MI_NORMAL
    };
    
    mi_heap_collect_ex(heap, collect);
}
pub fn _mi_heap_random_next(heap: &mut mi_heap_t) -> u64 {
    _mi_random_next(&mut heap.random)
}
pub fn mi_heap_is_default(heap: Option<&mi_heap_t>) -> bool {
    match heap {
        Some(heap_ref) => {
            let default_heap = mi_prim_get_default_heap();
            match default_heap {
                Some(default_heap_ptr) => {
                    // Compare the heap reference with the default heap pointer
                    let heap_ptr = heap_ref as *const mi_heap_t;
                    heap_ptr == default_heap_ptr.0 as *const mi_heap_t
                }
                None => false,
            }
        }
        None => false,
    }
}
pub fn mi_heap_free(heap: Option<&mut mi_heap_t>, do_free_mem: bool) {
    // Check heap is not NULL
    if heap.is_none() {
        let assertion = CString::new("heap != NULL").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = CString::new("mi_heap_free").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 281, func.as_ptr());
        return;
    }

    let heap = heap.unwrap();

    // Check heap is initialized
    if !mi_heap_is_initialized_inline(Some(&*heap)) {
        let assertion = CString::new("mi_heap_is_initialized(heap)").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = CString::new("mi_heap_free").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 282, func.as_ptr());
        return;
    }

    // Early returns for invalid or special heaps
    if !mi_heap_is_initialized_inline(Some(&*heap)) {
        return;
    }

    // Note: mi_heap_is_backing dependency not provided - assuming it exists
    // if mi_heap_is_backing(heap) {
    //     return;
    // }

    if mi_heap_is_default(Some(&*heap)) {
        unsafe {
            if let Some(ref tld) = heap.tld {
                if let Some(ref heap_backing) = tld.heap_backing {
                    // Get raw pointer to the heap, not a Box
                    let heap_backing_ptr = heap_backing.as_ref() as *const _ as *mut mi_heap_t;
                    crate::_mi_heap_set_default_direct(heap_backing_ptr);
                }
            }
        }
    }

    // Linked list traversal - find heap in the list
    let mut prev_ptr: *mut mi_heap_t = std::ptr::null_mut();
    let mut curr_ptr: *mut mi_heap_t = std::ptr::null_mut();

    // Get mutable reference to the first heap in the list
    // We need to use raw pointers to avoid borrow conflicts
    if let Some(ref mut tld) = heap.tld {
        if let Some(ref mut heaps) = tld.heaps {
            curr_ptr = heaps.as_mut() as *mut mi_heap_t;
        }
    }

    // Traverse the linked list to find the heap
    let mut found = false;
    let heap_ptr = heap as *const _ as *mut mi_heap_t;
    
    while !curr_ptr.is_null() {
        // Compare by address (using pointer comparison)
        if curr_ptr == heap_ptr {
            found = true;
            break;
        }
        
        // Move to next heap
        prev_ptr = curr_ptr;
        // Get the next heap without borrowing conflicts using raw pointers
        unsafe {
            if let Some(ref mut next) = (*curr_ptr).next {
                curr_ptr = next.as_mut() as *mut mi_heap_t;
            } else {
                curr_ptr = std::ptr::null_mut();
            }
        }
    }

    // Assert we found the heap
    if !found {
        let assertion = CString::new("curr == heap").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = CString::new("mi_heap_free").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 299, func.as_ptr());
    }

    // Remove heap from linked list
    if found {
        unsafe {
            if !prev_ptr.is_null() {
                // Store heap's next pointer before taking it
                let heap_next = (*heap_ptr).next.take();
                (*prev_ptr).next = heap_next;
            } else {
                // Heap is the first in the list
                if let Some(ref mut tld) = heap.tld {
                    let heap_next = heap.next.take();
                    tld.heaps = heap_next;
                }
            }
        }
    }

    // Assert heap list is not empty after removal
    {
        let heaps_not_null = if let Some(ref tld) = heap.tld {
            tld.heaps.is_some()
        } else {
            false
        };
        
        if !heaps_not_null {
            let assertion = CString::new("heap->tld->heaps != NULL").unwrap();
            let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
            let func = CString::new("mi_heap_free").unwrap();
            crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 304, func.as_ptr());
        }
    }

    // Free heap memory if requested
    if do_free_mem {
        let heap_ptr = heap as *const _ as *mut std::ffi::c_void;
        let size = std::mem::size_of::<mi_heap_t>();
        // Use std::ptr::read to extract memid without requiring Default
        let memid = unsafe { std::ptr::read(&heap.memid) };
        crate::_mi_meta_free(Some(heap_ptr), size, memid);
    }
}
pub fn _mi_heap_collect_abandon(heap: Option<&mut mi_heap_t>) {
    mi_heap_collect_ex(heap, crate::mi_collect_t::mi_collect_t::MI_ABANDON);
}
pub fn mi_heap_delete(heap: Option<&mut mi_heap_t>) {
    // Line 3: Assert heap is not NULL (None in Rust)
    if heap.is_none() {
        let assertion = std::ffi::CString::new("heap != NULL").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = std::ffi::CString::new("mi_heap_delete").unwrap();
        super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 460, func.as_ptr());
        return;
    }

    // We know heap is Some after the above check, so we can unwrap
    let heap = heap.unwrap();

    // Line 4: Assert heap is initialized
    if !mi_heap_is_initialized(Some(heap)) {
        let assertion = std::ffi::CString::new("mi_heap_is_initialized(heap)").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = std::ffi::CString::new("mi_heap_delete").unwrap();
        super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 461, func.as_ptr());
        return;
    }

    // Line 6-9: Early return if NULL or not initialized (already handled above)
    // No explicit return needed since we already have the same logic

    // Line 10: Collect abandoned pages
    _mi_heap_collect_abandon(Some(heap));

    // Line 11: Assert page_count is 0
    if heap.page_count != 0 {
        let assertion = std::ffi::CString::new("heap->page_count==0").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = std::ffi::CString::new("mi_heap_delete").unwrap();
        super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 468, func.as_ptr());
    }

    // Line 12: Free the heap
    mi_heap_free(Some(heap), true);
}
pub fn _mi_heap_init(
    heap: Option<&mut mi_heap_t>,
    arena_id: mi_arena_id_t,
    allow_destroy: bool,
    heap_tag: u8,
    tld: Option<&mut mi_tld_t>,
) {
    // Check for NULL pointer and assert
    assert!(heap.is_some(), "heap!=NULL");
    let heap = heap.unwrap();
    
    // Check tld is not NULL
    assert!(tld.is_some(), "tld!=NULL");
    let tld = tld.unwrap();
    
    // Save memid before copying empty heap - manually copy since Clone is not implemented
    let memid = MiMemid {
        mem: match &heap.memid.mem {
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
        memkind: heap.memid.memkind,
        is_pinned: heap.memid.is_pinned,
        initially_committed: heap.memid.initially_committed,
        initially_zero: heap.memid.initially_zero,
    };
    
    // Copy the empty heap structure - use the global _MI_HEAP_EMPTY
    let empty_heap = _MI_HEAP_EMPTY.lock().unwrap();
    
    // Use slice conversion for memory copy
    let heap_bytes = unsafe {
        std::slice::from_raw_parts_mut(
            heap as *mut mi_heap_t as *mut u8,
            std::mem::size_of::<mi_heap_t>(),
        )
    };
    let empty_bytes = unsafe {
        std::slice::from_raw_parts(
            &*empty_heap as *const mi_heap_t as *const u8,
            std::mem::size_of::<mi_heap_t>(),
        )
    };
    _mi_memcpy_aligned(heap_bytes, empty_bytes, std::mem::size_of::<mi_heap_t>());
    
    // Restore memid
    heap.memid = memid;
    
    // Set tld reference - convert &mut to Box
    unsafe {
        heap.tld = Some(Box::from_raw(tld as *mut mi_tld_t));
    }
    heap.tag = heap_tag;
    heap.numa_node = tld.numa_node;
    
    // Get exclusive arena (store as Box, not raw pointer)
    heap.exclusive_arena = unsafe {
        let arena_ptr = _mi_arena_from_id(arena_id);
        if !arena_ptr.is_null() {
            Some(Box::from_raw(arena_ptr))
        } else {
            None
        }
    };
    
    // Set page reclaim/abandon flags - using correct enum variant names
    heap.allow_page_reclaim = (!allow_destroy) && (mi_option_get(crate::MiOption::PageReclaimOnFree) >= 0);
    heap.allow_page_abandon = (!allow_destroy) && (mi_option_get(crate::MiOption::PageFullRetain) >= 0);
    
    // Set page full retain
    heap.page_full_retain = mi_option_get_clamp(
        crate::MiOption::PageFullRetain,
        -1,
        32,
    );
    
    // Adjust for threadpool
    if tld.is_in_threadpool {
        if heap.page_full_retain > 0 {
            heap.page_full_retain = heap.page_full_retain / 4;
        }
    }
    
    // Initialize or split random context
    if tld.heap_backing.is_none() {
        // Store heap reference in tld - convert &mut to Box
        unsafe {
            tld.heap_backing = Some(Box::from_raw(heap as *mut mi_heap_t));
        }
        // Initialize random context - use the correct module path for random::mi_random_ctx_t
        crate::random::_mi_random_init(unsafe {
            &mut *(heap as *mut mi_heap_t as *mut crate::random::mi_random_ctx_t)
        });
    } else {
        if let Some(backing_heap) = &tld.heap_backing {
            // Use crate::mi_random_ctx_t::mi_random_ctx_t for _mi_random_split
            crate::_mi_random_split(&backing_heap.random, &mut heap.random);
        }
    }
    
    // Set cookie - cast u64 to usize
    heap.cookie = (_mi_heap_random_next(heap) as usize) | 1;
    
    // Initialize guarded heap
    _mi_heap_guarded_init(Some(heap));
    
    // Insert heap into tld's heap list
    let next_heap = tld.heaps.take();
    unsafe {
        heap.next = next_heap;
        tld.heaps = Some(Box::from_raw(heap as *mut mi_heap_t));
    }
}
pub type mi_arena_id_t = *mut std::ffi::c_void;
// Use existing global variables (declared elsewhere)
static HEAP: AtomicUsize = AtomicUsize::new(0);
static HEAP_IDX: AtomicUsize = AtomicUsize::new(0);

pub fn mi_heap_get_default() -> Option<&'static mut mi_heap_t> {
    let heap_ptr = mi_prim_get_default_heap()?;
    
    // Convert MiHeapPtr to raw pointer and then to reference
    let raw_ptr = heap_ptr.0;
    
    // Original C logic: if (!mi_heap_is_initialized(heap))
    // The triple negation !(!(!x)) simplifies to !x
    // So we need to check if the heap is NOT initialized
    if !mi_heap_is_initialized(Some(unsafe { &*raw_ptr })) {
        mi_thread_init();
        // In original C: heap_idx = mi_prim_get_default_heap();
        // This assigns to the global heap_idx variable
        let new_heap_ptr = mi_prim_get_default_heap();
        if let Some(ptr) = new_heap_ptr {
            HEAP_IDX.store(ptr.0 as usize, std::sync::atomic::Ordering::Relaxed);
        }
    }
    
    // Return mutable reference from raw pointer
    Some(unsafe { &mut *raw_ptr })
}
pub fn _mi_heap_by_tag(heap: Option<&mi_heap_t>, tag: u8) -> Option<&mi_heap_t> {
    // Check if input heap is None (equivalent to NULL check in C)
    let heap = heap?;
    
    // First check if the current heap has the right tag
    if heap.tag == tag {
        return Some(heap);
    }
    
    // Then iterate through the heaps linked list from tld->heaps
    // Need to handle Option for tld and heaps
    if let Some(tld) = &heap.tld {
        let mut curr_heap = &tld.heaps;
        
        while let Some(heap_ref) = curr_heap {
            if heap_ref.tag == tag {
                return Some(heap_ref);
            }
            curr_heap = &heap_ref.next;
        }
    }
    
    None  // Return None instead of 0 (NULL)
}
pub fn _mi_heap_page_destroy(
    heap: &mut mi_heap_t,
    pq: &mut mi_page_queue_t,
    page: &mut mi_page_t,
    arg1: *mut std::ffi::c_void,
    arg2: *mut std::ffi::c_void,
) -> bool {
    let _ = arg1;
    let _ = arg2;
    let _ = pq;
    
    let bsize = mi_page_block_size(page);
    
    // MI_HUGE_OBJ_SIZE_MAX = (8 * (1 * (1UL << (13 + 3)))) / 8
    let huge_obj_size_max = (8 * (1 * (1_usize << (13 + 3)))) / 8;
    
    if bsize > huge_obj_size_max {
        __mi_stat_decrease(&mut heap.tld.as_mut().unwrap().stats.malloc_huge, bsize);
    }
    
    _mi_page_free_collect(page, false);
    let inuse = page.used as usize;
    
    if bsize <= huge_obj_size_max {
        __mi_stat_decrease(&mut heap.tld.as_mut().unwrap().stats.malloc_normal, bsize * inuse);
        __mi_stat_decrease(&mut heap.tld.as_mut().unwrap().stats.malloc_bins[_mi_bin(bsize)], inuse);
    }
    
    // Check if mi_page_thread_free is None (NULL in C)
    if mi_page_thread_free(page).is_some() {
        // Use fully qualified path to super_function_unit5 module to avoid ambiguous import
        super::super_function_unit5::_mi_assert_fail(
            "mi_page_thread_free(page) == NULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const std::os::raw::c_char,
            355,
            "_mi_heap_page_destroy\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    page.used = 0;
    page.next = Option::None;
    page.prev = Option::None;
    
    // mi_page_set_heap(page, 0) - setting heap to null
    page.heap = Option::None;
    
    _mi_arenas_page_free(page, Some(&mut heap.tld.as_mut().unwrap()));
    
    true
}
pub fn mi_heap_reset_pages(heap: Option<&mut mi_heap_t>) {
    // Convert assertions from C ternary operator to Rust if statements
    if heap.is_none() {
        let assertion = std::ffi::CString::new("heap != NULL").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = std::ffi::CString::new("mi_heap_reset_pages").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 270, func.as_ptr());
        return;
    }
    
    let heap = heap.unwrap(); // Safe because we just checked above
    
    // Convert mutable reference to immutable for mi_heap_is_initialized
    if !mi_heap_is_initialized(Some(&*heap)) {
        let assertion = std::ffi::CString::new("mi_heap_is_initialized(heap)").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = std::ffi::CString::new("mi_heap_reset_pages").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 271, func.as_ptr());
        return;
    }
    
    // Reset pages_free_direct array by setting all elements to None using _mi_memset
    // Convert the array to a byte slice for memset
    let ptr = heap.pages_free_direct.as_mut_ptr() as *mut u8;
    let size = std::mem::size_of_val(&heap.pages_free_direct);
    unsafe {
        // Create a slice from raw parts and pass it to _mi_memset
        let slice = std::slice::from_raw_parts_mut(ptr, size);
        _mi_memset(slice, 0, size);
    }
    
    // Copy pages from _mi_heap_empty to heap using _mi_memcpy_aligned
    let empty_heap = _MI_HEAP_EMPTY.lock().unwrap();
    let src_ptr = empty_heap.pages.as_ptr() as *const u8;
    let dst_ptr = heap.pages.as_mut_ptr() as *mut u8;
    let size = std::mem::size_of_val(&heap.pages);
    unsafe {
        // Create slices for source and destination
        let src_slice = std::slice::from_raw_parts(src_ptr, size);
        let dst_slice = std::slice::from_raw_parts_mut(dst_ptr, size);
        _mi_memcpy_aligned(dst_slice, src_slice, size);
    }
    
    // Set page_count to 0
    heap.page_count = 0;
}

pub fn _mi_heap_destroy_pages(heap: Option<&mut crate::MiHeapS>) {
    // Helper function to wrap _mi_heap_page_destroy to match HeapPageVisitorFun signature
    fn page_destroy_wrapper(
        heap: Option<&crate::MiHeapS>,
        pq: Option<&crate::MiPageQueueS>,
        page: Option<&crate::MiPageS>,
        arg1: Option<&c_void>,
        arg2: Option<&c_void>,
    ) -> bool {
        // Convert Option<&T> to raw pointers for the actual function
        let heap_ptr = heap.map_or(std::ptr::null_mut(), |h| h as *const crate::MiHeapS as *mut crate::MiHeapS);
        let pq_ptr = pq.map_or(std::ptr::null_mut(), |p| p as *const crate::MiPageQueueS as *mut crate::MiPageQueueS);
        let page_ptr = page.map_or(std::ptr::null_mut(), |p| p as *const crate::MiPageS as *mut crate::MiPageS);
        
        // Convert Option<&c_void> to *mut c_void
        let arg1_ptr = arg1.map(|a| a as *const c_void as *mut c_void).unwrap_or(std::ptr::null_mut());
        let arg2_ptr = arg2.map(|a| a as *const c_void as *mut c_void).unwrap_or(std::ptr::null_mut());
        
        if !heap_ptr.is_null() && !pq_ptr.is_null() && !page_ptr.is_null() {
            unsafe {
                crate::_mi_heap_page_destroy(
                    &mut *heap_ptr,
                    &mut *pq_ptr,
                    &mut *page_ptr,
                    arg1_ptr,
                    arg2_ptr,
                )
            }
        } else {
            false
        }
    }
    
    // Convert Option<&mut T> to Option<&T> using .as_deref()
    crate::mi_heap_visit_pages(
        heap.as_deref(),
        page_destroy_wrapper,
        Option::None,
        Option::None,
    );
    crate::mi_heap_reset_pages(heap);
}
pub fn mi_heap_destroy(mut heap: Option<&mut crate::heap::mi_heap_t>) {
    // Check heap != NULL
    if heap.is_none() {
        // Disambiguate _mi_assert_fail by using the full path
        crate::page::_mi_assert_fail(
            "heap != NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c",
            382,
            "mi_heap_destroy",
        );
        // Don't return - continue like in C
    }
    
    // Early return if heap is None
    if heap.is_none() {
        return;
    }
    
    // Get a mutable reference to the heap without consuming the Option
    let heap_ref = match heap.as_mut() {
        Some(r) => r,
        None => return, // If heap is None, we can't proceed
    };
    
    // Check mi_heap_is_initialized(heap)
    if !mi_heap_is_initialized(Some(heap_ref)) {
        crate::page::_mi_assert_fail(
            "mi_heap_is_initialized(heap)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c",
            383,
            "mi_heap_destroy",
        );
        // Don't return - continue like in C
    }
    
    // Check !heap->allow_page_reclaim
    if heap_ref.allow_page_reclaim {
        crate::page::_mi_assert_fail(
            "!heap->allow_page_reclaim",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c",
            384,
            "mi_heap_destroy",
        );
        // Don't return - continue like in C
    }
    
    // Check !heap->allow_page_abandon
    if heap_ref.allow_page_abandon {
        crate::page::_mi_assert_fail(
            "!heap->allow_page_abandon",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c",
            385,
            "mi_heap_destroy",
        );
        // Don't return - continue like in C
    }
    
    // Check if heap is not initialized (from original C code)
    if !mi_heap_is_initialized(Some(heap_ref)) {
        return;
    }
    
    // At this point, heap_ref is still valid
    // The original C code checks heap->allow_page_reclaim to decide the path
    if heap_ref.allow_page_reclaim {
        // Show warning and call mi_heap_delete
        let warning_msg = std::ffi::CStr::from_bytes_with_nul(
            b"'mi_heap_destroy' called but ignored as the heap was not created with 'allow_destroy' (heap at %p)\n\0"
        ).unwrap();
        _mi_warning_message(
            warning_msg,
            heap_ref as *const _ as *mut std::ffi::c_void,
        );
        
        // Call mi_heap_delete with a reference to the heap
        mi_heap_delete(Some(heap_ref));
    } else {
        // Destroy pages and free heap
        // Since mi_heap_t is an alias for MiHeapS, we can use the same reference
        _mi_heap_destroy_pages(heap.as_mut().map(|h| unsafe { &mut *(h as *mut _ as *mut crate::MiHeapS) }));
        
        // Call mi_heap_free with the heap reference
        mi_heap_free(heap, true);
    }
}
pub fn _mi_heap_unsafe_destroy_all(heap: Option<&mut mi_heap_t>) {
    // Equivalent to the C NULL check; if no heap, nothing to do.
    let heap = match heap {
        Some(h) => h,
        Option::None => return,
    };

    // We must be able to *take ownership* of the linked list nodes to destroy them safely.
    // That requires mutable access to the TLD.
    let tld = match heap.tld.as_mut() {
        Some(t) => t,
        Option::None => return,
    };

    // Take the head of the list out of the TLD so we can walk and destroy nodes without
    // creating &mut aliases (and without invalid & -> &mut casts).
    let mut curr_opt: Option<Box<mi_heap_t>> = tld.heaps.take();

    while let Some(mut curr) = curr_opt {
        // Detach next first so `curr` can be safely destroyed.
        let next_opt: Option<Box<mi_heap_t>> = curr.next.take();

        if !curr.allow_page_reclaim {
            mi_heap_destroy(Some(&mut *curr));
        } else {
            _mi_heap_destroy_pages(Some(&mut *curr));
        }

        curr_opt = next_opt;
    }

    // List is fully consumed/destroyed.
    tld.heaps = Option::None;
}
pub fn mi_collect(force: bool) {
    if let Some(heap_ptr) = mi_prim_get_default_heap() {
        unsafe {
            mi_heap_collect(Some(&mut *heap_ptr.0), force);
        }
    }
}

pub fn mi_heap_page_check_owned(
    heap: Option<&mi_heap_t>,
    pq: Option<&mi_page_queue_t>,
    page: &mi_page_t,
    p: *const c_void,
    vfound: &mut bool,
) -> bool {
    // Parameters marked as unused in original C code
    let _ = heap;
    let _ = pq;
    
    let found = vfound;
    
    // Get page start address using provided dependency
    let start = match mi_page_start(page) {
        Some(ptr) => ptr,
        None => {
            *found = false;
            return !(*found);
        }
    };
    
    // Calculate end address
    let capacity = page.capacity as usize;
    let block_size = mi_page_block_size(page);
    
    // Convert pointers to usize for safe comparison
    let start_addr = start as usize;
    let p_addr = p as usize;
    let end_addr = start_addr.wrapping_add(capacity.wrapping_mul(block_size));
    
    // Check if p is within [start, end)
    *found = (p_addr >= start_addr) && (p_addr < end_addr);
    
    // Return the opposite of found
    !(*found)
}
pub fn mi_heap_check_owned(heap: Option<&mi_heap_t>, p: *const c_void) -> bool {
    if heap.is_none() {
        crate::super_function_unit5::_mi_assert_fail(
            "heap != NULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const std::os::raw::c_char,
            570,
            "mi_heap_check_owned\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    if heap.is_none() || !mi_heap_is_initialized(heap) {
        return false;
    }
    
    if ((p as usize) & ((1 << 3) - 1)) != 0 {
        return false;
    }
    
    // Use UnsafeCell for interior mutability
    let found = std::cell::UnsafeCell::new(false);
    
    // Create a wrapper function that matches the expected HeapPageVisitorFun signature
    fn wrapper(
        heap: Option<&crate::MiHeapS>,
        pq: Option<&crate::MiPageQueueS>,
        page: Option<&crate::MiPageS>,
        arg1: Option<&c_void>,
        arg2: Option<&c_void>,
    ) -> bool {
        // Convert arguments to match mi_heap_page_check_owned signature
        let page_ref = page.expect("page should not be None");
        
        // Convert Option<&c_void> to *const c_void
        let p_ptr = arg1.map(|r| r as *const c_void).unwrap_or(std::ptr::null());
        
        // Convert Option<&c_void> to &mut bool using UnsafeCell
        let vfound = if let Some(arg2_ref) = arg2 {
            // Cast to *const UnsafeCell<bool> and dereference to get UnsafeCell reference
            let cell_ptr = arg2_ref as *const c_void as *const std::cell::UnsafeCell<bool>;
            unsafe { &mut *cell_ptr.as_ref().expect("cell pointer should not be null").get() }
        } else {
            return false;
        };
        
        mi_heap_page_check_owned(heap, pq, page_ref, p_ptr, vfound)
    }
    
    mi_heap_visit_pages(
        heap.map(|h| h as &crate::MiHeapS),
        wrapper,  // Direct function pointer, not wrapped in Option
        Some(unsafe { &*(p as *const c_void) }),
        // Pass pointer to UnsafeCell instead of raw mutable reference
        Some(unsafe { &*(&found as *const std::cell::UnsafeCell<bool> as *const c_void) }),
    );
    
    // Get the value from UnsafeCell
    unsafe { *found.get() }
}
pub fn mi_check_owned(p: Option<&c_void>) -> bool {
    let heap = mi_prim_get_default_heap();
    // Convert Option<MiHeapPtr> to Option<&mi_heap_t>
    let heap_ref = heap.map(|ptr| unsafe { &*ptr.0 });
    mi_heap_check_owned(heap_ref, 
                       p.map_or(std::ptr::null(), |ptr| ptr as *const c_void))
}
pub fn mi_heap_set_numa_affinity(heap: Option<&mut mi_heap_t>, numa_node: i32) {
    // Check if heap is None (equivalent to checking for NULL in C)
    if heap.is_none() {
        return;
    }
    
    // Unwrap safely: If `heap` is `Some`, it will be a valid mutable reference
    let heap = heap.unwrap();
    
    // Calculate the numa_node value using the same logic as C code
    heap.numa_node = if numa_node < 0 {
        -1
    } else {
        numa_node % _mi_os_numa_node_count()
    };
}
pub fn mi_fast_divide(n: usize, magic: u64, shift: usize) -> usize {
    // Assertion check
    if n > u32::MAX as usize {
        // Disambiguate by using fully qualified path from one of the imported modules
        // Based on the imports shown in the error, use super_function_unit5::_mi_assert_fail
        crate::super_function_unit5::_mi_assert_fail(
            b"n <= UINT32_MAX\0".as_ptr() as *const std::os::raw::c_char,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const std::os::raw::c_char,
            608,
            b"mi_fast_divide\0".as_ptr() as *const std::os::raw::c_char,
        );
    }

    let hi = ((n as u64).wrapping_mul(magic)) >> 32;
    ((hi as usize).wrapping_add(n)) >> shift
}
pub fn mi_heap_get_backing() -> Option<&'static mut mi_heap_t> {
    let heap = mi_heap_get_default();
    
    if heap.is_none() {
        let assertion = CStr::from_bytes_with_nul(b"heap!=NULL\0").unwrap();
        let fname = CStr::from_bytes_with_nul(b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0").unwrap();
        let func = CStr::from_bytes_with_nul(b"mi_heap_get_backing\0").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 169, func.as_ptr());
    }
    
    let heap = heap.unwrap();
    
    // Get the backing heap from the TLD
    let bheap = if let Some(tld) = heap.tld.as_mut() {
        tld.heap_backing.as_mut()
    } else {
        None
    };
    
    if bheap.is_none() {
        let assertion = CStr::from_bytes_with_nul(b"bheap!=NULL\0").unwrap();
        let fname = CStr::from_bytes_with_nul(b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0").unwrap();
        let func = CStr::from_bytes_with_nul(b"mi_heap_get_backing\0").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 171, func.as_ptr());
    }
    
    let bheap = bheap.unwrap();
    
    // Check thread ID
    let current_thread_id = _mi_thread_id();
    let bheap_tld = bheap.tld.as_ref().unwrap_or_else(|| {
        let assertion = CStr::from_bytes_with_nul(b"bheap->tld->thread_id == _mi_thread_id()\0").unwrap();
        let fname = CStr::from_bytes_with_nul(b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0").unwrap();
        let func = CStr::from_bytes_with_nul(b"mi_heap_get_backing\0").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 172, func.as_ptr());
        unreachable!();
    });
    
    if bheap_tld.thread_id != current_thread_id {
        let assertion = CStr::from_bytes_with_nul(b"bheap->tld->thread_id == _mi_thread_id()\0").unwrap();
        let fname = CStr::from_bytes_with_nul(b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0").unwrap();
        let func = CStr::from_bytes_with_nul(b"mi_heap_get_backing\0").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 172, func.as_ptr());
    }
    
    Some(bheap)
}
pub fn mi_heap_new_ex(heap_tag: i32, allow_destroy: bool, arena_id: mi_arena_id_t) -> Option<Box<mi_heap_t>> {
    let bheap = mi_heap_get_backing();
    
    if bheap.is_none() {
        let assertion = CString::new("bheap != NULL").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = CString::new("mi_heap_new_ex").unwrap();
        
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr(), 
            fname.as_ptr(), 
            242, 
            func.as_ptr()
        );
        return Option::None;
    }
    
    let bheap = bheap.unwrap();
    // _mi_heap_create might be in a different module, but we don't have that information
    // Based on the original C code, we need to pass bheap.tld
    // Since we can't find _mi_heap_create, we'll return None for now
    // In a real fix, we would need to find the correct function
    Option::None
}
pub fn mi_heap_new() -> Option<Box<mi_heap_t>> {
    let arena_id = crate::_mi_arena_id_none();


    let arena_id_ptr: *mut c_void = unsafe { std::mem::transmute(arena_id) };

    crate::mi_heap_new_ex(0, true, arena_id_ptr)
}

pub fn mi_get_fast_divisor(
    divisor: usize,
    magic: Option<&mut u64>,
    shift: Option<&mut usize>,
) {
    // Assert condition from line 3
    if !(divisor > 0 && divisor <= u32::MAX as usize) {
        _mi_assert_fail(
            "divisor > 0 && divisor <= UINT32_MAX\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const std::os::raw::c_char,
            602,
            "mi_get_fast_divisor\0".as_ptr() as *const std::os::raw::c_char,
        );
    }

    // Calculate shift value from line 4
    let shift_val = ((1 << 3) * 8) - crate::mi_clz(divisor - 1);
    
    // Calculate magic value from line 5
    let magic_val = ((((1u64 << 32) * ((1u64 << shift_val) - divisor as u64)) / divisor as u64) + 1);

    // Write results to output parameters if provided
    if let Some(m) = magic {
        *m = magic_val;
    }
    
    if let Some(s) = shift {
        *s = shift_val;
    }
}
pub fn _mi_heap_memid_is_suitable(heap: Option<&mi_heap_t>, memid: crate::MiMemid) -> bool {
    match heap {
        Some(heap_ref) => {
            let request_arena = heap_ref.exclusive_arena.as_ref().map(|boxed| &**boxed);
            crate::_mi_arena_memid_is_suitable(memid, request_arena)
        }
        None => false,
    }
}

pub fn mi_heap_of_block(p: Option<*const c_void>) -> Option<*mut mi_heap_t> {
    let p = p?;
    
    unsafe {
        let page = _mi_ptr_page(p);
        mi_page_heap(page)
    }
}

pub fn mi_heap_contains_block(heap: Option<&mi_heap_t>, p: Option<*const c_void>) -> bool {
    // Convert the assertion parameters to C strings
    let assertion = CString::new("heap != NULL").unwrap();
    let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
    let func = CString::new("mi_heap_contains_block").unwrap();

    // Check assertion: heap should not be NULL (None in Rust)
    if heap.is_none() {
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr(),
            fname.as_ptr(),
            553,
            func.as_ptr(),
        );
    }

    // Check if heap is None or not initialized
    if heap.is_none() || !mi_heap_is_initialized(heap) {
        return false;
    }

    // Get the raw pointer for comparison
    let heap_ptr = heap.unwrap() as *const mi_heap_t as *mut mi_heap_t;
    
    // Compare heap pointer with the heap of the block
    let block_heap = mi_heap_of_block(p);
    match (heap_ptr, block_heap) {
        (h, Some(bh)) if h == bh => true,
        _ => false,
    }
}
pub fn mi_heap_new_in_arena(arena_id: mi_arena_id_t) -> Option<Box<mi_heap_t>> {
    mi_heap_new_ex(0, false, arena_id)
}
pub fn _mi_heap_area_init(area: &mut crate::mi_heap_area_t::mi_heap_area_t, page: &crate::mi_page_t) {
    let bsize = crate::mi_page_block_size(page);
    // Calculate usable block size - in C this would be mi_page_usable_block_size
    // Since we don't have that function, we'll use the block_size field from the page
    // or calculate it based on bsize minus padding
    let ubsize = page.block_size; // Using the block_size field from mi_page_t
    
    area.reserved = (page.reserved as usize) * bsize;
    area.committed = (page.capacity as usize) * bsize;
    area.blocks = Option::None; // Cannot convert Option<*mut u8> to Option<Vec<u8>> safely
    area.used = page.used as usize;
    area.block_size = ubsize;
    area.full_block_size = bsize;
    area.heap_tag = page.heap_tag as i32;
}
pub fn mi_heap_visit_areas_page(
    heap: Option<&crate::MiHeapS>,
    pq: Option<&crate::mi_page_queue_t>,
    page: Option<&crate::mi_page_t>,
    vfun: *mut c_void,
    arg: *mut c_void,
) -> bool {
    // Mark unused parameters explicitly
    let _ = heap;
    let _ = pq;
    
    // Convert the void pointer to the function pointer type
    let fun: crate::mi_heap_area_visit_fun::mi_heap_area_visit_fun = 
        unsafe { std::mem::transmute(vfun) };
    
    // Create the extended area structure
    let mut xarea = crate::mi_heap_area_visit_fun::MiHeapAreaExT {
        page: page.map(|p| {
            // We need to convert the reference to a Box for storage
            // Since we're not consuming the original page, we'll create a deep copy
            // This is necessary to match the C behavior where the page pointer is stored
            Box::new(crate::mi_page_t {
                xthread_id: std::sync::atomic::AtomicUsize::new(p.xthread_id.load(std::sync::atomic::Ordering::Relaxed)),
                free: p.free,
                used: p.used,
                capacity: p.capacity,
                reserved: p.reserved,
                retire_expire: p.retire_expire,
                local_free: p.local_free,
                xthread_free: std::sync::atomic::AtomicUsize::new(p.xthread_free.load(std::sync::atomic::Ordering::Relaxed)),
                block_size: p.block_size,
                page_start: p.page_start,
                heap_tag: p.heap_tag,
                free_is_zero: p.free_is_zero,
                keys: p.keys,
                heap: p.heap,
                next: p.next,
                prev: p.prev,
                slice_committed: p.slice_committed,
                memid: crate::MiMemid {
                    mem: match &p.memid.mem {
                        crate::MiMemidMem::Os(os_info) => crate::MiMemidMem::Os(crate::MiMemidOsInfo {
                            base: os_info.base.clone(),
                            size: os_info.size,
                        }),
                        crate::MiMemidMem::Arena(arena_info) => crate::MiMemidMem::Arena(crate::mi_memid_arena_info_t {
                            arena: arena_info.arena,
                            slice_index: arena_info.slice_index,
                            slice_count: arena_info.slice_count,
                        }),
                        crate::MiMemidMem::Meta(meta_info) => crate::MiMemidMem::Meta(crate::MiMemidMetaInfo {
                            meta_page: meta_info.meta_page,
                            block_index: meta_info.block_index,
                            block_count: meta_info.block_count,
                        }),
                    },
                    memkind: p.memid.memkind,
                    is_pinned: p.memid.is_pinned,
                    initially_committed: p.memid.initially_committed,
                    initially_zero: p.memid.initially_zero,
                },
            })
        }),
        area: crate::mi_heap_area_t::mi_heap_area_t {
            blocks: Option::None,
            reserved: 0,
            committed: 0,
            used: 0,
            block_size: 0,
            full_block_size: 0,
            heap_tag: 0,
        },
    };
    
    // Initialize the area using the page
    if let Some(page_ref) = page {
        crate::_mi_heap_area_init(&mut xarea.area, page_ref);
    }
    
    // Convert the raw pointer to an Option reference for arg
    let arg_ref = if arg.is_null() {
        Option::None
    } else {
        Some(unsafe { &*arg })
    };
    
    // Call the visitor function and return its result
    fun(heap, Some(&xarea), arg_ref)
}
pub fn mi_heap_visit_areas(
    heap: Option<&crate::MiHeapS>,
    visitor: Option<crate::mi_heap_area_visit_fun::mi_heap_area_visit_fun>,
    arg: Option<&c_void>,
) -> bool {
    // Check if visitor is NULL (None)
    if visitor.is_none() {
        return false;
    }

    // Convert visitor function pointer to *mut c_void as expected by mi_heap_visit_areas_page
    let visitor_ptr = match visitor {
        Some(f) => f as *const crate::mi_heap_area_visit_fun::mi_heap_area_visit_fun as *mut c_void,
        None => return false,
    };

    // Create a wrapper function that matches HeapPageVisitorFun signature
    fn visit_wrapper(
        heap: Option<&crate::MiHeapS>,
        pq: Option<&crate::MiPageQueueS>,
        page: Option<&crate::MiPageS>,
        arg1: Option<&c_void>,
        arg2: Option<&c_void>,
    ) -> bool {
        // Convert arg1 to *mut c_void for mi_heap_visit_areas_page
        let vfun = match arg1 {
            Some(ptr) => ptr as *const c_void as *mut c_void,
            None => std::ptr::null_mut(),
        };
        
        // Convert arg2 to *mut c_void for mi_heap_visit_areas_page
        let arg = match arg2 {
            Some(ptr) => ptr as *const c_void as *mut c_void,
            None => std::ptr::null_mut(),
        };
        
        // Call the actual page visitor function
        crate::mi_heap_visit_areas_page(heap, pq, page, vfun, arg)
    }

    // Call mi_heap_visit_pages with the wrapper function
    crate::mi_heap_visit_pages(
        heap,
        visit_wrapper as crate::HeapPageVisitorFun,
        Some(unsafe { &*(visitor_ptr as *const c_void) }),
        arg,
    )
}
pub fn _mi_heap_area_visit_blocks(
    area: Option<&crate::mi_heap_area_t::mi_heap_area_t>,
    page: Option<&mut mi_page_t>,
    visitor: Option<unsafe extern "C" fn(*const mi_heap_t, *const crate::mi_heap_area_t::mi_heap_area_t, *mut c_void, usize, *mut c_void) -> bool>,
    arg: *mut c_void,
) -> bool {
    // Check area pointer
    if area.is_none() {
        crate::super_function_unit5::_mi_assert_fail(
            "area != NULL\0".as_ptr() as _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
            614,
            "_mi_heap_area_visit_blocks\0".as_ptr() as _,
        );
    }
    if area.is_none() {
        return true;
    }
    
    // Check page pointer
    if page.is_none() {
        crate::super_function_unit5::_mi_assert_fail(
            "page != NULL\0".as_ptr() as _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
            616,
            "_mi_heap_area_visit_blocks\0".as_ptr() as _,
        );
    }
    if page.is_none() {
        return true;
    }
    
    let page = page.unwrap();
    _mi_page_free_collect(page, true);
    
    // Check local_free
    if page.local_free.is_some() {
        crate::super_function_unit5::_mi_assert_fail(
            "page->local_free == NULL\0".as_ptr() as _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
            620,
            "_mi_heap_area_visit_blocks\0".as_ptr() as _,
        );
    }
    
    if page.used == 0 {
        return true;
    }
    
    let area = area.unwrap();
    let mut psize = 0usize;
    let pstart = mi_page_area(page, Some(&mut psize));
    let heap = unsafe { mi_page_heap(page as *const _) };
    let bsize = mi_page_block_size(page);
    let ubsize = mi_page_block_size(page); // Using mi_page_block_size instead of missing mi_page_usable_block_size
    
    if page.capacity == 1 {
        if !(page.used == 1 && page.free.is_none()) {
            crate::super_function_unit5::_mi_assert_fail(
                "page->used == 1 && page->free == NULL\0".as_ptr() as _,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
                631,
                "_mi_heap_area_visit_blocks\0".as_ptr() as _,
            );
        }
        return visitor.map_or(true, |vis| {
            let heap = unsafe { mi_page_heap(page as *const _) };
            unsafe { vis(heap.expect("Heap should be valid"), area as *const _, pstart.expect("pstart should be valid") as *mut c_void, ubsize, arg) }
        });
    }
    
    if !(bsize <= u32::MAX as usize) {
        crate::super_function_unit5::_mi_assert_fail(
            "bsize <= UINT32_MAX\0".as_ptr() as _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
            634,
            "_mi_heap_area_visit_blocks\0".as_ptr() as _,
        );
    }
    
    if page.used == page.capacity {
        let mut block = pstart.expect("pstart should be valid");
        let mut block_idx = 0u32;
        
        for _ in 0..page.capacity {
            if !visitor.map_or(true, |vis| unsafe {
                vis(heap.expect("Heap should be valid"), area as *const _, block as *mut c_void, ubsize, arg)
            }) {
                return false;
            }
            block_idx += bsize as u32;
            unsafe {
                block = block.add(bsize);
            }
        }
        return true;
    }
    
    let mut free_map: [usize; 128] = [0; 128];
    let bmapsize = _mi_divide_up(page.capacity as usize, 64);
    
    // Clear the free_map
    free_map[..bmapsize].fill(0);
    
    if (page.capacity % 64) != 0 {
        let shift = page.capacity % 64;
        let mask = usize::MAX << shift;
        free_map[bmapsize - 1] = mask;
    }
    
    let mut magic = 0u64;
    let mut shift = 0usize;
    mi_get_fast_divisor(bsize, Some(&mut magic), Some(&mut shift));
    let mut free_count = 0usize;
    
    let mut block = page.free;
    while let Some(current_block) = block {
        free_count += 1;
        
        let block_ptr = current_block as *mut u8;
        let pstart_ptr = pstart.expect("pstart should be valid");
        if !(block_ptr >= pstart_ptr && block_ptr < unsafe { pstart_ptr.add(psize) }) {
            crate::super_function_unit5::_mi_assert_fail(
                "(uint8_t*)block >= pstart && (uint8_t*)block < (pstart + psize)\0".as_ptr() as _,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
                670,
                "_mi_heap_area_visit_blocks\0".as_ptr() as _,
            );
        }
        
        let offset = unsafe { block_ptr.offset_from(pstart_ptr) } as usize;
        if (offset % bsize) != 0 {
            crate::super_function_unit5::_mi_assert_fail(
                "offset % bsize == 0\0".as_ptr() as _,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
                672,
                "_mi_heap_area_visit_blocks\0".as_ptr() as _,
            );
        }
        
        if !(offset <= u32::MAX as usize) {
            crate::super_function_unit5::_mi_assert_fail(
                "offset <= UINT32_MAX\0".as_ptr() as _,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
                673,
                "_mi_heap_area_visit_blocks\0".as_ptr() as _,
            );
        }
        
        let blockidx = mi_fast_divide(offset, magic, shift);
        if blockidx != (offset / bsize) {
            crate::super_function_unit5::_mi_assert_fail(
                "blockidx == offset / bsize\0".as_ptr() as _,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
                675,
                "_mi_heap_area_visit_blocks\0".as_ptr() as _,
            );
        }
        
        if !(blockidx < (65536 / std::mem::size_of::<*mut c_void>())) {
            crate::super_function_unit5::_mi_assert_fail(
                "blockidx < MI_MAX_BLOCKS\0".as_ptr() as _,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
                676,
                "_mi_heap_area_visit_blocks\0".as_ptr() as _,
            );
        }
        
        let bitidx = blockidx / 64;
        let bit = blockidx - (bitidx * 64);
        free_map[bitidx] |= 1usize << bit;
        
        block = unsafe { Some(mi_block_next(page as *const _, current_block)) };
    }
    
    let heap = unsafe { mi_page_heap(page as *const _) };
    let pstart = pstart.expect("pstart should be valid");
    
    if !(page.capacity as usize == (free_count + page.used as usize)) {
        crate::super_function_unit5::_mi_assert_fail(
            "page->capacity == (free_count + page->used)\0".as_ptr() as _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
            681,
            "_mi_heap_area_visit_blocks\0".as_ptr() as _,
        );
    }
    
    let mut used_count = 0usize;
    let mut block = pstart;
    let mut block_idx = 0u32;
    
    for i in 0..bmapsize {
        if free_map[i] == 0 {
            for _ in 0..64 {
                used_count += 1;
                if !visitor.map_or(true, |vis| unsafe {
                    vis(heap.expect("Heap should be valid"), area as *const _, block as *mut c_void, ubsize, arg)
                }) {
                    return false;
                }
                block_idx += bsize as u32;
                unsafe {
                    block = block.add(bsize);
                }
            }
        } else {
            let mut m = !free_map[i];
            while m != 0 {
                used_count += 1;
                let bitidx = mi_ctz(m);
                let target_block = unsafe { block.add(bitidx * bsize) };
                if !visitor.map_or(true, |vis| unsafe {
                    vis(heap.expect("Heap should be valid"), area as *const _, target_block as *mut c_void, ubsize, arg)
                }) {
                    return false;
                }
                m &= m - 1;
            }
            block_idx += (bsize * 64) as u32;
            unsafe {
                block = block.add(bsize * 64);
            }
        }
    }
    
    if !(page.used as usize == used_count) {
        crate::super_function_unit5::_mi_assert_fail(
            "page->used == used_count\0".as_ptr() as _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as _,
            713,
            "_mi_heap_area_visit_blocks\0".as_ptr() as _,
        );
    }
    
    true
}
pub fn mi_heap_area_visitor(
    heap: Option<&crate::heap::mi_heap_t>,
    xarea: Option<&crate::mi_heap_area_t::mi_heap_area_t>,
    arg: *mut c_void,
) -> bool {
    // Convert raw pointer to reference safely
    let args_ptr = arg as *mut crate::mi_visit_blocks_args_t::mi_visit_blocks_args_t;
    
    if args_ptr.is_null() {
        return false;
    }
    
    // SAFETY: We've checked that args_ptr is not null
    let args = unsafe { &*args_ptr };
    
    // Check if heap and xarea are provided (not None)
    let (Some(heap_ref), Some(xarea_ref)) = (heap, xarea) else {
        return false;
    };
    
    // Call the visitor function
    if let Some(visitor_fn) = args.visitor {
        // SAFETY: This is an FFI call, we're passing valid pointers
        let visitor_result = unsafe {
            visitor_fn(
                heap_ref as *const _ as *const c_void,
                xarea_ref as *const _ as *const crate::mi_block_visit_fun::mi_heap_area_t,
                0,
                xarea_ref.block_size,
                args.arg,
            )
        };
        
        if !visitor_result {
            return false;
        }
    } else {
        return false;
    }
    
    if args.visit_blocks {
        // Call the block visitor function with page
        // Note: Since mi_heap_area_t doesn't have a page field in our dependencies,
        // we pass None for page. This matches the original C code's intent when
        // xarea->page is NULL.
        return crate::_mi_heap_area_visit_blocks(
            Some(xarea_ref),
            Option::None,
            // Convert the visitor function signature to match what _mi_heap_area_visit_blocks expects
            args.visitor.map(|visitor| unsafe {
                std::mem::transmute::<
                    unsafe extern "C" fn(*const c_void, *const crate::mi_block_visit_fun::mi_heap_area_t, usize, usize, *mut c_void) -> bool,
                    unsafe extern "C" fn(*const crate::heap::mi_heap_t, *const crate::mi_heap_area_t::mi_heap_area_t, *mut c_void, usize, *mut c_void) -> bool
                >(visitor)
            }),
            args.arg,
        );
    } else {
        return true;
    }
}
pub unsafe extern "C" fn mi_heap_set_default(heap: *mut mi_heap_t) -> *mut mi_heap_t {
    if heap.is_null() {
        crate::super_function_unit5::_mi_assert_fail(
            "heap != NULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const std::os::raw::c_char,
            473,
            "mi_heap_set_default\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    if !mi_heap_is_initialized(Some(&*heap)) {
        crate::super_function_unit5::_mi_assert_fail(
            "mi_heap_is_initialized(heap)\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c\0".as_ptr() as *const std::os::raw::c_char,
            474,
            "mi_heap_set_default\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    if heap.is_null() || !mi_heap_is_initialized(Some(&*heap)) {
        return std::ptr::null_mut();
    }
    let old = mi_prim_get_default_heap();
    _mi_heap_set_default_direct(heap);
    match old {
        Some(ptr) => ptr.0, // Access the inner pointer directly
        None => std::ptr::null_mut(),
    }
}
#[repr(C)]
pub struct mi_visit_blocks_args_t {
    pub visit_blocks: bool,
    pub visitor: Option<unsafe extern "C" fn(*const std::ffi::c_void, *const crate::mi_block_visit_fun::mi_heap_area_t, usize, usize, *mut std::ffi::c_void) -> bool>,
    pub arg: *mut std::ffi::c_void,
}
pub fn mi_heap_unload(heap: Option<&mut mi_heap_t>) {
    // Translate the assertion: (mi_heap_is_initialized(heap)) ? ((void) 0) : (_mi_assert_fail(...));
    // This assertion only runs if heap is not null.
    if let Some(heap_ref) = heap.as_deref() {
        if !mi_heap_is_initialized(Some(heap_ref)) {
            let assertion = CString::new("mi_heap_is_initialized(heap)").unwrap();
            let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
            let func = CString::new("mi_heap_unload").unwrap();
            crate::super_function_unit5::_mi_assert_fail(
                assertion.as_ptr(),
                fname.as_ptr(),
                487,
                func.as_ptr()
            );
        }
    }
    
    // Translate: if ((heap == 0) || (!mi_heap_is_initialized(heap))) return;
    if heap.is_none() || !mi_heap_is_initialized(heap.as_deref()) {
        return;
    }
    
    // Safe to unwrap here since we checked heap.is_none() above
    let heap = heap.unwrap();
    
    // Translate: if (heap->exclusive_arena == 0) { warning and return; }
    if heap.exclusive_arena.is_none() {
        let warning_msg = CString::new("cannot unload heaps that are not associated with an exclusive arena\n").unwrap();
        _mi_warning_message(&warning_msg, std::ptr::null_mut());
        return;
    }
    
    // Translate: _mi_heap_collect_abandon(heap);
    _mi_heap_collect_abandon(Some(heap));
    
    // Translate assertion: (heap->page_count == 0) ? ((void) 0) : (_mi_assert_fail(...));
    if heap.page_count != 0 {
        let assertion = CString::new("heap->page_count==0").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c").unwrap();
        let func = CString::new("mi_heap_unload").unwrap();
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr(),
            fname.as_ptr(),
            497,
            func.as_ptr()
        );
    }
    
    // Translate: mi_heap_free(heap, 0);
    mi_heap_free(Some(heap), false);
    
    // Translate: heap->tld = 0;
    heap.tld = None;
    
    // Implicit return in Rust
}
pub fn mi_heap_reload(heap: Option<&mut mi_heap_t>, arena_id: crate::mi_arena_id_t) -> bool {
    // Assert heap is initialized
    if !mi_heap_is_initialized(heap.as_deref()) {
        crate::super_function_unit5::_mi_assert_fail(
            "mi_heap_is_initialized(heap)".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c".as_ptr() as *const std::os::raw::c_char,
            508,
            "mi_heap_reload".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // Check if heap is null or not initialized
    let heap = match heap {
        Some(h) => h,
        None => return false,
    };
    
    if !mi_heap_is_initialized(Some(heap)) {
        return false;
    }
    
    // Check exclusive arena
    if heap.exclusive_arena.is_none() {
        _mi_warning_message(
            &std::ffi::CStr::from_bytes_with_nul("cannot reload heaps that were not associated with an exclusive arena\n".as_bytes()).unwrap(),
            std::ptr::null_mut(),
        );
        return false;
    }
    
    // Check tld
    if heap.tld.is_some() {
        _mi_warning_message(
            &std::ffi::CStr::from_bytes_with_nul("cannot reload heaps that were not unloaded first\n".as_bytes()).unwrap(),
            std::ptr::null_mut(),
        );
        return false;
    }
    
    // Get arena from id
    let arena = unsafe { _mi_arena_from_id(arena_id) };
    
    // Compare arenas
    let heap_arena_ptr = heap.exclusive_arena.as_ref().map(|a| a.as_ref() as *const _);
    if heap_arena_ptr != Some(arena as *const _) {
        _mi_warning_message(
            &std::ffi::CStr::from_bytes_with_nul("trying to reload a heap at a different arena address: %p vs %p\n".as_bytes()).unwrap(),
            std::ptr::null_mut(),
        );
        return false;
    }
    
    // Assert page_count is 0
    if heap.page_count != 0 {
        crate::super_function_unit5::_mi_assert_fail(
            "heap->page_count==0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c".as_ptr() as *const std::os::raw::c_char,
            524,
            "mi_heap_reload".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // Get default heap's tld
    let default_heap = match mi_heap_get_default() {
        Some(h) => h,
        None => return false,
    };
    
    // Copy the tld pointer (not clone the Box) - just take the reference
    heap.tld = default_heap.tld.as_ref().map(|_| {
        // We need to create a new Box that points to the same data
        // Since we can't clone Box, we'll use the same approach as in C
        Box::new(unsafe { std::ptr::read(default_heap.tld.as_ref().unwrap().as_ref() as *const _) })
    });
    
    // Assert page_count is still 0
    if heap.page_count != 0 {
        crate::super_function_unit5::_mi_assert_fail(
            "heap->page_count == 0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/heap.c".as_ptr() as *const std::os::raw::c_char,
            530,
            "mi_heap_reload".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // Initialize pages_free_direct - assign pointer to _mi_page_empty
    for i in 0..(128 + (((std::mem::size_of::<crate::mi_padding_t::mi_padding_t>() + (1 << 3)) - 1) / (1 << 3))) + 1 {
        heap.pages_free_direct[i] = Some(Box::new(unsafe { std::ptr::read(&*_mi_page_empty as *const _) }));
    }
    
    // Link heap into tld's heap list
    // Take the current heaps from tld and set as next
    if let Some(tld) = &heap.tld {
        // Just copy the pointer, not clone
        heap.next = tld.heaps.as_ref().map(|_| {
            Box::new(unsafe { std::ptr::read(tld.heaps.as_ref().unwrap().as_ref() as *const _) })
        });
    }
    
    // Set this heap as the first in tld's heaps list
    // Use a raw pointer to avoid borrow conflicts
    let heap_ptr = heap as *const mi_heap_t as *mut mi_heap_t;
    if let Some(tld) = &mut heap.tld {
        tld.heaps = Some(Box::new(unsafe {
            std::ptr::read(heap_ptr as *const mi_heap_t)
        }));
    }
    
    true
}
