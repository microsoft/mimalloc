use crate::*;

pub unsafe extern "C" fn _mi_page_malloc_zero(
    heap: *mut crate::super_special_unit0::mi_heap_t,
    page: *mut crate::super_special_unit0::mi_page_t,
    size: usize,
    zero: bool,
) -> *mut std::ffi::c_void {
    // Assertions
    if (*page).block_size != 0 {
        let page_block_size = (*page).block_size;
        if !(page_block_size >= size) {
            crate::super_function_unit5::_mi_assert_fail(
                b"mi_page_block_size(page) >= size\0".as_ptr() as *const _,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc.c\0".as_ptr() as *const _,
                34,
                b"_mi_page_malloc_zero\0".as_ptr() as *const _,
            );
        }

        if !crate::_mi_is_aligned(Some(&mut *(page as *mut std::ffi::c_void)), 1 << (13 + 3)) {
            crate::super_function_unit5::_mi_assert_fail(
                b"_mi_is_aligned(page, MI_PAGE_ALIGN)\0".as_ptr() as *const _,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc.c\0".as_ptr() as *const _,
                35,
                b"_mi_page_malloc_zero\0".as_ptr() as *const _,
            );
        }

        if !(crate::_mi_ptr_page(page as *const std::ffi::c_void) == page as *mut _) {
            crate::super_function_unit5::_mi_assert_fail(
                b"_mi_ptr_page(page)==page\0".as_ptr() as *const _,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc.c\0".as_ptr() as *const _,
                36,
                b"_mi_page_malloc_zero\0".as_ptr() as *const _,
            );
        }
    }

    // Get free block or fallback
    let block: *mut crate::mi_block_t::MiBlock = match (*page).free {
        Some(p) => p,
        None => return _mi_malloc_generic(heap, size, zero, 0),
    };

    // Assertion
    if !(!block.is_null() && crate::_mi_ptr_page(block as *const std::ffi::c_void) == page as *mut _) {
        crate::super_function_unit5::_mi_assert_fail(
            b"block != NULL && _mi_ptr_page(block) == page\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc.c\0".as_ptr() as *const _,
            44,
            b"_mi_page_malloc_zero\0".as_ptr() as *const _,
        );
    }

    // Update page metadata and check assertions
    (*page).free = Some(crate::mi_block_next(page as *const _, block as *const _));
    (*page).used = (*page).used.wrapping_add(1);

    if let Some(new_free) = (*page).free {
        if !(crate::_mi_ptr_page(new_free as *const std::ffi::c_void) == page as *mut _) {
            crate::super_function_unit5::_mi_assert_fail(
                b"page->free == NULL || _mi_ptr_page(page->free) == page\0".as_ptr() as *const _,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc.c\0".as_ptr() as *const _,
                49,
                b"_mi_page_malloc_zero\0".as_ptr() as *const _,
            );
        }
    }

    if !((*page).block_size < 16
        || crate::_mi_is_aligned(Some(&mut *(block as *mut std::ffi::c_void)), 16))
    {
        crate::super_function_unit5::_mi_assert_fail(
            b"page->block_size < MI_MAX_ALIGN_SIZE || _mi_is_aligned(block, MI_MAX_ALIGN_SIZE)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc.c\0".as_ptr() as *const _,
            50,
            b"_mi_page_malloc_zero\0".as_ptr() as *const _,
        );
    }

    // Compute usable block size without relying on inaccessible helpers
    let padding_sz = core::mem::size_of::<crate::mi_padding_t::mi_padding_t>();
    let bsize = (*page).block_size.saturating_sub(padding_sz);

    // Zero initialization if requested
    if zero {
        if (*page).free_is_zero {
            (*block).next = 0;
        } else {
            crate::_mi_memzero_aligned(core::slice::from_raw_parts_mut(block as *mut u8, bsize), bsize);
        }
    }

    let large_threshold = 1usize << (13 + 3); // 65536

    // Debug fill for non-zero, non-huge pages
    if !zero && bsize <= large_threshold {
        core::slice::from_raw_parts_mut(block as *mut u8, bsize).fill(0xD0);
    }

    // Statistics for small blocks
    if bsize <= large_threshold {
        let tld = (*heap).tld.as_mut().unwrap();
        crate::__mi_stat_increase(&mut tld.stats.malloc_normal, bsize);
        crate::__mi_stat_counter_increase(&mut tld.stats.malloc_normal_count, 1);
        let bin = crate::_mi_bin(bsize);
        crate::__mi_stat_increase(&mut tld.stats.malloc_bins[bin], 1);

        let req_size = size.saturating_sub(padding_sz);
        crate::__mi_stat_increase(&mut tld.stats.malloc_requested, req_size);
    }

    // Padding setup
    let padding = (block as *mut u8).add(bsize) as *mut crate::mi_padding_t::mi_padding_t;
    let req_size = size.saturating_sub(padding_sz);
    let delta = (padding as *mut u8).offset_from(block as *mut u8) as isize - (req_size as isize);

    if !(delta >= 0 && bsize >= req_size.saturating_add(delta as usize)) {
        crate::super_function_unit5::_mi_assert_fail(
            b"delta >= 0 && bsize >= (size - MI_PADDING_SIZE + delta)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc.c\0".as_ptr() as *const _,
            99,
            b"_mi_page_malloc_zero\0".as_ptr() as *const _,
        );
    }

    (*padding).canary = crate::mi_ptr_encode_canary(Option::None, Option::None, &(*page).keys);
    (*padding).delta = delta as u32;

    // Debug fill for padding on non-huge pages
    if bsize <= large_threshold {
        let fill = (padding as *mut u8).offset(-delta);
        let maxpad = if delta > 16 { 16usize } else { delta as usize };
        for i in 0..maxpad {
            *fill.add(i) = 0xDE;
        }
    }

    block as *mut std::ffi::c_void
}

pub unsafe extern "C" fn _mi_malloc_generic(
    mut heap: *mut crate::super_special_unit0::mi_heap_t,
    size: usize,
    zero: bool,
    huge_alignment: usize,
) -> *mut std::ffi::c_void {
    // Assertion - heap != NULL
    if heap.is_null() {
        crate::super_function_unit5::_mi_assert_fail(
            b"heap != NULL\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const _,
            942,
            b"_mi_malloc_generic\0".as_ptr() as *const _,
        );
    }

    // Check if heap is initialized
    if !crate::mi_heap_is_initialized(Some(&*heap)) {
        let default_heap = crate::mi_heap_get_default();
        if default_heap.is_none() {
            return core::ptr::null_mut();
        }
        heap = default_heap.unwrap() as *mut crate::super_special_unit0::mi_heap_t;
    }

    // Assertion - heap is initialized
    if !crate::mi_heap_is_initialized(Some(&*heap)) {
        crate::super_function_unit5::_mi_assert_fail(
            b"mi_heap_is_initialized(heap)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const _,
            949,
            b"_mi_malloc_generic\0".as_ptr() as *const _,
        );
    }

    // Generic count handling and deferred collection
    {
        let heap_ref = &mut *heap;
        heap_ref.generic_count += 1;

        if heap_ref.generic_count >= 1000 {
            heap_ref.generic_collect_count += heap_ref.generic_count;
            heap_ref.generic_count = 0;

            crate::_mi_deferred_free(Some(heap_ref), false);

            let generic_collect = crate::mi_option_get_clamp(crate::MiOption::GenericCollect, 1, 1_000_000);
            if heap_ref.generic_collect_count >= generic_collect as i64 {
                heap_ref.generic_collect_count = 0;
                crate::mi_heap_collect(Some(heap_ref), false);
            }
        }
    }

    // Find a page for allocation
    let mut page = crate::mi_find_page(&mut *heap, size, huge_alignment);
    if page.is_none() {
        crate::mi_heap_collect(Some(&mut *heap), true);
        page = crate::mi_find_page(&mut *heap, size, huge_alignment);
    }

    let page_ptr = match page {
        Some(p) => p,
        None => {
            crate::alloc::_mi_error_message(12, b"unable to allocate memory\0".as_ptr() as *const _);
            return core::ptr::null_mut();
        }
    };

    // Assertions about the page
    if !crate::mi_page_immediate_available(Some(&*page_ptr)) {
        crate::super_function_unit5::_mi_assert_fail(
            b"mi_page_immediate_available(page)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const _,
            979,
            b"_mi_malloc_generic\0".as_ptr() as *const _,
        );
    }

    let page_block_size = (*page_ptr).block_size;
    if !(page_block_size >= size) {
        crate::super_function_unit5::_mi_assert_fail(
            b"mi_page_block_size(page) >= size\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const _,
            980,
            b"_mi_malloc_generic\0".as_ptr() as *const _,
        );
    }

    if !crate::_mi_is_aligned(Some(&mut *(page_ptr as *mut std::ffi::c_void)), 1 << (13 + 3)) {
        crate::super_function_unit5::_mi_assert_fail(
            b"_mi_is_aligned(page, MI_PAGE_ALIGN)\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const _,
            981,
            b"_mi_malloc_generic\0".as_ptr() as *const _,
        );
    }

    let ptr_page = crate::_mi_ptr_page(page_ptr as *const std::ffi::c_void);
    if !(ptr_page == page_ptr) {
        crate::super_function_unit5::_mi_assert_fail(
            b"_mi_ptr_page(page)==page\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const _,
            982,
            b"_mi_malloc_generic\0".as_ptr() as *const _,
        );
    }

    // Allocate from the page
    let p = _mi_page_malloc_zero(heap, page_ptr, size, zero);

    // Assertion - allocation succeeded
    if p.is_null() {
        crate::super_function_unit5::_mi_assert_fail(
            b"p != NULL\0".as_ptr() as *const _,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const _,
            986,
            b"_mi_malloc_generic\0".as_ptr() as *const _,
        );
    }

    // Update page state if full (avoid casting &T -> &mut T; borrow the queue mutably from the heap)
    if crate::mi_page_is_full(&*page_ptr) {
        let page_mut = &mut *page_ptr;
        let heap_mut = &mut *heap;

        // Best-effort: mirror typical mimalloc logic (queue index derived from bin)
        let bin = crate::_mi_bin(size);
        let idx = if bin < heap_mut.pages.len() { bin } else { heap_mut.pages.len() - 1 };
        let pq_mut: &mut crate::super_special_unit0::mi_page_queue_t = &mut heap_mut.pages[idx];

        crate::mi_page_to_full(page_mut, pq_mut);
    }

    p
}

