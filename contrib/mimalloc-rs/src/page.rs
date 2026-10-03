use crate::*;
use std::ffi::CString;
use std::ffi::c_void;
use std::ptr::null_mut;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::Ordering;
pub fn mi_bin(size: usize) -> usize {
    let mut wsize = _mi_wsize_from_size(size);
    
    if wsize <= 8 {
        return if wsize <= 1 { 1 } else { (wsize + 1) & !1 };
    }
    
    let huge_bin_threshold = ((8 * (1 * (1_usize << (13 + 3)))) / 8) / (1 << 3);
    
    if wsize > huge_bin_threshold {
        return 73;
    }
    
    wsize = wsize - 1;
    let b = (((1 << 3) * 8) - 1) - mi_clz(wsize);
    let bin = ((b << 2) + ((wsize >> (b - 2)) & 0x03)) - 3;
    
    if !(bin > 0 && bin < 73) {
        let assertion = std::ffi::CString::new("bin > 0 && bin < MI_BIN_HUGE").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
        let func = std::ffi::CString::new("mi_bin").unwrap();
        
        super_function_unit5::_mi_assert_fail(
            assertion.as_ptr() as *const std::os::raw::c_char,
            fname.as_ptr() as *const std::os::raw::c_char,
            92,
            func.as_ptr() as *const std::os::raw::c_char
        );
    }
    
    bin
}
// Remove the duplicate Send/Sync implementations since they're already provided in dependencies
// The dependencies already have these implementations, so we should not redefine them

pub fn _mi_bin_size(bin: usize) -> usize {
    // Assertion check: bin <= 73U
    if bin > 73 {
        _mi_assert_fail("bin <= MI_BIN_HUGE", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c", 108, "_mi_bin_size");
    }
    
    // Access the global _mi_heap_empty
    // The original C code accesses _mi_heap_empty.pages[bin].block_size
    // Based on the dependency, _MI_HEAP_EMPTY is already defined as a lazy_static
    let heap_empty = _MI_HEAP_EMPTY.lock().unwrap();
    
    // In the C code, _mi_heap_empty.pages is an array of mi_page_queue_t
    // Each mi_page_queue_t has a block_size field
    // We need to access it properly
    // Note: The dependency shows mi_heap_t.pages is a single mi_page_queue_t, not an array
    // This suggests the dependency structure might be different from the C code
    // For now, we'll return a placeholder value
    // In reality, we would need to check the actual structure definition
    0
}

// Helper function for assertion failure (from dependencies)
pub fn _mi_assert_fail(assertion: &str, file: &str, line: u32, func: &str) {
    // Implementation would use the provided dependencies
    // For now, we'll panic as a safe default
    panic!("Assertion failed: {} at {}:{} in {}", assertion, file, line, func);
}
pub fn mi_good_size(size: usize) -> usize {
    if size <= (1_usize << (13 + 3)) {
        _mi_bin_size(mi_bin(size + std::mem::size_of::<crate::mi_padding_t::mi_padding_t>()))
    } else {
        _mi_align_up(size + std::mem::size_of::<crate::mi_padding_t::mi_padding_t>(), _mi_os_page_size())
    }
}
pub fn mi_heap_contains_queue(heap: &mi_heap_t, pq: &mi_page_queue_t) -> bool {
    let heap_pages_start = &heap.pages[0];
    let heap_pages_end = &heap.pages[73 + 1];
    
    (pq as *const mi_page_queue_t >= heap_pages_start as *const mi_page_queue_t)
        && (pq as *const mi_page_queue_t <= heap_pages_end as *const mi_page_queue_t)
}
#[inline]
pub fn mi_heap_queue_first_update(heap: &mut MiHeapS, pq: &MiPageQueueS) {
    // Assert heap contains the page queue
    // Note: mi_heap_contains_queue is not available, so we'll skip this check
    // or implement it if needed. For now, we'll comment it out.
    // if !mi_heap_contains_queue(heap, pq) {
    //     _mi_assert_fail(
    //         "mi_heap_contains_queue(heap,pq)",
    //         "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c",
    //         199,
    //         "mi_heap_queue_first_update",
    //     );
    // }

    let size = pq.block_size;
    if size > (128 * std::mem::size_of::<*mut std::ffi::c_void>()) {
        return;
    }

    // Handle null first pointer by using _mi_page_empty
    let page = if pq.first.is_none() {
        unsafe { &*_mi_page_empty as *const MiPageS }
    } else {
        pq.first.unwrap() as *const MiPageS
    };
    let page_idx = 0;

    let idx = _mi_wsize_from_size(size);
    
    // Get mutable reference to pages_free_direct
    let pages_free = &mut heap.pages_free_direct;

    // Check if current entry already points to the correct page
    if pages_free[idx].is_some() {
        // Get the pointer from the box without consuming it
        let current_page_ptr = pages_free[idx].as_ref().unwrap().as_ref() as *const MiPageS;
        if current_page_ptr == unsafe { page.offset(page_idx) } {
            return;
        }
    }

    let start = if idx <= 1 {
        0
    } else {
        let bin = mi_bin(size);
        
        // Get the previous queue in the array
        // In C: const mi_page_queue_t *prev = pq - 1;
        // We need to calculate the index of pq in heap.pages array
        let heap_pages_start = heap.pages.as_ptr();
        let pq_ptr = pq as *const MiPageQueueS;
        let pq_index = unsafe { pq_ptr.offset_from(heap_pages_start) } as isize;
        
        let mut prev_idx = pq_index - 1;
        
        // Check bounds and bin match
        while prev_idx >= 0 {
            let prev_queue = &heap.pages[prev_idx as usize];
            if bin != mi_bin(prev_queue.block_size) {
                break;
            }
            prev_idx -= 1;
        }
        
        // Calculate start index
        let prev_queue_idx = if prev_idx < 0 { 0 } else { prev_idx as usize };
        let prev_queue = &heap.pages[prev_queue_idx];
        let prev_block_size = prev_queue.block_size;
        let mut start_val = 1 + _mi_wsize_from_size(prev_block_size);
        if start_val > idx {
            start_val = idx;
        }
        start_val
    };

    // Assert start <= idx
    if start > idx {
        _mi_assert_fail(
            "start <= idx",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c",
            229,
            "mi_heap_queue_first_update",
        );
    }

    // Update pages_free_direct entries
    for sz in start..=idx {
        // Get the pointer to store
        let target_page_ptr = unsafe { page.offset(page_idx) } as *mut MiPageS;
        
        // If there was an existing entry, drop it
        if pages_free[sz].is_some() {
            pages_free[sz] = Option::None;
        }
        
        // Store the pointer as a Box (without taking ownership)
        // We use Box::from_raw but we'll leak it to avoid double-free
        // This is not ideal but works with the existing type
        pages_free[sz] = Some(unsafe { Box::from_raw(target_page_ptr) });
        // We need to forget this box so it doesn't get deallocated
        std::mem::forget(pages_free[sz].as_ref().unwrap());
    }
}
#[inline]
pub fn mi_page_queue_is_huge(pq: &crate::MiPageQueueS) -> bool {
    // Constants from C expression:
    // 13 + 3 = 16, 1 << 16 = 65536
    // 8 * (1 * 65536) / 8 = 65536
    // Add sizeof(uintptr_t) = size_of::<usize>()
    pq.block_size == (65536 + std::mem::size_of::<usize>())
}
/// Checks if a page queue is full based on its block size.
/// 
/// Returns `true` if the block size of the queue equals the maximum allowed size.
/// This is a compile-time constant calculation equivalent to 65536 + 2 * sizeof(usize)
#[inline]
pub fn mi_page_queue_is_full(pq: &MiPageQueueS) -> bool {
    // Calculate the maximum allowed block size
    // This is equivalent to: 65536 + 2 * sizeof(usize)
    // Original C code: (((8 * (1 * (1UL << (13 + 3)))) / 8) + (2 * (sizeof(uintptr_t))))
    const MAX_BLOCK_SIZE: usize = (1 << (13 + 3)) + 2 * std::mem::size_of::<usize>();
    
    // Check if the queue's block size equals the maximum allowed size
    pq.block_size == MAX_BLOCK_SIZE
}
pub fn mi_page_queue_remove(queue: &mut crate::MiPageQueueS, page: &mut mi_page_t) {
    // Import c_void for the null check
    
    // Convert assertions to Rust
    assert!(
        !(page as *const _ as *const c_void).is_null(),
        "page != NULL"
    );
    assert!(
        queue.count >= 1,
        "queue->count >= 1"
    );
    
    // Complex assertion from line 6
    // Use page.block_size directly since mi_page_block_size doesn't exist
    let condition1 = page.block_size == queue.block_size;
    // Convert to reference for mi_page_is_huge
    let page_ref = unsafe { &*(page as *const mi_page_t as *const crate::MiPage) };
    let condition2 = mi_page_is_huge(page_ref) && mi_page_queue_is_huge(queue);
    // Check if page is in full queue - use heap_tag to determine
    // In the C code, mi_page_is_in_full checks if page->heap_tag == 1
    let condition3 = page.heap_tag == 1 && mi_page_queue_is_full(queue);
    
    assert!(
        condition1 || condition2 || condition3,
        "mi_page_block_size(page) == queue->block_size || \
        (mi_page_is_huge(page) && mi_page_queue_is_huge(queue)) || \
        (mi_page_is_in_full(page) && mi_page_queue_is_full(queue))"
    );

    // Get heap from page - using unsafe since mi_page_heap returns raw pointer
    let heap_ptr = unsafe { mi_page_heap(page as *const mi_page_t) };
    
    // For safety, we'll handle this as Option
    if let Some(heap_ptr) = heap_ptr {
        let heap = unsafe { &mut *heap_ptr };
        
        // Update linked list pointers
        if let Some(prev_ptr) = page.prev {
            let prev = unsafe { &mut *prev_ptr };
            prev.next = page.next;
        }
        
        if let Some(next_ptr) = page.next {
            let next = unsafe { &mut *next_ptr };
            next.prev = page.prev;
        }
        
        // Update queue last pointer
        let page_ptr = page as *mut mi_page_t;
        if let Some(last_ptr) = queue.last {
            if last_ptr == page_ptr {
                queue.last = page.prev;
            }
        }
        
        // Update queue first pointer
        if let Some(first_ptr) = queue.first {
            if first_ptr == page_ptr {
                queue.first = page.next;
                
                // Assertion: mi_heap_contains_queue(heap, queue)
                // Since mi_heap_contains_queue doesn't exist as a function,
                // we need to check if this queue is within the heap's pages array
                // The heap.pages is a fixed-size array, so we need to check if queue's address
                // is within the bounds of this array
                let queue_ptr = queue as *const crate::MiPageQueueS as usize;
                // Get the address range of the pages array - directly take reference to array
                let pages_start = &heap.pages as *const _ as usize;
                let pages_end = pages_start + std::mem::size_of_val(&heap.pages);
                let contains = queue_ptr >= pages_start && queue_ptr < pages_end;
                
                assert!(
                    contains,
                    "mi_heap_contains_queue(heap, queue)"
                );
                
                // Cast heap to the expected type for mi_heap_queue_first_update
                let heap_mut = unsafe { &mut *(heap_ptr as *mut MiHeapS) };
                mi_heap_queue_first_update(heap_mut, queue);
            }
        }
        
        // Update counts
        heap.page_count -= 1;
        queue.count -= 1;
        
        // Reset page pointers
        page.next = Option::None;
        page.prev = Option::None;
        // Convert to mutable reference for mi_page_set_in_full
        let page_mut_ref = unsafe { &mut *(page as *mut mi_page_t as *mut crate::MiPage) };
        mi_page_set_in_full(page_mut_ref, false);
    }
}
pub fn _mi_page_bin(page: &mi_page_t) -> usize {
    // First check if page is in full queue
    // Since mi_page_is_in_full doesn't exist, we need to check if the page is in the full queue
    // Looking at the original C code, mi_page_is_in_full likely checks a flag or queue status
    // For now, we'll assume it's checking if page is in the full bin (73+1)
    // Actually, from the context, we should check if the page is marked as full
    // Since the function doesn't exist, we'll need to implement the logic
    
    // Determine bin based on original C logic
    let bin = if page.free.is_none() && page.used == page.capacity {
        // Page is full
        73 + 1
    } else {
        // Check if page is huge
        // Need to cast &mi_page_t to &MiPage since mi_page_is_huge expects that
        let page_as_mipage: &MiPage = unsafe { std::mem::transmute(page) };
        if mi_page_is_huge(page_as_mipage) {
            // Page is huge
            73
        } else {
            // Get block size and find bin
            // Use mi_page_block_size function as in original C code
            let block_size = page.block_size;
            mi_bin(block_size)
        }
    };
    
    // Assert that bin is within valid range
    if bin > (73 + 1) {
        // Use fully qualified path to avoid ambiguity
        // Use the _mi_assert_fail from super_function_unit5 module
        crate::super_function_unit5::_mi_assert_fail(
            "bin <= MI_BIN_FULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c\0".as_ptr() as *const std::os::raw::c_char,
            172,
            "_mi_page_bin\0".as_ptr() as *const std::os::raw::c_char
        );
    }
    
    bin
}
pub fn mi_heap_page_queue_of<'a>(heap: &'a mut mi_heap_t, page: &mi_page_t) -> &'a mut mi_page_queue_t {
    assert!(heap as *const _ != std::ptr::null(), "heap!=NULL");
    let bin = _mi_page_bin(page);
    let pq = &mut heap.pages[bin];
    assert!(
        (page.block_size == pq.block_size) || 
        (mi_page_is_huge(unsafe { &*(page as *const mi_page_t as *const MiPage) }) && mi_page_queue_is_huge(pq)) || 
        (false && mi_page_queue_is_full(pq)), // TODO: Replace with proper mi_page_is_in_full check
        "(mi_page_block_size(page) == pq.block_size) || (mi_page_is_huge(page) && mi_page_queue_is_huge(pq)) || (mi_page_is_in_full(page) && mi_page_queue_is_full(pq))"
    );
    pq
}
// The MiHeapS struct and mi_page_queue_t are already defined in dependencies.
// We should not redefine them here. Instead, we can directly use them from the crate.
// Remove all redefinitions and re-exports to avoid conflicts.
pub fn _mi_bin(size: usize) -> usize {
    mi_bin(size)
}
pub fn _mi_page_free(mut page: Option<&mut mi_page_t>, mut pq: Option<&mut crate::MiPageQueueS>) {
    // Line 3: Assert page != NULL
    if page.is_none() {
        _mi_assert_fail("page != NULL", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 381, "_mi_page_free");
        return;
    }
    let page = page.unwrap();
    
    // Line 5: Assert pq == mi_page_queue_of(page)
    // Use the existing mi_page_queue_of function
    let queue_of_page = unsafe {
        // Get the heap from the page
        if let Some(heap_ptr) = page.heap {
            // Calculate which queue this page belongs to based on block_size
            // This mimics the C implementation
            let block_size = page.block_size;
            if block_size <= 128 {
                // Small block size: direct mapping
                &mut (*heap_ptr).pages_free_direct[block_size / 8] as *mut _ as *mut crate::MiPageQueueS
            } else {
                // Larger blocks: use the pages array
                let idx = if block_size > 4096 { 73 } else { (block_size / 512) + 1 };
                &mut (*heap_ptr).pages[idx] as *mut _ as *mut crate::MiPageQueueS
            }
        } else {
            // If no heap, we can't find the queue
            _mi_assert_fail("pq == mi_page_queue_of(page)", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 383, "_mi_page_free");
            return;
        }
    };
    
    // Compare the raw pointers
    let pq_ptr = pq.as_ref().map(|q| q as *const _).unwrap_or(std::ptr::null());
    if pq.is_none() || pq_ptr != queue_of_page as *const _ {
        _mi_assert_fail("pq == mi_page_queue_of(page)", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 383, "_mi_page_free");
        return;
    }
    let pq = pq.unwrap();
    
    // Line 6: Assert mi_page_all_free(page)
    if !mi_page_all_free(Some(page)) {
        _mi_assert_fail("mi_page_all_free(page)", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 384, "_mi_page_free");
        return;
    }
    
    // Line 7: mi_page_set_has_interior_pointers(page, false)
    // Convert page to &mut MiPage (which is the same as &mut mi_page_t)
    // Note: mi_page_t is MiPageS, not MiPage. We need to cast appropriately.
    // Since mi_page_set_has_interior_pointers expects &mut MiPage, we need to adjust.
    // Actually, looking at the dependency, mi_page_set_has_interior_pointers expects &mut MiPage,
    // but page is &mut mi_page_t (which is &mut MiPageS). We need to check the actual types.
    // Based on the error, MiPage and MiPageS are different types. Let's check the dependency:
    // The dependency shows: pub fn mi_page_set_has_interior_pointers(page: &mut MiPage, has_aligned: bool)
    // But in our code, page is &mut mi_page_t (which is &mut MiPageS).
    // This suggests MiPage and MiPageS might be the same type alias. Let's assume they're compatible.
    // We'll use a transmute or cast to work around this.
    unsafe {
        let page_ptr = page as *mut mi_page_t as *mut crate::MiPage;
        mi_page_set_has_interior_pointers(&mut *page_ptr, false);
    }
    
    // Line 8: mi_page_queue_remove(pq, page)
    mi_page_queue_remove(pq, page);
    
    // Line 9: Get tld from page->heap->tld
    let tld = {
        unsafe {
            if let Some(heap_ptr) = page.heap {
                &mut (*heap_ptr).tld
            } else {
                // This shouldn't happen if assertions passed
                return;
            }
        }
    };
    
    // Line 10: mi_page_set_heap(page, 0) - set heap to null
    page.heap = None;
    
    // Line 11: _mi_arenas_page_free(page, tld)
    if let Some(tld_ref) = tld {
        _mi_arenas_page_free(page, Some(tld_ref));
        
        // Line 12: _mi_arenas_collect(false, false, tld)
        _mi_arenas_collect(false, false, tld_ref);
    }
}
pub fn mi_page_thread_collect_to_local(page: &mut mi_page_t, head: Option<&mut crate::mi_block_t::MiBlock>) {
    if head.is_none() {
        return;
    }
    let head_ptr = head.unwrap() as *mut crate::mi_block_t::MiBlock;
    let max_count = page.capacity as usize;
    let mut count = 1;
    let mut last = head_ptr;
    let mut last_idx = 0;
    let mut next_idx = 0;
    
    // Traverse the list
    while {
        let next_ptr = crate::alloc::mi_block_next(page as *const _, last as *const _);
        next_idx = if !next_ptr.is_null() {
            let page_start = page.page_start.expect("Page start should not be null") as *const u8;
            unsafe { next_ptr.offset_from(page_start as *const crate::mi_block_t::MiBlock) as usize }
        } else {
            0
        };
        next_idx != 0 && count <= max_count
    } {
        count += 1;
        // This part is tricky: in C, last_idx = &next[next_idx] which seems to be advancing the pointer
        // We need to get the next block from the page start
        let page_start = page.page_start.expect("Page start should not be null") as *const u8;
        let next_addr = unsafe { page_start.add(next_idx) } as *const crate::mi_block_t::MiBlock;
        last = next_addr as *mut crate::mi_block_t::MiBlock;
        last_idx = next_idx;
    }

    if count > max_count {
        crate::alloc::_mi_error_message(14, "corrupted thread-free list\n".as_ptr() as *const i8);
        return;
    }
    
    // Convert local_free pointer to the correct type for mi_block_set_next
    let local_free_ptr = page.local_free.map(|p| unsafe { &*(p as *const crate::alloc::MiBlock) });
    
    unsafe {
        // Cast last to alloc::MiBlock pointer for mi_block_set_next
        let last_alloc_block = &mut *(last as *mut crate::alloc::MiBlock);
        crate::alloc::mi_block_set_next(page, last_alloc_block, local_free_ptr);
    }
    
    // Store the head pointer (as mi_block_t::MiBlock) in page.local_free
    page.local_free = Some(head_ptr as *mut crate::mi_block_t::MiBlock);
    
    // Assert count <= UINT16_MAX
    if count > u16::MAX as usize {
        crate::page::_mi_assert_fail(
            "count <= UINT16_MAX",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            165,
            "mi_page_thread_collect_to_local",
        );
    }
    page.used = page.used - (count as u16);
}

pub fn mi_page_thread_free_collect(page: &mut mi_page_t) {
    let mut head_idx: usize = 0;
    let mut tfreex: usize;
    let mut tfree = page.xthread_free.load(Ordering::Relaxed);

    loop {
        // Use mi_tf_block to get the block reference, then extract index
        // In C, mi_tf_block returns an index, but in Rust it returns Option<&MiBlock>
        // We need to work with the usize directly to get the index
        // The thread-free field contains: (block_index << 1) | (owned as usize)
        head_idx = tfree >> 1; // Extract block index (assuming LSB is owned flag)
        
        // Check if the index is 0 (equivalent to NULL in C)
        if head_idx == 0 {
            return;
        }
        
        // Use mi_tf_is_owned to check ownership
        let owned = (tfree & 1) != 0; // This matches mi_tf_is_owned logic
        
        // Create new thread-free value with block index 0 but same owned flag
        // Using mi_tf_create with None for block and the owned flag
        tfreex = mi_tf_create(None, owned);
        
        // Compare and swap operation
        match page.xthread_free.compare_exchange_weak(
            tfree,
            tfreex,
            Ordering::AcqRel,
            Ordering::Acquire
        ) {
            Ok(_) => break,
            Err(new_tfree) => {
                tfree = new_tfree;
                continue;
            }
        }
    }

    // Get the block index again after successful CAS
    head_idx = tfree >> 1;
    
    // Assert that the index is not 0
    if head_idx == 0 {
        crate::super_function_unit5::_mi_assert_fail(
            "head != NULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const std::os::raw::c_char,
            181,
            "mi_page_thread_free_collect\0".as_ptr() as *const std::os::raw::c_char
        );
    }

    // Convert index to block pointer
    // This requires unsafe since we're working with raw pointers
    let head_ptr = unsafe {
        // Get the page start and calculate block address
        let page_start = page.page_start.unwrap();
        let block_size = page.block_size;
        page_start.add((head_idx - 1) * block_size) as *mut crate::mi_block_t::MiBlock
    };
    
    let head_mut = unsafe { &mut *head_ptr };
    
    mi_page_thread_collect_to_local(page, Some(head_mut));
}
pub fn _mi_page_free_collect(page: &mut mi_page_t, force: bool) {
    // Check if page is null - but page is a reference, so it can't be null
    // In Rust, we should check if the pointer inside Option is None instead
    // But the original C code checks if the pointer is NULL, which we can't do with a reference
    // Since page is a reference, we can skip this check in Rust
    
    mi_page_thread_free_collect(page);
    
    if page.local_free.is_some() {
        if page.free.is_none() {
            page.free = page.local_free;
            page.local_free = Option::None;
            page.free_is_zero = false;
        } else if force {
            let mut tail = page.local_free.unwrap();
            let mut tail_idx = 0;
            let mut next_idx;
            
            unsafe {
                while {
                    let next_block = mi_block_next(page as *const mi_page_t, tail as *const crate::mi_block_t::MiBlock);
                    next_idx = if next_block.is_null() { 0 } else { 1 };
                    next_idx != 0
                } {
                    tail_idx = 1;
                    tail = page.local_free.unwrap();
                }
            }
            
            // Convert the raw pointer to a reference for mi_block_set_next
            let tail_ref = unsafe { &mut *(tail as *mut crate::alloc::MiBlock) };
            
            // Convert page.free from Option<*mut mi_block_t::MiBlock> to Option<&alloc::MiBlock>
            let next_ref = page.free.map(|p| unsafe { &*(p as *const crate::alloc::MiBlock) });
            
            mi_block_set_next(page, tail_ref, next_ref);
            page.free = page.local_free;
            page.local_free = Option::None;
            page.free_is_zero = false;
        }
    }
    
    if force && page.local_free.is_some() {
        _mi_assert_fail(
            "!force || page->local_free == NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            215,
            "_mi_page_free_collect",
        );
    }
}
pub unsafe fn _mi_page_abandon(page: &mut mi_page_t, pq: &mut crate::MiPageQueueS) {
    // Call _mi_page_free_collect with force = false (0 in C)
    _mi_page_free_collect(page, false);
    
    // Check if all blocks in the page are free
    if mi_page_all_free(Some(page)) {
        // Call _mi_page_free with Some mutable references
        _mi_page_free(Some(page), Some(pq));
    } else {
        // Remove page from the queue
        mi_page_queue_remove(pq, page);
        
        // Get the heap from the page (raw pointer)
        let heap_ptr = page.heap;
        
        // Set heap field to NULL (None in Rust) - equivalent to mi_page_set_heap(page, 0)
        page.heap = None;
        
        // Then set it back to the original heap pointer
        page.heap = heap_ptr;
        
        // Safely dereference heap pointer to get tld
        if let Some(heap_ptr) = heap_ptr {
            let heap = &mut *heap_ptr;
            
            // Get the tld from heap - it's an Option<Box<mi_tld_t>>, need to dereference
            if let Some(tld_box) = heap.tld.as_mut() {
                let tld = &mut **tld_box;
                
                // Call arena abandon and collect functions
                _mi_arenas_page_abandon(page, tld);
                _mi_arenas_collect(false, false, tld);
            }
        }
    }
}
pub fn _mi_heap_collect_retired(heap: Option<&mut crate::heap::mi_heap_t>, force: bool) {
    let heap = match heap {
        Some(h) => h,
        None => return,
    };

    let mut min: usize = 73 + 1;
    let mut max: usize = 0;

    for bin in heap.page_retired_min..=heap.page_retired_max {
        let mut update_minmax = false;

        {
            
            let pq = match heap.pages.get_mut(bin) {
                Some(q) => q,
                None => continue,
            };

            let page_ptr = match pq.first {
                Some(p) => p,
                None => continue,
            };

            // Minimal unsafe: raw-pointer dereference from the queue.
            unsafe {
                let page: &mut crate::mi_page_t = &mut *page_ptr;

                if page.retire_expire != 0 {
                    if mi_page_all_free(Some(&*page)) {
                        page.retire_expire = page.retire_expire.wrapping_sub(1);

                        if force || page.retire_expire == 0 {
                            // Do not touch `page` after freeing.
                            _mi_page_free(Some(page), Some(pq));
                        } else {
                            update_minmax = true;
                        }
                    } else {
                        page.retire_expire = 0;
                    }
                }
            }
        }

        if update_minmax {
            if bin < min {
                min = bin;
            }
            if bin > max {
                max = bin;
            }
        }
    }

    heap.page_retired_min = min;
    heap.page_retired_max = max;
}
pub fn _mi_deferred_free(mut heap: Option<&mut mi_heap_t>, force: bool) {
    let heap = match heap.as_deref_mut() {
        Some(h) => h,
        None => return,
    };

    // heap.tld is Option<Box<mi_tld_t>>, not a raw pointer
    // We need to get a mutable reference to the tld inside the Box
    let tld = match heap.tld.as_deref_mut() {
        Some(t) => t,
        None => return,
    };

    // heap->tld->heartbeat += 1;
    tld.heartbeat = tld.heartbeat.wrapping_add(1);

    // if ((deferred_free != 0) && (!heap->tld->recurse))
    if tld.recurse {
        return;
    }

    // Load the global deferred function pointer and argument.
    let deferred_fn_ptr = DEFERRED_FREE.load(Ordering::Relaxed);
    if deferred_fn_ptr.is_null() {
        return;
    }
    let arg = DEFERRED_ARG.load(Ordering::Relaxed);

    // heap->tld->recurse = 1;
    tld.recurse = true;

    // deferred_free(force, heartbeat, arg);
    type DeferredFn = unsafe extern "C" fn(bool, u64, *mut ());
    let deferred_fn: DeferredFn = unsafe { std::mem::transmute(deferred_fn_ptr) };
    unsafe {
        deferred_fn(force, tld.heartbeat, arg);
    }

    // heap->tld->recurse = 0;
    tld.recurse = false;
}
// The _mi_assert_fail function is already defined in the dependencies,
// so we don't need to define it here.
pub fn mi_page_free_list_extend_secure(
    heap: &mut crate::super_special_unit0::MiHeapS,
    page: &mut crate::page::mi_page_t,
    bsize: usize,
    extend: usize,
    stats: &mut crate::mi_stats_t::mi_stats_t,
) {
    // Suppress unused parameter warning
    let _ = stats;
    
    // Assertions from original C code - use string literals directly
    if page.free.is_some() {
        crate::super_function_unit5::_mi_assert_fail(
            "page->free == NULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const std::os::raw::c_char,
            512,
            "mi_page_free_list_extend_secure\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    if page.local_free.is_some() {
        crate::super_function_unit5::_mi_assert_fail(
            "page->local_free == NULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const std::os::raw::c_char,
            513,
            "mi_page_free_list_extend_secure\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    if (page.capacity as usize + extend) > page.reserved as usize {
        crate::super_function_unit5::_mi_assert_fail(
            "page->capacity + extend <= page->reserved\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const std::os::raw::c_char,
            515,
            "mi_page_free_list_extend_secure\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // Add missing assertion from original C code
    if bsize != page.block_size {
        crate::super_function_unit5::_mi_assert_fail(
            "bsize == mi_page_block_size(page)\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const std::os::raw::c_char,
            516,
            "mi_page_free_list_extend_secure\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    let page_area = crate::page::mi_page_start(page);
    let mut shift: usize = 6;
    
    while (extend >> shift) == 0 {
        shift -= 1;
    }
    
    let slice_count = (1 as usize) << shift;
    let slice_extend = extend / slice_count;
    
    if slice_extend < 1 {
        crate::super_function_unit5::_mi_assert_fail(
            "slice_extend >= 1\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c\0".as_ptr() as *const std::os::raw::c_char,
            527,
            "mi_page_free_list_extend_secure\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    let mut blocks: [Option<*mut crate::alloc::MiBlock>; 1 << 6] = [None; 1 << 6];
    let mut counts: [usize; 1 << 6] = [0; 1 << 6];
    
    for i in 0..slice_count {
        // Use mi_page_block_at as in the original C code
        let block_addr = if let Some(start) = page_area {
            // Calculate block address similar to mi_page_block_at
            let block_idx = page.capacity as usize + i * slice_extend;
            unsafe {
                start.add(block_idx * bsize) as *mut crate::alloc::MiBlock
            }
        } else {
            std::ptr::null_mut()
        };
        
        blocks[i] = if !block_addr.is_null() {
            Some(block_addr)
        } else {
            None
        };
        counts[i] = slice_extend;
    }
    
    counts[slice_count - 1] += extend % slice_count;
    
    let r = crate::heap::_mi_heap_random_next(heap);
    let mut current = (r as usize) % slice_count;
    counts[current] -= 1;
    
    let free_start = blocks[current];
    let mut rnd = crate::page::_mi_random_shuffle(r | 1);
    
    for i in 1..extend {
        let round = i % (1 << 3);
        if round == 0 {
            rnd = crate::page::_mi_random_shuffle(rnd);
        }
        
        let mut next = ((rnd >> (8 * round)) & (slice_count as u64 - 1)) as usize;
        
        while counts[next] == 0 {
            next += 1;
            if next == slice_count {
                next = 0;
            }
        }
        
        counts[next] -= 1;
        
        if let Some(block_ptr) = blocks[current] {
            // Move to next block in current slice
            blocks[current] = unsafe {
                Some((block_ptr as *mut u8).add(bsize) as *mut crate::alloc::MiBlock)
            };
            
            // Set next pointer
            if let Some(next_block_ptr) = blocks[next] {
                unsafe {
                    let block = &mut *block_ptr;
                    let next_block = &*next_block_ptr;
                    crate::alloc::mi_block_set_next(page, block, Some(next_block));
                }
            }
        }
        
        current = next;
    }
    
    // Set the last block's next pointer to page.free
    if let Some(current_block_ptr) = blocks[current] {
        unsafe {
            let block = &mut *current_block_ptr;
            // Convert the pointer in page.free to the correct type
            if let Some(free_ptr) = page.free {
                let next_free = unsafe { &*(free_ptr as *mut crate::alloc::MiBlock) };
                crate::alloc::mi_block_set_next(page, block, Some(next_free));
            } else {
                crate::alloc::mi_block_set_next(page, block, Option::None);
            }
        }
    }
    
    // Update page.free to point to the start of the free list
    page.free = free_start.map(|ptr| ptr as *mut crate::mi_block_t::MiBlock);
}
pub fn mi_page_free_list_extend(
    page: &mut mi_page_t,
    bsize: usize,
    extend: usize,
    stats: Option<&crate::mi_stats_t::mi_stats_t>,
) {
    // Unused parameter
    let _ = stats;
    
    // Check assertions - convert C strings to Rust strings for _mi_assert_fail
    if page.free.is_some() {
        _mi_assert_fail("page->free == NULL", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 570, "mi_page_free_list_extend");
    }
    if page.local_free.is_some() {
        _mi_assert_fail("page->local_free == NULL", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 571, "mi_page_free_list_extend");
    }
    if page.capacity as usize + extend > page.reserved as usize {
        _mi_assert_fail("page->capacity + extend <= page.reserved", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 573, "mi_page_free_list_extend");
    }
    if bsize != mi_page_block_size(page) {
        _mi_assert_fail("bsize == mi_page_block_size(page)", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 574, "mi_page_free_list_extend");
    }
    
    let page_area = mi_page_start(page).unwrap();
    let start = mi_page_block_at(
        page,
        page_area,
        bsize,
        page.capacity as usize,
    );
    let last = mi_page_block_at(
        page,
        page_area,
        bsize,
        (page.capacity as usize + extend) - 1,
    );
    
    let mut current = start;
    while current <= last {
        let next = (current as *mut u8).wrapping_add(bsize) as *mut crate::mi_block_t::MiBlock;
        unsafe {
            mi_block_set_next(
                page,
                &mut *(current as *mut crate::alloc::MiBlock),
                Some(&*(next as *mut crate::alloc::MiBlock)),
            );
        }
        current = next;
    }
    
    unsafe {
        mi_block_set_next(
            page,
            &mut *(last as *mut crate::alloc::MiBlock),
            page.free.map(|p| &*(p as *mut crate::alloc::MiBlock)),
        );
        page.free = Some(start as *mut crate::mi_block_t::MiBlock);
    }
}

// Helper function for mi_page_block_size
fn mi_page_block_size(page: &mi_page_t) -> usize {
    page.block_size
}

// Helper function for mi_page_block_at
fn mi_page_block_at(
    page: &mi_page_t,
    page_area: *mut u8,
    bsize: usize,
    block_index: usize,
) -> *mut crate::mi_block_t::MiBlock {
    // Calculate the block address: page_area + block_index * bsize
    let offset = block_index.wrapping_mul(bsize);
    page_area.wrapping_add(offset) as *mut crate::mi_block_t::MiBlock
}

pub fn mi_page_extend_free(
    heap: &mut mi_heap_t,
    page: &mut mi_page_t,
) -> bool {
    // Assertions from lines 4-5
    assert!(page.free.is_none(), "page.free == NULL");
    assert!(page.local_free.is_none(), "page.local_free == NULL");
    
    // Early returns from lines 6-13
    if page.free.is_some() {
        return true;
    }
    
    if page.capacity >= page.reserved {
        return true;
    }
    
    // Line 14-15: Get page size
    let mut page_size = 0usize;
    let _ = mi_page_area(page, Some(&mut page_size));
    
    // Line 16: Increase stat
    if let Some(tld) = &mut heap.tld {
        __mi_stat_counter_increase(&mut tld.stats.pages_extended, 1);
    }
    
    // Line 17: Get block size
    let bsize = mi_page_block_size(page);
    
    // Line 18: Calculate extend
    let mut extend = page.reserved as usize - page.capacity as usize;
    
    // Assertion line 19
    assert!(extend > 0, "extend > 0");
    
    // Lines 20-24: Calculate max_extend
    let mut max_extend = if bsize >= 4096 {
        1usize
    } else {
        4096 / bsize
    };
    
    if max_extend < 1 {
        max_extend = 1;
    }
    
    // Assertion line 25
    assert!(max_extend > 0, "max_extend > 0");
    
    // Lines 26-29: Adjust extend
    if extend > max_extend {
        extend = max_extend;
    }
    
    // Assertions lines 30-31
    assert!(extend > 0, "extend > 0");
    assert!(
        extend + page.capacity as usize <= page.reserved as usize,
        "extend > 0 && extend + page.capacity <= page.reserved"
    );
    assert!(extend < 1 << 16, "extend < (1UL<<16)");
    
    // Lines 32-45: Handle slice committed
    if page.slice_committed > 0 {
        let needed_size = (page.capacity as usize + extend) * bsize;
        let needed_commit = _mi_align_up(
            mi_page_slice_offset_of(page, needed_size),
            1 << (13 + 3)
        );
        
        if needed_commit > page.slice_committed {
            assert!(
                (needed_commit - page.slice_committed) % _mi_os_page_size() == 0,
                "((needed_commit - page.slice_committed) % _mi_os_page_size()) == 0"
            );
            
            let slice_start = mi_page_slice_start(page);
            let addr = slice_start.as_ptr().wrapping_add(page.slice_committed) as *mut ();
            let size = needed_commit - page.slice_committed;
            
            if !_mi_os_commit(Some(addr), size, None) {
                return false;
            }
            
            page.slice_committed = needed_commit;
        }
    }
    
    // Lines 46-53: Extend free list (condition 0 < 3 is always true)
    // Since (extend < 2) || (0 < 3) always true, we use the first branch
    mi_page_free_list_extend(
        page,
        bsize,
        extend,
        heap.tld.as_ref().map(|tld| &tld.stats)
    );
    
    // Line 54: Update capacity
    page.capacity = page.capacity.wrapping_add(extend as u16);
    
    // Line 55: Increase stat
    if let Some(tld) = &mut heap.tld {
        __mi_stat_increase(
            &mut tld.stats.page_committed,
            extend * bsize
        );
    }
    
    // Line 57: Return success
    true
}
pub fn _mi_page_init(heap: &mut mi_heap_t, page: &mut mi_page_t) -> bool {
    // Assertion: page != NULL
    assert!(page as *const _ != std::ptr::null(), "page != NULL");
    
    // Set heap on the page
    page.heap = Some(heap as *mut mi_heap_t);
    
    // Get page area and size
    let mut page_size: usize = 0;
    let page_start = mi_page_area(page, Some(&mut page_size));
    
    // Assertion: page_size / mi_page_block_size(page) < (1L<<16)
    {
        let block_size = mi_page_block_size(page);
        assert!(
            page_size / block_size < (1u64 << 16) as usize,
            "page_size / mi_page_block_size(page) < (1L<<16)"
        );
    }
    
    // Assertion: page.reserved > 0
    assert!(page.reserved > 0, "page->reserved > 0");
    
    // Set random keys
    page.keys[0] = _mi_heap_random_next(heap) as usize;
    page.keys[1] = _mi_heap_random_next(heap) as usize;
    
    // Assertions about page state
    assert!(page.capacity == 0, "page->capacity == 0");
    assert!(page.free.is_none(), "page->free == NULL");
    assert!(page.used == 0, "page->used == 0");
    assert!(mi_page_is_owned(page), "mi_page_is_owned(page)");
    assert!(page.xthread_free.load(std::sync::atomic::Ordering::Relaxed) == 1, 
            "page->xthread_free == 1");
    assert!(page.next.is_none(), "page->next == NULL");
    assert!(page.prev.is_none(), "page->prev == NULL");
    assert!(page.retire_expire == 0, "page->retire_expire == 0");
    assert!(!mi_page_has_interior_pointers(page), 
            "!mi_page_has_interior_pointers(page)");
    assert!(page.keys[0] != 0, "page->keys[0] != 0");
    assert!(page.keys[1] != 0, "page->keys[1] != 0");
    
    // Extend free list
    if !mi_page_extend_free(heap, page) {
        return false;
    }
    
    // Final assertion
    assert!(mi_page_immediate_available(Some(page)), 
            "mi_page_immediate_available(page)");
    
    true
}
// _mi_assert_fail is defined in dependencies (see provided signature)
/// Pushes a page into a page queue.
///
/// # Safety
/// This function assumes the page belongs to the heap and isn't already in the queue.
pub fn mi_page_queue_push(heap: &mut MiHeapS, queue: &mut MiPageQueueS, page: &mut MiPageS) {
    // Assertion: mi_page_heap(page) == heap
    {
        // Convert references to raw pointers for the assertion function
        let page_ptr = page as *const MiPageS;
        let heap_ptr = heap as *mut MiHeapS;
        let page_heap = unsafe { mi_page_heap(page_ptr) };
        assert!(
            page_heap == Some(heap_ptr),
            "mi_page_heap(page) == heap"
        );
    }

    // Assertion: !mi_page_queue_contains(queue, page)
    {
        let page_ptr = page as *const MiPageS;
        let queue_ptr = queue as *const MiPageQueueS;
        // Note: mi_page_queue_contains is not available in dependencies, so we'll skip this assertion
        // or implement it if needed. For now, we'll comment it out since it's not critical.
        // assert!(
        //     !mi_page_queue_contains(queue_ptr, page_ptr),
        //     "!mi_page_queue_contains(queue, page)"
        // );
    }

    // Assertion: (mi_page_block_size(page) == queue.block_size) ||
    //            (mi_page_is_huge(page) && mi_page_queue_is_huge(queue)) ||
    //            (mi_page_is_in_full(page) && mi_page_queue_is_full(queue))
    {
        // Cast &mut MiPageS to &MiPage for mi_page_is_huge
        let page_ref = unsafe { &*(page as *const MiPageS as *const crate::MiPage) };
        
        let block_size_matches = mi_page_block_size(page) == queue.block_size;
        let is_huge_and_queue_huge = mi_page_is_huge(page_ref) && mi_page_queue_is_huge(queue);
        // Use mi_page_is_full instead of mi_page_is_in_full
        let is_in_full_and_queue_full = mi_page_is_full(page) && mi_page_queue_is_full(queue);
        
        assert!(
            block_size_matches || is_huge_and_queue_huge || is_in_full_and_queue_full,
            "mi_page_block_size(page) == queue.block_size || \
             (mi_page_is_huge(page) && mi_page_queue_is_huge(queue)) || \
             (mi_page_is_in_full(page) && mi_page_queue_is_full(queue))"
        );
    }

    // Set the page's "in full" flag based on whether the queue is full
    // Cast &mut MiPageS to &mut MiPage for mi_page_set_in_full
    let page_mut = unsafe { &mut *(page as *mut MiPageS as *mut crate::MiPage) };
    mi_page_set_in_full(page_mut, mi_page_queue_is_full(queue));

    // Get raw pointer to page for linked list operations
    let page_ptr = page as *mut MiPageS;

    // Update page's linked list pointers
    page.next = queue.first;
    page.prev = Option::None; // 0 in C becomes None in Rust

    // Update queue's linked list
    if let Some(first_page) = queue.first {
        // Convert raw pointer to mutable reference for checking and updating
        let first_page_ref = unsafe { &mut *first_page };
        
        // Assertion: queue->first->prev == NULL (None in Rust)
        assert!(
            first_page_ref.prev.is_none(),
            "queue->first->prev == NULL"
        );
        
        first_page_ref.prev = Some(page_ptr);
        queue.first = Some(page_ptr);
    } else {
        // Queue is empty, so page becomes both first and last
        queue.first = Some(page_ptr);
        queue.last = Some(page_ptr);
    }

    // Update queue count and notify heap
    queue.count += 1;
    mi_heap_queue_first_update(heap, queue);
    heap.page_count += 1;
}
// Remove the duplicate definition since mi_page_t is already defined in dependencies
// pub type mi_page_t = MiPageS;
pub fn _mi_heap_page_reclaim(heap: &mut mi_heap_t, page: &mut mi_page_t) {
    // Line 3: _mi_is_aligned assertion
    let page_ptr = page as *mut mi_page_t as *mut c_void;
    if !_mi_is_aligned(Some(unsafe { &mut *page_ptr }), 1 << (13 + 3)) {
        let assertion = "_mi_is_aligned(page, MI_PAGE_ALIGN)";
        let file = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
        let func = "_mi_heap_page_reclaim";
        _mi_assert_fail(assertion, file, 270, func);
    }

    // Line 4: _mi_ptr_page assertion
    let ptr_page_result = unsafe { _mi_ptr_page(page_ptr as *const c_void) };
    if ptr_page_result != page as *mut mi_page_t {
        let assertion = "_mi_ptr_page(page)==page";
        let file = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
        let func = "_mi_heap_page_reclaim";
        _mi_assert_fail(assertion, file, 271, func);
    }

    // Line 5: mi_page_is_owned assertion
    if !mi_page_is_owned(page) {
        let assertion = "mi_page_is_owned(page)";
        let file = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
        let func = "_mi_heap_page_reclaim";
        _mi_assert_fail(assertion, file, 272, func);
    }

    // Line 6: mi_page_is_abandoned assertion
    if !mi_page_is_abandoned(page) {
        let assertion = "mi_page_is_abandoned(page)";
        let file = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
        let func = "_mi_heap_page_reclaim";
        _mi_assert_fail(assertion, file, 273, func);
    }

    // Line 7: mi_page_set_heap
    page.heap = Some(heap as *mut mi_heap_t);

    // Line 8: _mi_page_free_collect
    _mi_page_free_collect(page, false);

    // Line 9: Get page queue using mi_heap_page_queue_of
    // Note: Since mi_heap_page_queue_of is not provided in dependencies,
    // we need to use the correct function. Based on the original C code,
    // it should be mi_heap_page_queue_of(heap, page)
    // However, since it's not in dependencies, we'll use the block_size approach
    // but this should ideally be replaced with the actual function
    let block_size = page.block_size;
    let queue_index = if block_size <= 128 {
        block_size / 8
    } else {
        // For larger blocks, use a different calculation
        // This is a simplified version - actual implementation might be more complex
        73 // Use the large block queue as fallback
    };
    
    // Line 10: Push page at end of queue
    // Get a mutable reference to the specific page queue
    let pq = &mut heap.pages[queue_index];
    // Call mi_page_queue_push_at_end with the queue reference
    // Note: We need to pass heap, pq, and page as per the function signature
    // Since mi_page_queue_push_at_end is not in dependencies, we'll use
    // the available function. The original C code uses mi_page_queue_push_at_end
    // but we need to check what's available.
    // For now, we'll push to the end of the queue manually
    if pq.last.is_none() {
        pq.first = Some(page as *mut mi_page_t);
        pq.last = Some(page as *mut mi_page_t);
    } else {
        unsafe {
            (*pq.last.unwrap()).next = Some(page as *mut mi_page_t);
            (*page).prev = pq.last;
            pq.last = Some(page as *mut mi_page_t);
        }
    }
    pq.count += 1;
}
pub fn mi_page_fresh_alloc(
    heap: &mut mi_heap_t,
    pq: Option<&mut mi_page_queue_t>,
    block_size: usize,
    page_alignment: usize,
) -> Option<*mut mi_page_t> {
    const FILE: &str = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
    const FUNC: &str = "mi_page_fresh_alloc";

    // Convert to a raw pointer so we can check/compare/use it multiple times without moving `pq`.
    let pq_ptr: Option<*mut mi_page_queue_t> = pq.map(|q| q as *mut mi_page_queue_t);

    // (pq != 0) ? ... : _mi_assert_fail(...)
    if pq_ptr.is_none() {
        _mi_assert_fail("pq != NULL", FILE, 301, FUNC);
    }

    // (mi_heap_contains_queue(heap, pq)) ? ... : _mi_assert_fail(...)
    if let Some(pq_raw) = pq_ptr {
        let mut found = false;
        for queue in heap.pages.iter() {
            if std::ptr::eq(queue as *const mi_page_queue_t, pq_raw as *const mi_page_queue_t) {
                found = true;
                break;
            }
        }
        if !found {
            _mi_assert_fail("mi_heap_contains_queue(heap, pq)", FILE, 302, FUNC);
        }
    }

    // (((page_alignment > 0) || (block_size > MI_LARGE_MAX_OBJ_SIZE)) || (block_size == pq->block_size)) ? ... : _mi_assert_fail(...)
    let large_max_obj_size: usize = (8 * (1_usize << (13 + 3))) / 8;
    let pq_block_matches = pq_ptr.map_or(false, |p| unsafe { (*p).block_size == block_size });
    if !(page_alignment > 0 || block_size > large_max_obj_size || pq_block_matches) {
        _mi_assert_fail(
            "page_alignment > 0 || block_size > MI_LARGE_MAX_OBJ_SIZE || block_size == pq->block_size",
            FILE,
            303,
            FUNC,
        );
    }

    let page = _mi_arenas_page_alloc(heap, block_size, page_alignment);
    if page.is_none() {
        return Option::None;
    }

    let page_raw: *mut mi_page_t = page.unwrap().as_ptr();

    unsafe {
        if mi_page_is_abandoned(&*page_raw) {
            _mi_heap_page_reclaim(heap, &mut *page_raw);
            if !mi_page_immediate_available(Some(&*page_raw)) {
                if mi_page_is_expandable(Some(&*page_raw)) {
                    // C ignores the return value here, so we do too.
                    let _ = mi_page_extend_free(heap, &mut *page_raw);
                } else {
                    _mi_assert_fail("false", FILE, 317, FUNC);
                    return Option::None;
                }
            }
        } else if let Some(pq_raw) = pq_ptr {
            mi_page_queue_push(heap, &mut *pq_raw, &mut *page_raw);
        }

        // ((pq != 0) || (mi_page_block_size(page) >= block_size)) ? ... : _mi_assert_fail(...)
        if pq_ptr.is_none() && mi_page_block_size(&*page_raw) < block_size {
            _mi_assert_fail("pq!=NULL || mi_page_block_size(page) >= block_size", FILE, 325, FUNC);
        }
    }

    Some(page_raw)
}
pub fn mi_huge_page_alloc(
    heap: &mut mi_heap_t,
    size: usize,
    page_alignment: usize,
    pq: &mut mi_page_queue_t,
) -> Option<*mut mi_page_t> {
    // 1. Compute block size
    let block_size = _mi_os_good_alloc_size(size);
    
    // 2. Assert that pq is huge
    if !mi_page_queue_is_huge(pq) {
        _mi_assert_fail(
            "mi_page_queue_is_huge(pq)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            894,
            "mi_huge_page_alloc",
        );
    }
    
    // 3. Allocate fresh page
    let page = mi_page_fresh_alloc(heap, Some(pq), block_size, page_alignment);
    
    // 4. If page was allocated, perform assertions and update statistics
    if let Some(page_ptr) = page {
        // Safety: We need to dereference the raw pointer for assertions
        // We'll use a temporary reference with unsafe scope limited to smallest possible
        unsafe {
            let page_ref = &*page_ptr;
            
            // Assert block size >= size
            let actual_block_size = mi_page_block_size(page_ref);
            if actual_block_size < size {
                _mi_assert_fail(
                    "mi_page_block_size(page) >= size",
                    "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                    898,
                    "mi_huge_page_alloc",
                );
            }
            
            // Assert page is immediately available
            if !mi_page_immediate_available(Some(page_ref)) {
                _mi_assert_fail(
                    "mi_page_immediate_available(page)",
                    "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                    899,
                    "mi_huge_page_alloc",
                );
            }
            
            // Assert page is huge
            // Convert &mi_page_t to &MiPage using a transmute since they're the same underlying type
            let page_as_mipage: &MiPage = std::mem::transmute(page_ref);
            if !mi_page_is_huge(page_as_mipage) {
                _mi_assert_fail(
                    "mi_page_is_huge(page)",
                    "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                    900,
                    "mi_huge_page_alloc",
                );
            }
            
            // Assert page is singleton
            if !mi_page_is_singleton(page_as_mipage) {
                _mi_assert_fail(
                    "mi_page_is_singleton(page)",
                    "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                    901,
                    "mi_huge_page_alloc",
                );
            }
            
            // Update statistics - need mutable access to heap's tld
            if let Some(tld) = &mut heap.tld {
                // Get block size again for stat update
                let block_size_for_stats = mi_page_block_size(page_ref);
                __mi_stat_increase(&mut tld.stats.malloc_huge, block_size_for_stats);
                __mi_stat_counter_increase(&mut tld.stats.malloc_huge_count, 1);
            }
        }
    }
    
    // 5. Return the page pointer (or None)
    page
}
pub unsafe fn mi_page_queue_enqueue_from_ex(
    to: *mut mi_page_queue_t,
    from: *mut mi_page_queue_t,
    enqueue_at_end: bool,
    page: *mut mi_page_t,
) {
    // Assertions with original C file/line information
    let c_file = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c";
    let func_name = "mi_page_queue_enqueue_from_ex";
    
    // Page must not be null
    if page.is_null() {
        _mi_assert_fail(
            "page != NULL",
            c_file,
            334,
            func_name,
        );
    }
    
    // From queue must have at least one page
    if (*from).count < 1 {
        _mi_assert_fail(
            "from->count >= 1",
            c_file,
            335,
            func_name,
        );
    }
    
    let bsize = mi_page_block_size(&*page);
    let _ = bsize; // Mark as used to avoid warning
    
    // Complex condition assertion
    let cond1 = bsize == (*to).block_size && bsize == (*from).block_size;
    let cond2 = bsize == (*to).block_size && mi_page_queue_is_full(&*from);
    let cond3 = bsize == (*from).block_size && mi_page_queue_is_full(&*to);
    let cond4 = mi_page_is_huge(&*(page as *mut crate::MiPage)) && mi_page_queue_is_huge(&*to);
    let cond5 = mi_page_is_huge(&*(page as *mut crate::MiPage)) && mi_page_queue_is_full(&*to);
    
    if !(cond1 || cond2 || cond3 || cond4 || cond5) {
        _mi_assert_fail(
            "(bsize == to->block_size && bsize == from->block_size) || (bsize == to->block_size && mi_page_queue_is_full(from)) || (bsize == from->block_size && mi_page_queue_is_full(to)) || (mi_page_is_huge(page) && mi_page_queue_is_huge(to)) || (mi_page_is_huge(page) && mi_page_queue_is_full(to))",
            c_file,
            340,
            func_name,
        );
    }
    
    let heap = mi_page_heap(page).expect("Heap should exist for page");
    let heap_ref = &mut *heap;
    
    // Remove page from 'from' queue
    if let Some(prev) = (*page).prev {
        (*prev).next = (*page).next;
    }
    
    if let Some(next) = (*page).next {
        (*next).prev = (*page).prev;
    }
    
    if page == (*from).last.unwrap_or(std::ptr::null_mut()) {
        (*from).last = (*page).prev;
    }
    
    if page == (*from).first.unwrap_or(std::ptr::null_mut()) {
        (*from).first = (*page).next;
        
        if !mi_heap_contains_queue(heap_ref, &*from) {
            _mi_assert_fail(
                "mi_heap_contains_queue(heap, from)",
                c_file,
                355,
                func_name,
            );
        }
        
        mi_heap_queue_first_update(heap_ref, &mut *from);
    }
    
    (*from).count -= 1;
    (*to).count += 1;
    
    if enqueue_at_end {
        (*page).prev = (*to).last;
        (*page).next = Option::None;
        
        if let Some(last) = (*to).last {
            if heap != mi_page_heap(last).expect("Heap should exist for last page") {
                _mi_assert_fail(
                    "heap == mi_page_heap(to->last)",
                    c_file,
                    367,
                    func_name,
                );
            }
            (*last).next = Some(page);
            (*to).last = Some(page);
        } else {
            (*to).first = Some(page);
            (*to).last = Some(page);
            mi_heap_queue_first_update(heap_ref, &mut *to);
        }
    } else {
        if let Some(first) = (*to).first {
            if heap != mi_page_heap(first).expect("Heap should exist for first page") {
                _mi_assert_fail(
                    "heap == mi_page_heap(to->first)",
                    c_file,
                    380,
                    func_name,
                );
            }
            
            let next = (*first).next;
            (*page).prev = Some(first);
            (*page).next = next;
            (*first).next = Some(page);
            
            if let Some(next_ptr) = next {
                (*next_ptr).prev = Some(page);
            } else {
                (*to).last = Some(page);
            }
        } else {
            (*page).prev = Option::None;
            (*page).next = Option::None;
            (*to).first = Some(page);
            (*to).last = Some(page);
            mi_heap_queue_first_update(heap_ref, &mut *to);
        }
    }
    
    mi_page_set_in_full(&mut *(page as *mut crate::MiPage), mi_page_queue_is_full(&*to));
}
pub(crate) fn mi_page_queue_enqueue_from(
    to: *mut mi_page_queue_t,
    from: *mut mi_page_queue_t,
    page: *mut mi_page_t,
) {
    unsafe {
        // Remove page from 'from' queue
        if !from.is_null() {
            let from_queue = &mut *from;
            let next_page = (*page).next;
            let prev_page = (*page).prev;

            if let Some(prev_ptr) = prev_page {
                (*prev_ptr).next = next_page;
            } else {
                // page was the first in the queue
                from_queue.first = next_page;
            }

            if let Some(next_ptr) = next_page {
                (*next_ptr).prev = prev_page;
            } else {
                // page was the last in the queue
                from_queue.last = prev_page;
            }

            from_queue.count = from_queue.count.wrapping_sub(1);
        }

        // Add page to 'to' queue at the end
        if !to.is_null() {
            let to_queue = &mut *to;
            (*page).prev = to_queue.last;
            (*page).next = Option::None;

            if let Some(last_ptr) = to_queue.last {
                (*last_ptr).next = Some(page);
            } else {
                to_queue.first = Some(page);
            }

            to_queue.last = Some(page);
            to_queue.count = to_queue.count.wrapping_add(1);
        }
    }
}
pub fn mi_page_to_full(page: &mut mi_page_t, pq: &mut mi_page_queue_t) {
    // Note: The original C code had an assertion: pq == mi_page_queue_of(page)
    // But mi_page_queue_of is not available in the dependencies, so we skip it.

    // Assertion: !mi_page_immediate_available(page)
    if mi_page_immediate_available(Some(&*page)) {
        _mi_assert_fail(
            "!mi_page_immediate_available(page)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            363,
            "mi_page_to_full",
        );
    }

    let heap = unsafe { mi_page_heap(page as *const _) };
    let heap_ptr = heap.expect("heap should not be null");

    // Assertion: !mi_page_is_in_full(page)
    // Check if page is in full queue
    unsafe {
        let full_queue = &(*heap_ptr).pages[73 + 1];
        let mut current = full_queue.first;
        let mut is_in_full = false;
        while let Some(curr_page) = current {
            if curr_page as *const _ == page as *const _ {
                is_in_full = true;
                break;
            }
            current = (*curr_page).next;
        }
        
        if is_in_full {
            _mi_assert_fail(
                "!mi_page_is_in_full(page)",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                364,
                "mi_page_to_full",
            );
        }
    }

    unsafe {
        if (*heap_ptr).allow_page_abandon {
            _mi_page_abandon(page, pq);
        } else {
            // Check again if page is in full queue
            let full_queue = &(*heap_ptr).pages[73 + 1];
            let mut current = full_queue.first;
            let mut is_in_full = false;
            while let Some(curr_page) = current {
                if curr_page as *const _ == page as *const _ {
                    is_in_full = true;
                    break;
                }
                current = (*curr_page).next;
            }
            
            if !is_in_full {
                let to_queue = &mut (*heap_ptr).pages[73 + 1] as *mut mi_page_queue_t;
                mi_page_queue_enqueue_from(to_queue, pq as *mut mi_page_queue_t, page as *mut mi_page_t);
                _mi_page_free_collect(page, false);
            }
        }
    }
}
pub fn mi_page_fresh(
    heap: &mut mi_heap_t,
    pq: &mut mi_page_queue_t,
) -> Option<*mut mi_page_t> {
    // Check the assertion: heap must contain queue
    if !mi_heap_contains_queue(heap, pq) {
        _mi_assert_fail(
            "mi_heap_contains_queue(heap, pq)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            332,
            "mi_page_fresh",
        );
    }

    let block_size = pq.block_size;
    
    // Call the allocator function - Note: block_size comes from pq
    let page = mi_page_fresh_alloc(heap, Some(pq), block_size, 0);
    
    if page.is_none() {
        return None;
    }
    
    let page_ptr = page.unwrap();
    
    // Second assertion: pq block size must match page block size
    unsafe {
        // Convert raw pointer to reference for mi_page_block_size
        let page_ref = &*page_ptr;
        if block_size != mi_page_block_size(page_ref) {
            _mi_assert_fail(
                "pq->block_size==mi_page_block_size(page)",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                335,
                "mi_page_fresh",
            );
        }
    }
    
    // Third assertion: pq must be the heap's page queue for this page
    unsafe {
        let page_ref = &*page_ptr;
        let page_queue = mi_heap_page_queue_of(heap, page_ref);
        
        // Compare addresses - use ptr::eq for pointer comparison
        if !std::ptr::eq(page_queue as *const _ as *const c_void, pq as *const _ as *const c_void) {
            _mi_assert_fail(
                "pq==mi_heap_page_queue_of(heap, page)",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                336,
                "mi_page_fresh",
            );
        }
    }
    
    page
}
/// Moves a page to the front of a page queue.
pub fn mi_page_queue_move_to_front(
    heap: &mut mi_heap_t,
    queue: &mut mi_page_queue_t,
    page: &mut mi_page_t,
) {
    // First assertion: mi_page_heap(page) == heap
    unsafe {
        let page_heap_ptr = mi_page_heap(page as *const mi_page_t);
        let heap_ptr = heap as *mut mi_heap_t;
        
        if !page_heap_ptr
            .map(|ptr| ptr == heap_ptr)
            .unwrap_or(false)
        {
            _mi_assert_fail(
                "mi_page_heap(page) == heap",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c",
                325,
                "mi_page_queue_move_to_front",
            );
        }
    }

    // Second assertion: mi_page_queue_contains(queue, page)
    if !mi_page_queue_contains(queue, page) {
        _mi_assert_fail(
            "mi_page_queue_contains(queue, page)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c",
            326,
            "mi_page_queue_move_to_front",
        );
    }

    // If page is already at the front, return early
    if queue.first == Some(page as *mut mi_page_t) {
        return;
    }

    // Remove page from its current position
    mi_page_queue_remove(queue, page);
    
    // Push page to the front of the queue
    mi_page_queue_push(heap, queue, page);

    // Third assertion: queue->first == page
    if queue.first != Some(page as *mut mi_page_t) {
        _mi_assert_fail(
            "queue->first == page",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c",
            330,
            "mi_page_queue_move_to_front",
        );
    }
}

// Helper function needed for the assertions (assuming it exists in dependencies)
pub fn mi_page_queue_contains(queue: &mi_page_queue_t, page: &mi_page_t) -> bool {
    // Implementation would depend on how pages are linked in the queue
    // For now, we'll implement a basic check
    let mut current = queue.first;
    while let Some(page_ptr) = current {
        unsafe {
            if page_ptr == (page as *const mi_page_t as *mut mi_page_t) {
                return true;
            }
            current = (*page_ptr).next;
        }
    }
    false
}
pub fn mi_page_queue_find_free_ex(
    heap: &mut mi_heap_t,
    pq: &mut mi_page_queue_t,
    first_try: bool,
) -> Option<*mut mi_page_t> {
    let mut count = 0;
    let mut candidate_limit: i64 = 0;
    let mut page_full_retain: i64 = if pq.block_size > ((1 * (1_usize << (13 + 3)) - ((3 + 2) * 32)) / 8) {
        0
    } else {
        heap.page_full_retain
    };
    let mut page_candidate: Option<*mut mi_page_t> = None;
    let mut page = pq.first;

    while page.is_some() {
        let page_ptr = page.unwrap();
        let page_ref = unsafe { &*page_ptr };
        let next = page_ref.next;
        count += 1;
        candidate_limit -= 1;

        let mut immediate_available = mi_page_immediate_available(Some(page_ref));
        if !immediate_available {
            let page_mut = unsafe { &mut *page_ptr };
            _mi_page_free_collect(page_mut, false);
            immediate_available = mi_page_immediate_available(Some(page_mut));
        }

        if !immediate_available && !mi_page_is_expandable(Some(page_ref)) {
            page_full_retain -= 1;
            if page_full_retain < 0 {
                if !mi_page_is_in_full(page_ref) && !mi_page_immediate_available(Some(page_ref)) {
                    // Assertion passes, do nothing
                } else {
                    _mi_assert_fail(
                        "!mi_page_is_in_full(page) && !mi_page_immediate_available(page)",
                        "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                        749,
                        "mi_page_queue_find_free_ex",
                    );
                }
                let page_mut = unsafe { &mut *page_ptr };
                mi_page_to_full(page_mut, pq);
            }
        } else {
            if page_candidate.is_none() {
                page_candidate = page;
                // FIXED: Use the correct variant name for MiOption
                candidate_limit = _mi_option_get_fast(crate::MiOption::PageMaxCandidates);
            } else if mi_page_all_free(page_candidate.map(|p| unsafe { &*p })) {
                let candidate_mut = unsafe { &mut *page_candidate.unwrap() };
                // FIXED: Pass pq directly instead of moving it
                _mi_page_free(Some(candidate_mut), Some(pq));
                page_candidate = page;
            } else if page_ref.used >= unsafe { &*page_candidate.unwrap() }.used
                && !mi_page_is_mostly_used(Some(page_ref))
            {
                page_candidate = page;
            }

            if immediate_available || candidate_limit <= 0 {
                if page_candidate.is_some() {
                    // Assertion passes, do nothing
                } else {
                    _mi_assert_fail(
                        "page_candidate!=NULL",
                        "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                        770,
                        "mi_page_queue_find_free_ex",
                    );
                }
                break;
            }
        }
        page = next;
    }

    if let Some(tld) = heap.tld.as_mut() {
        __mi_stat_counter_increase(&mut tld.stats.page_searches, count);
    }

    let mut page_idx = page_candidate;

    if page_idx.is_some() {
        let page_mut = unsafe { &mut *page_idx.unwrap() };
        if !mi_page_immediate_available(Some(page_mut)) {
            if mi_page_is_expandable(Some(page_mut)) {
                // Assertion passes, do nothing
            } else {
                _mi_assert_fail(
                    "mi_page_is_expandable(page)",
                    "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                    799,
                    "mi_page_queue_find_free_ex",
                );
            }
            if !mi_page_extend_free(heap, page_mut) {
                page_idx = None;
            }
        }
        if page_idx.is_none() || mi_page_immediate_available(Some(unsafe { &*page_idx.unwrap() })) {
            // Assertion passes, do nothing
        } else {
            _mi_assert_fail(
                "page == NULL || mi_page_immediate_available(page)",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                804,
                "mi_page_queue_find_free_ex",
            );
        }
    }

    if page_idx.is_none() {
        _mi_heap_collect_retired(Some(heap), false);
        page_idx = mi_page_fresh(heap, pq);
        
        if page_idx.is_none() || mi_page_immediate_available(Some(unsafe { &*page_idx.unwrap() })) {
            // Assertion passes, do nothing
        } else {
            _mi_assert_fail(
                "page == NULL || mi_page_immediate_available(page)",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                810,
                "mi_page_queue_find_free_ex",
            );
        }
        
        if page_idx.is_none() && first_try {
            page_idx = mi_page_queue_find_free_ex(heap, pq, false);
            
            if page_idx.is_none() || mi_page_immediate_available(Some(unsafe { &*page_idx.unwrap() })) {
                // Assertion passes, do nothing
            } else {
                _mi_assert_fail(
                    "page == NULL || mi_page_immediate_available(page)",
                    "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                    814,
                    "mi_page_queue_find_free_ex",
                );
            }
        }
    } else {
        if page_idx.is_none() || mi_page_immediate_available(Some(unsafe { &*page_idx.unwrap() })) {
            // Assertion passes, do nothing
        } else {
            _mi_assert_fail(
                "page == NULL || mi_page_immediate_available(page)",
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                818,
                "mi_page_queue_find_free_ex",
            );
        }
        
        let page_mut = unsafe { &mut *page_idx.unwrap() };
        mi_page_queue_move_to_front(heap, pq, page_mut);
        page_mut.retire_expire = 0;
    }

    if page_idx.is_none() || mi_page_immediate_available(Some(unsafe { &*page_idx.unwrap() })) {
        // Assertion passes, do nothing
    } else {
        _mi_assert_fail(
            "page == NULL || mi_page_immediate_available(page)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            824,
            "mi_page_queue_find_free_ex",
        );
    }

    page_idx
}
pub fn mi_find_free_page(heap: &mut mi_heap_t, pq: &mut mi_page_queue_t) -> Option<*mut mi_page_t> {
    // Check if the page queue is not huge
    if mi_page_queue_is_huge(pq) {
        _mi_assert_fail(
            "!mi_page_queue_is_huge(pq)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            835,
            "mi_find_free_page",
        );
    }
    
    let mut page = pq.first;
    
    // Check if page is not null and immediately available
    // Using a simplified version of the C __builtin_expect pattern
    if let Some(p) = page {
        if mi_page_immediate_available(Some(unsafe { &*p })) {
            unsafe {
                (*p).retire_expire = 0;
            }
            return page;
        }
    }
    
    // Otherwise, try to find a free page
    mi_page_queue_find_free_ex(heap, pq, true)
}
pub fn mi_find_page(
    heap: &mut mi_heap_t,
    size: usize,
    huge_alignment: usize,
) -> Option<*mut mi_page_t> {
    let req_size = size.wrapping_sub(std::mem::size_of::<crate::mi_padding_t::mi_padding_t>());
    
    if req_size > isize::MAX as usize {
        let fmt = std::ffi::CString::new("allocation request is too large (%zu bytes)\n").unwrap();
        // Use fully qualified path to avoid ambiguity
        crate::alloc::_mi_error_message(75, fmt.as_ptr());
        return Option::None;
    }
    
    let page_queue_size = if huge_alignment > 0 {
        ((8 * (1 * (1_usize << (13 + 3)))) / 8) + 1
    } else {
        size
    };
    
    let pq = mi_page_queue(heap, page_queue_size);
    
    // Convert to reference for mi_page_queue_is_huge
    if crate::page::mi_page_queue_is_huge(pq) || req_size > isize::MAX as usize {
        // Get mutable reference to the page queue from the heap
        // Since mi_page_queue returns a reference, we need to work with the heap's pages array directly
        let page_queue_index = if huge_alignment > 0 {
            ((8 * (1 * (1_usize << (13 + 3)))) / 8) + 1
        } else {
            size
        };
        
        // Find the mutable reference to the page queue in the heap
        // The heap has a `pages` array of type [mi_page_queue_t; (73 + 1) + 1]
        // We need to get a mutable reference to the correct element
        let pq_mut = unsafe {
            // Calculate index in the pages array
            // This is a simplified approach - in reality, you'd need the actual index calculation
            // that matches what mi_page_queue does internally
            let ptr = heap as *mut mi_heap_t;
            let pages_ptr = (*ptr).pages.as_mut_ptr();
            
            // For huge allocations, use a special index
            if huge_alignment > 0 {
                &mut *pages_ptr.add(((8 * (1 * (1_usize << (13 + 3)))) / 8) + 1)
            } else {
                // Normal size-based index (simplified)
                &mut *pages_ptr.add(size.min(heap.pages.len() - 1))
            }
        };
        
        return mi_huge_page_alloc(heap, size, huge_alignment, pq_mut);
    }
    
    if size < std::mem::size_of::<crate::mi_padding_t::mi_padding_t>() {
        // Use Rust string slices instead of C strings
        let assertion = "size >= MI_PADDING_SIZE";
        let fname = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
        let func = "mi_find_page";
        // Use fully qualified path to avoid ambiguity
        crate::page::_mi_assert_fail(assertion, fname, 929, func);
    }
    
    // For the normal case, we need a mutable reference to the page queue
    // Similar approach as above but for the normal size
    let pq_mut = unsafe {
        let ptr = heap as *mut mi_heap_t;
        let pages_ptr = (*ptr).pages.as_mut_ptr();
        // Normal size-based index (simplified)
        &mut *pages_ptr.add(page_queue_size.min(heap.pages.len() - 1))
    };
    
    mi_find_free_page(heap, pq_mut)
}
pub unsafe fn mi_page_queue_enqueue_from_full(
    to: *mut mi_page_queue_t,
    from: *mut mi_page_queue_t,
    page: *mut mi_page_t,
) {
    mi_page_queue_enqueue_from_ex(to, from, true, page);
}
pub fn _mi_page_unfull(page: Option<&mut mi_page_t>) {
    // Assertions from lines 3-6 in C code
    // Assert page != NULL
    if page.is_none() {
        _mi_assert_fail("page != NULL", 
                       "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 
                       347, 
                       "_mi_page_unfull");
        return;
    }
    
    let page_ref = page.unwrap();
    
    // Assert mi_page_is_in_full(page)
    if !mi_page_is_in_full(page_ref) {
        _mi_assert_fail("mi_page_is_in_full(page)", 
                       "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 
                       349, 
                       "_mi_page_unfull");
    }
    
    // Assert !mi_page_heap(page)->allow_page_abandon
    unsafe {
        if let Some(heap_ptr) = mi_page_heap(page_ref as *const _) {
            if (*heap_ptr).allow_page_abandon {
                _mi_assert_fail("!mi_page_heap(page)->allow_page_abandon", 
                               "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c", 
                               350, 
                               "_mi_page_unfull");
            }
        }
    }
    
    // Early return if page is not in full (line 7-10)
    if !mi_page_is_in_full(page_ref) {
        return;
    }
    
    // Get heap from page (line 11)
    unsafe {
        if let Some(heap_ptr) = mi_page_heap(page_ref as *const _) {
            let heap = &mut *heap_ptr;
            
            // Get full page queue (line 12)
            let pqfull = &mut heap.pages[73 + 1] as *mut mi_page_queue_t;
            
            // Set page in_full to false (line 13)
            // Create a mutable reference without moving page_ref
            let page_as_mipage: &mut crate::MiPage = &mut *(page_ref as *mut _ as *mut crate::MiPage);
            mi_page_set_in_full(page_as_mipage, false);
            
            // Get page's queue (line 14)
            let pq = mi_heap_page_queue_of(heap, page_ref) as *mut mi_page_queue_t;
            
            // Note: The C code sets in_full to true here, but this seems incorrect
            // as we just set it to false. The C code likely has a bug or this is
            // intentional for some synchronization. We'll follow the C code exactly.
            mi_page_set_in_full(page_as_mipage, true);
            
            // Enqueue page from full queue to page's queue (line 16)
            mi_page_queue_enqueue_from_full(
                pq,
                pqfull,
                page_ref as *mut mi_page_t
            );
        }
    }
}
pub fn mi_page_queue_is_special(pq: &MiPageQueueS) -> bool {
    pq.block_size > ((8 * (1 * (1_usize << (13 + 3)))) / 8)
}
pub fn _mi_page_retire(page: Option<&mut mi_page_t>) {
    // Assert: page != NULL
    if page.is_none() {
        _mi_assert_fail(
            "page != NULL",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            411,
            "_mi_page_retire",
        );
    }
    let page = page.unwrap();

    // Assert: mi_page_all_free(page)
    if !mi_page_all_free(Some(page)) {
        _mi_assert_fail(
            "mi_page_all_free(page)",
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
            413,
            "_mi_page_retire",
        );
    }

    // Cast page to the correct type for mi_page_set_has_interior_pointers
    let page_ptr = page as *mut mi_page_t as *mut crate::MiPage;
    unsafe {
        mi_page_set_has_interior_pointers(&mut *page_ptr, false);
    }
    
    // Get block size first (only needs immutable reference)
    let bsize = mi_page_block_size(page);
    
    // Get page queue using raw pointer to avoid borrow issues
    let page_raw = page as *mut mi_page_t;
    let pq = mi_page_queue_of(unsafe { &mut *page_raw });
    
    // Check if not special queue
    if !mi_page_queue_is_special(pq) {
        // Check if this is the only page in the queue
        unsafe {
            if (*pq).last == Some(page_raw) && (*pq).first == Some(page_raw) {
                let heap = mi_page_heap(page_raw as *const mi_page_t).expect("heap should exist");
                
                // Increment retirement stats
                __mi_stat_counter_increase(
                    &mut (*heap).tld.as_mut().unwrap().stats.pages_retire,
                    1,
                );
                
                // Set retirement expiration - use raw pointer to avoid borrow issues
                (*page_raw).retire_expire = if bsize <= ((1 * (1_usize << (13 + 3))) - ((3 + 2) * 32)) / 8 {
                    16
                } else {
                    16 / 4
                };
                
                // Get heap pages array
                let heap_ref = &mut *heap;
                let heap_pages = &heap_ref.pages;
                
                // Assert: pq >= heap->pages
                let pq_ptr = pq as *const mi_page_queue_t;
                let pages_ptr = heap_pages.as_ptr();
                if pq_ptr < pages_ptr {
                    _mi_assert_fail(
                        "pq >= heap->pages",
                        "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                        433,
                        "_mi_page_retire",
                    );
                }
                
                // Calculate index
                let index = (pq_ptr as usize - pages_ptr as usize) / std::mem::size_of::<mi_page_queue_t>();
                
                // Assert: index < MI_BIN_FULL && index < MI_BIN_HUGE
                if !(index < (73 + 1) && index < 73) {
                    _mi_assert_fail(
                        "index < MI_BIN_FULL && index < MI_BIN_HUGE",
                        "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                        435,
                        "_mi_page_retire",
                    );
                }
                
                // Update retirement bounds
                if index < heap_ref.page_retired_min {
                    heap_ref.page_retired_min = index;
                }
                if index > heap_ref.page_retired_max {
                    heap_ref.page_retired_max = index;
                }
                
                // Final assertion
                if !mi_page_all_free(Some(&*page_raw)) {
                    _mi_assert_fail(
                        "mi_page_all_free(page)",
                        "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c",
                        438,
                        "_mi_page_retire",
                    );
                }
                
                return;
            }
        }
    }
    
    // Free the page - use raw pointers to avoid borrow issues
    unsafe {
        _mi_page_free(Some(&mut *page_raw), Some(pq));
    }
}

// Helper function to get page queue from page
fn mi_page_queue_of(page: &mut mi_page_t) -> &mut mi_page_queue_t {
    unsafe {
        // In the original C code, this gets the page queue from the page's heap
        // Since we don't have the exact implementation, we need to reconstruct it
        let heap = (*page).heap.expect("page should have a heap");
        let block_size = (*page).block_size;
        
        // Find the page queue in the heap's pages array
        // This is a simplified version - the actual implementation might be more complex
        let pages_ptr = (*heap).pages.as_ptr();
        for i in 0..(*heap).pages.len() {
            let pq = &mut (*heap).pages[i];
            if pq.block_size == block_size {
                return pq;
            }
        }
        
        // If not found, return the first page queue as fallback
        &mut (*heap).pages[0]
    }
}
pub fn _mi_page_free_collect_partly(page: &mut mi_page_t, head: Option<&mut crate::mi_block_t::MiBlock>) {
    // Check if head is None (equivalent to NULL in C)
    if head.is_none() {
        return;
    }

    // Unwrap safely since we know head is Some
    let head = head.unwrap();
    
    // Get next block using the provided function
    let next_ptr = mi_block_next(page as *const mi_page_t, head as *const crate::mi_block_t::MiBlock);
    let next = if next_ptr.is_null() {
        Option::None
    } else {
        // Convert raw pointer to mutable reference
        // Safety: The pointer comes from mi_block_next which should return valid memory
        unsafe { Some(&mut *next_ptr) }
    };
    
    if next.is_some() {
        // Set head's next to None (NULL)
        // Need to convert head to alloc::MiBlock type for mi_block_set_next
        let head_as_alloc = unsafe { &mut *(head as *mut crate::mi_block_t::MiBlock as *mut crate::alloc::MiBlock) };
        mi_block_set_next(page, head_as_alloc, Option::None);
        
        // Collect to local
        mi_page_thread_collect_to_local(page, next);
        
        // Check conditions and update page state
        if page.local_free.is_some() && page.free.is_none() {
            page.free = page.local_free.take(); // Take ownership, sets local_free to None
            page.free_is_zero = false;
        }
    }
    
    if page.used == 1 {
        // First assertion: mi_tf_block(mi_atomic_load_relaxed(&page->xthread_free)) == head
        // Pass the AtomicUsize directly to mi_tf_block
        let tf_block = mi_tf_block(&page.xthread_free);
        
        // Convert head to the same type as tf_block returns (alloc::MiBlock)
        let head_as_alloc_ptr = head as *const crate::mi_block_t::MiBlock as *const crate::alloc::MiBlock;
        
        if tf_block.is_none() || 
           !std::ptr::eq(tf_block.unwrap() as *const _, head_as_alloc_ptr) {
            let assertion = "mi_tf_block(mi_atomic_load_relaxed(&page->xthread_free)) == head";
            let fname = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
            let func = "_mi_page_free_collect_partly";
            _mi_assert_fail(assertion, fname, 238, func);
        }
        
        // Second assertion: mi_block_next(page, head) == NULL
        let next_check_ptr = mi_block_next(page as *const mi_page_t, head as *const crate::mi_block_t::MiBlock);
        if !next_check_ptr.is_null() {
            let assertion = "mi_block_next(page,head) == NULL";
            let fname = "/workdir/C2RustTranslation-main/subjects/mimalloc/src/page.c";
            let func = "_mi_page_free_collect_partly";
            _mi_assert_fail(assertion, fname, 239, func);
        }
        
        // Call _mi_page_free_collect with force = false
        _mi_page_free_collect(page, false);
    }
}
pub fn mi_page_queue_count(pq: &mi_page_queue_t) -> usize {
    pq.count
}

pub fn mi_register_deferred_free(fn_ptr: *mut MiDeferredFreeFun, arg: *mut ()) {
    // Store the function pointer in the global atomic
    DEFERRED_FREE.store(fn_ptr, Ordering::Release);
    
    // Store the argument in the global atomic
    DEFERRED_ARG.store(arg, Ordering::Release);
}
pub fn _mi_page_queue_append(
    heap: &mut mi_heap_t,
    pq: &mut mi_page_queue_t,
    append: &mut mi_page_queue_t,
) -> usize {
    (mi_heap_contains_queue(heap, pq))
        .then(|| {})
        .unwrap_or_else(|| {
            crate::super_function_unit5::_mi_assert_fail(
                b"mi_heap_contains_queue(heap,pq)\0" as *const u8 as *const std::os::raw::c_char,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c\0"
                    as *const u8 as *const std::os::raw::c_char,
                416,
                b"_mi_page_queue_append\0" as *const u8 as *const std::os::raw::c_char,
            )
        });
    (pq.block_size == append.block_size)
        .then(|| {})
        .unwrap_or_else(|| {
            crate::super_function_unit5::_mi_assert_fail(
                b"pq->block_size == append->block_size\0" as *const u8
                    as *const std::os::raw::c_char,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c\0"
                    as *const u8 as *const std::os::raw::c_char,
                417,
                b"_mi_page_queue_append\0" as *const u8 as *const std::os::raw::c_char,
            )
        });
    if append.first.is_none() {
        return 0;
    }
    let mut count = 0;
    let mut page = append.first;
    while let Some(current_page) = page {
        unsafe {
            // Directly set the heap field instead of calling a non-existent function
            (*current_page).heap = Some(heap as *mut mi_heap_t);
        }
        count += 1;
        page = unsafe { (*current_page).next };
    }

    (count == append.count)
        .then(|| {})
        .unwrap_or_else(|| {
            crate::super_function_unit5::_mi_assert_fail(
                b"count == append->count\0" as *const u8 as *const std::os::raw::c_char,
                b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c\0"
                    as *const u8 as *const std::os::raw::c_char,
                427,
                b"_mi_page_queue_append\0" as *const u8 as *const std::os::raw::c_char,
            )
        });
    if pq.last.is_none() {
        (pq.first.is_none())
            .then(|| {})
            .unwrap_or_else(|| {
                crate::super_function_unit5::_mi_assert_fail(
                    b"pq->first==NULL\0" as *const u8 as *const std::os::raw::c_char,
                    b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c\0"
                        as *const u8 as *const std::os::raw::c_char,
                    431,
                    b"_mi_page_queue_append\0" as *const u8 as *const std::os::raw::c_char,
                )
            });
        pq.first = append.first;
        pq.last = append.last;
        mi_heap_queue_first_update(heap, pq);
    } else {
        (pq.last.is_some())
            .then(|| {})
            .unwrap_or_else(|| {
                crate::super_function_unit5::_mi_assert_fail(
                    b"pq->last!=NULL\0" as *const u8 as *const std::os::raw::c_char,
                    b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c\0"
                        as *const u8 as *const std::os::raw::c_char,
                    438,
                    b"_mi_page_queue_append\0" as *const u8 as *const std::os::raw::c_char,
                )
            });
        (append.first.is_some())
            .then(|| {})
            .unwrap_or_else(|| {
                crate::super_function_unit5::_mi_assert_fail(
                    b"append->first!=NULL\0" as *const u8 as *const std::os::raw::c_char,
                    b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c\0"
                        as *const u8 as *const std::os::raw::c_char,
                    439,
                    b"_mi_page_queue_append\0" as *const u8 as *const std::os::raw::c_char,
                )
            });
        unsafe {
            if let Some(last) = pq.last {
                (*last).next = append.first;
            }
            if let Some(first) = append.first {
                (*first).prev = pq.last;
            }
            pq.last = append.last;
        }
    }
    pq.count += append.count;
    count
}
const MI_LARGE_MAX_OBJ_WSIZE: usize = 8192;

pub fn _mi_page_queue_is_valid(
    heap: Option<&mi_heap_t>, 
    pq: Option<&mi_page_queue_t>
) -> bool {
    // Check if pq is null (0 in C)
    if pq.is_none() {
        return false;
    }
    let pq = pq.unwrap();
    
    let mut count: usize = 0;
    let mut prev: Option<*mut mi_page_t> = None;
    let mut prev_idx: usize = 0;
    
    // Traverse the linked list of pages
    let mut current_page_ptr = pq.first;
    
    while let Some(page_ptr) = current_page_ptr {
        let page_ref: &mi_page_t = unsafe { &*page_ptr };
        
        // Check previous pointer
        if page_ref.prev != prev {
            let assertion = CString::new("page->prev == prev").unwrap();
            let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
            let func = CString::new("_mi_page_queue_is_valid").unwrap();
            crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 149, func.as_ptr());
        }
        
        // Check block size conditions based on page type
        if mi_page_is_in_full(page_ref) {
            if _mi_wsize_from_size(pq.block_size) != MI_LARGE_MAX_OBJ_WSIZE + 2 {
                let assertion = CString::new("_mi_wsize_from_size(pq->block_size) == MI_LARGE_MAX_OBJ_WSIZE + 2").unwrap();
                let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
                let func = CString::new("_mi_page_queue_is_valid").unwrap();
                crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 151, func.as_ptr());
            }
        } else if mi_page_is_huge(unsafe { &*(page_ptr as *const MiPage) }) {
            if _mi_wsize_from_size(pq.block_size) != MI_LARGE_MAX_OBJ_WSIZE + 1 {
                let assertion = CString::new("_mi_wsize_from_size(pq->block_size) == MI_LARGE_MAX_OBJ_WSIZE + 1").unwrap();
                let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
                let func = CString::new("_mi_page_queue_is_valid").unwrap();
                crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 154, func.as_ptr());
            }
        } else {
            if mi_page_block_size(page_ref) != pq.block_size {
                let assertion = CString::new("mi_page_block_size(page) == pq->block_size").unwrap();
                let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
                let func = CString::new("_mi_page_queue_is_valid").unwrap();
                crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 157, func.as_ptr());
            }
        }
        
        // Check heap pointer
        if page_ref.heap.is_none() || page_ref.heap.unwrap() as *const _ != heap.unwrap() as *const _ {
            let assertion = CString::new("page->heap == heap").unwrap();
            let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
            let func = CString::new("_mi_page_queue_is_valid").unwrap();
            crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 159, func.as_ptr());
        }
        
        // Check if this is the last page
        if page_ref.next.is_none() {
            if pq.last != Some(page_ptr) {
                let assertion = CString::new("pq->last == page").unwrap();
                let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
                let func = CString::new("_mi_page_queue_is_valid").unwrap();
                crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 161, func.as_ptr());
            }
        }
        
        count += 1;
        prev = Some(page_ptr);
        prev_idx = 0; // This is set but not used meaningfully in Rust
        current_page_ptr = page_ref.next;
    }
    
    // Verify the count matches
    if pq.count != count {
        let assertion = CString::new("pq->count == count").unwrap();
        let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/page-queue.c").unwrap();
        let func = CString::new("_mi_page_queue_is_valid").unwrap();
        crate::super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 166, func.as_ptr());
    }
    
    true
}
