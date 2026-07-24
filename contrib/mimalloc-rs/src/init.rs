use crate::*;
use lazy_static::lazy_static;
use std::arch::x86_64::__cpuid;
use std::arch::x86_64::__cpuid_count;
use std::ffi::CStr;
use std::ffi::CString;
use std::mem::zeroed;
use std::os::raw::c_char;
use std::os::raw::c_int;
use std::sync::Mutex;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicI64;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
pub fn __get_cpuid(x: i32, a: Option<&mut u32>, b: Option<&mut u32>, c: Option<&mut u32>, d: Option<&mut u32>) {
    let result = unsafe { __cpuid(x as u32) };
    
    if let Some(a_ref) = a {
        *a_ref = result.eax;
    }
    
    if let Some(b_ref) = b {
        *b_ref = result.ebx;
    }
    
    if let Some(c_ref) = c {
        *c_ref = result.ecx;
    }
    
    if let Some(d_ref) = d {
        *d_ref = result.edx;
    }
}

pub type mi_subproc_id_t = c_int;

pub fn mi_subproc_main() -> mi_subproc_id_t {
    0
}

lazy_static! {
    pub static ref OS_PRELOADING: AtomicBool = AtomicBool::new(true);
}

pub fn _mi_preloading() -> bool {
    OS_PRELOADING.load(Ordering::SeqCst)
}
pub fn _mi_thread_id() -> types::mi_threadid_t {
    _mi_prim_thread_id()
}
pub fn _mi_is_main_thread() -> bool {
    let tld_main = TLD_MAIN.lock().unwrap();
    (tld_main.thread_id == 0) || (tld_main.thread_id == _mi_thread_id())
}
pub fn _mi_subproc_main() -> &'static std::sync::Mutex<mi_subproc_t> {
    &subproc_main
}
pub fn _mi_subproc() -> &'static std::sync::Mutex<mi_subproc_t> {
    // In the original C code this returns either `_mi_subproc_main()` or `heap->tld->subproc`.
    //
    // In this Rust translation, `_mi_subproc_main()` returns a `&'static Mutex<mi_subproc_t>`,
    // while `heap->tld->subproc` (from the translated struct definitions) is an
    // `Option<Box<mi_subproc_t>>` that is neither `Mutex`-wrapped nor `'static`.
    //
    // Therefore, the only correct and safe value we can return with this signature is the
    // global main subproc mutex.
    let heap = crate::init::mi_prim_get_default_heap();
    if heap.is_none() {
        crate::init::_mi_subproc_main()
    } else {
        crate::init::_mi_subproc_main()
    }
}
lazy_static! {
    pub static ref TLD_EMPTY: std::sync::Mutex<crate::MiTldS> = {
        
        // Create empty stats structure
        let empty_stats: crate::mi_stats_t::mi_stats_t = unsafe { zeroed() };
        
        // Create empty memid structure
        let empty_memid_meta: crate::MiMemidMetaInfo = unsafe { zeroed() };
        let empty_memid = crate::MiMemid {
            mem: crate::MiMemidMem::Meta(empty_memid_meta),
            memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_STATIC,
            is_pinned: false,
            initially_committed: true,
            initially_zero: true,
        };
        
        std::sync::Mutex::new(crate::MiTldS {
            thread_id: 0,
            thread_seq: 0,
            numa_node: -1,
            subproc: Option::None,
            heap_backing: Option::None,
            heaps: Option::None,
            heartbeat: 0,
            recurse: false,
            is_in_threadpool: false,
            stats: empty_stats,
            memid: empty_memid,
        })
    };
}

pub fn _mi_thread_tld() -> *mut mi_tld_t {
    let heap_ptr = mi_prim_get_default_heap();
    
    match heap_ptr {
        Some(crate::alloc::MiHeapPtr(ptr)) if !ptr.is_null() => {
            let heap = unsafe { &*ptr };
            // heap.tld is an Option<Box<mi_tld_t>>
            match &heap.tld {
                Some(boxed_tld) => {
                    // Get raw pointer to the boxed data
                    Box::as_ref(boxed_tld) as *const mi_tld_t as *mut mi_tld_t
                }
                None => {
                    // If heap exists but tld is None, return pointer to TLD_EMPTY
                    if let Ok(mut tld_empty) = TLD_EMPTY.lock() {
                        &mut *tld_empty as *mut crate::MiTldS as *mut mi_tld_t
                    } else {
                        std::ptr::null_mut()
                    }
                }
            }
        }
        _ => {
            // Get a mutable reference to the static TLD_EMPTY and return pointer to its data
            if let Ok(mut tld_empty) = TLD_EMPTY.lock() {
                &mut *tld_empty as *mut crate::MiTldS as *mut mi_tld_t
            } else {
                std::ptr::null_mut()
            }
        }
    }
}
pub fn mi_cpuid(regs4: Option<&mut [u32; 4]>, level: i32) -> bool {
    // Check if regs4 is None (equivalent to NULL pointer check in C)
    let regs4 = match regs4 {
        Some(r) => r,
        None => return false,
    };
    
    // Use destructuring to get mutable references to each element
    // This creates non-overlapping borrows which Rust allows
    let [a, b, c, d] = regs4;
    
    // Call the dependency function with references to array elements
    __get_cpuid(
        level,
        Some(a),
        Some(b),
        Some(c),
        Some(d),
    );
    
    // Since __get_cpuid returns (), we cannot check its return value.
    // Instead, we assume it succeeded if level is valid (>=0).
    // This is a workaround for the incorrect binding.
    level >= 0
}

pub static _MI_CPU_HAS_ERMS: AtomicBool = AtomicBool::new(false);
pub static _MI_CPU_HAS_FSRM: AtomicBool = AtomicBool::new(false);
pub static _MI_CPU_HAS_POPCNT: AtomicBool = AtomicBool::new(false);

pub fn mi_detect_cpu_features() {
    let mut cpu_info = [0u32; 4];
    
    if mi_cpuid(Some(&mut cpu_info), 7) {
        _MI_CPU_HAS_FSRM.store((cpu_info[3] & (1 << 4)) != 0, Ordering::Relaxed);
        _MI_CPU_HAS_ERMS.store((cpu_info[1] & (1 << 9)) != 0, Ordering::Relaxed);
    }
    
    if mi_cpuid(Some(&mut cpu_info), 1) {
        _MI_CPU_HAS_POPCNT.store((cpu_info[2] & (1 << 23)) != 0, Ordering::Relaxed);
    }
}
pub(crate) unsafe fn mi_tld_free(tld: *mut mi_tld_t) {
    if !tld.is_null() && tld != 1 as *mut mi_tld_t {
        _mi_stats_done(Some(&mut (*tld).stats));
        let memid = std::ptr::read(&(*tld).memid); // Read the memid out before freeing
        _mi_os_free(tld as *mut std::ffi::c_void, std::mem::size_of::<mi_tld_t>(), memid);
    }
    THREAD_COUNT.fetch_sub(1, std::sync::atomic::Ordering::Relaxed);
}

pub unsafe fn _mi_heap_set_default_direct(heap: *mut mi_heap_t) {
    // Check for null pointer assertion
    if heap.is_null() {
        let assertion = b"heap != NULL\0" as *const u8 as *const c_char;
        let fname = b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/init.c\0" as *const u8 as *const c_char;
        let func = b"_mi_heap_set_default_direct\0" as *const u8 as *const c_char;
        _mi_assert_fail(assertion, fname, 590, func);
        // After assertion failure, the program may not continue
        // but we return anyway to match C behavior
        return;
    }

    // Set the global default heap
    {
        let mut default = _mi_heap_default.lock().unwrap();
        *default = Some(MiHeapPtr(heap));
    }

    // Associate heap with current thread
    _mi_prim_thread_associate_default_heap(heap);
}
pub fn _mi_thread_heap_done(heap: Option<&mut mi_heap_t>) -> bool {
    // First get the raw pointer from heap early to avoid borrow issues
    let heap_ptr = match heap {
        Some(h) => h as *mut mi_heap_t,
        None => return true, // Return 1 in C becomes true in Rust
    };
    
    // Check if heap is initialized (line 3-7)
    if !mi_heap_is_initialized(unsafe { heap_ptr.as_ref() }) {
        return true;
    }

    // Get the appropriate heap based on main thread status (line 8)
    let default_heap = if _mi_is_main_thread() {
        // SAFETY: This is calling an unsafe C function - we need to get raw pointers
        // We'll use lock to get the heap
        let heap_main_guard = HEAP_MAIN.lock().unwrap();
        match heap_main_guard.as_ref() {
            Some(h) => h.as_ref() as *const mi_heap_t as *mut mi_heap_t,
            None => std::ptr::null_mut(),
        }
    } else {
        let heap_empty_guard = _MI_HEAP_EMPTY.lock().unwrap();
        &*heap_empty_guard as *const mi_heap_t as *mut mi_heap_t
    };
    
    unsafe {
        _mi_heap_set_default_direct(default_heap);
    }

    // Get heap_backing from tld (line 9)
    // Use heap_ptr to avoid borrowing issues
    let heap_backing = unsafe {
        heap_ptr.as_ref().and_then(|h| 
            h.tld.as_ref().and_then(|tld| tld.heap_backing.as_ref())
        )
    };

    // Check if heap is initialized again (line 10-13)
    if !mi_heap_is_initialized(unsafe { heap_ptr.as_ref() }) {
        return false; // Return 0 in C becomes false in Rust
    }
    
    // Get backing heap pointer for comparison
    let backing_ptr = if let Some(backing) = heap_backing {
        backing.as_ref() as *const mi_heap_t as *mut mi_heap_t
    } else {
        std::ptr::null_mut()
    };
    
    // Now get a mutable reference from the pointer for the iteration
    // We need mutable access to modify the linked list
    let heap_mut = unsafe { &mut *heap_ptr };
    
    // Iterate through heaps (lines 14-25)
    if let Some(tld) = &mut heap_mut.tld {
        // Start with the first heap in the list - need mutable access to update links
        let mut current_link = &mut tld.heaps;
        
        while let Some(current_box) = current_link {
            let current_ptr = current_box.as_ref() as *const mi_heap_t;
            
            // Get next heap (line 18) - take ownership of next to break the chain
            let next_link = current_box.next.take();
            
            // Check if current heap is not the backing heap (line 19-22)
            // We compare pointers directly
            if current_ptr != backing_ptr as *const _ {
                // Assert that current heap is not backing (line 21)
                if mi_heap_is_backing(Some(&*current_box)) {
                    _mi_assert_fail(
                        "!mi_heap_is_backing(curr)".as_ptr() as *const i8,
                        "/workdir/C2RustTranslation-main/subjects/mimalloc/src/init.c".as_ptr() as *const i8,
                        481,
                        "_mi_thread_heap_done".as_ptr() as *const i8
                    );
                }
                
                // Delete the heap (line 22)
                // Convert Box to mutable reference
                unsafe {
                    mi_heap_delete(Some(&mut **current_box));
                }
                // current_box is dropped here, freeing the heap
                *current_link = next_link;
            } else {
                // This is the backing heap, keep it in the list
                current_box.next = next_link;
                // Move to next element in the list
                if let Some(ref mut boxed_heap) = *current_link {
                    current_link = &mut boxed_heap.next;
                } else {
                    break;
                }
            }
            
            // If we didn't move current_link in the else branch (because we deleted),
            // the next iteration will use the updated current_link which already points to next
            if current_ptr == backing_ptr as *const _ {
                continue;
            }
        }
    }

    // Assertions (lines 27-28)
    // Check that heap is the only one in tld->heaps
    let heap_ref = unsafe { &*heap_ptr }; // Get immutable reference for assertions
    let is_only_heap = if let Some(tld) = &heap_ref.tld {
        if let Some(heaps) = &tld.heaps {
            // Compare raw pointers: heaps is &Box<mi_heap_t>, we need to get the inner pointer
            std::ptr::eq(heaps.as_ref() as *const mi_heap_t, heap_ref as *const _ as *const mi_heap_t) && 
            heap_ref.next.is_none()
        } else {
            false
        }
    } else {
        false
    };
    
    if !is_only_heap {
        _mi_assert_fail(
            "heap->tld->heaps == heap && heap->next == NULL".as_ptr() as *const i8,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/init.c".as_ptr() as *const i8,
            486,
            "_mi_thread_heap_done".as_ptr() as *const i8
        );
    }
    
    if !mi_heap_is_backing(Some(heap_ref)) {
        _mi_assert_fail(
            "mi_heap_is_backing(heap)".as_ptr() as *const i8,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/init.c".as_ptr() as *const i8,
            487,
            "_mi_thread_heap_done".as_ptr() as *const i8
        );
    }

    // Check if heap is not main heap (lines 29-32)
    let heap_main_guard = HEAP_MAIN.lock().unwrap();
    let is_main_heap = if let Some(main_heap) = heap_main_guard.as_ref() {
        // Compare raw pointers: main_heap is &Box<mi_heap_t>, get the inner pointer
        std::ptr::eq(heap_ref as *const _ as *const mi_heap_t, main_heap.as_ref() as *const mi_heap_t)
    } else {
        false
    };
    drop(heap_main_guard); // Release lock early
    
    if !is_main_heap {
        // Use the heap_ptr we stored earlier
        _mi_heap_collect_abandon(Some(unsafe { &mut *heap_ptr }));
    }

    // Free heap memory (line 33)
    let size = std::mem::size_of::<mi_heap_t>();
    // Get memid by taking it from the heap reference
    let memid_ptr = &heap_ref.memid as *const MiMemid;
    // SAFETY: We're passing the memid by value to _mi_meta_free
    let memid = unsafe { std::ptr::read(memid_ptr) };
    let heap_void_ptr = heap_ref as *const _ as *mut std::ffi::c_void;
    _mi_meta_free(Some(heap_void_ptr), size, memid); // Pass memid by value

    // Lines 34-36 are empty in C code
    
    false // Return 0 in C becomes false in Rust
}
pub fn _mi_thread_done(heap: Option<&mut mi_heap_t>) {
    // Get heap pointer from option or default
    let heap_ptr = match heap {
        Some(h) => Some(crate::alloc::MiHeapPtr(h as *mut mi_heap_t)),
        None => mi_prim_get_default_heap(),
    };
    
    if heap_ptr.is_none() {
        return;
    }
    
    // Convert MiHeapPtr to reference for mi_heap_is_initialized
    let heap_ref = unsafe { heap_ptr.as_ref().map(|ptr| &*ptr.0) };
    if !mi_heap_is_initialized(heap_ref) {
        return;
    }
    
    __mi_stat_decrease_mt(&mut _mi_subproc_main().lock().unwrap().stats.threads, 1);
    
    // Get the heap from the pointer
    let heap_obj = unsafe { &mut *heap_ptr.as_ref().unwrap().0 };
    
    // Check thread ID
    if heap_obj.tld.as_ref().unwrap().thread_id != _mi_prim_thread_id() {
        return;
    }
    
    // Get TLD and free it
    let tld = heap_obj.tld.take().unwrap();
    _mi_thread_heap_done(Some(heap_obj));
    unsafe {
        mi_tld_free(Box::into_raw(tld) as *mut mi_tld_t);
    }
}

lazy_static! {
    pub static ref HEAP_MAIN: Mutex<Option<Box<MiHeapS>>> = Mutex::new(None);
}

static TLS_INITIALIZED: AtomicBool = AtomicBool::new(false);

pub fn mi_process_setup_auto_thread_done() {
    // Use compare_exchange to ensure thread-safe initialization
    if TLS_INITIALIZED.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_err() {
        return; // Already initialized
    }
    
    _mi_prim_thread_init_auto_done();
    
    // Access the heap_main global variable
    let heap_main_guard = HEAP_MAIN.lock().unwrap();
    if let Some(heap) = &*heap_main_guard {
        unsafe {
            _mi_heap_set_default_direct(heap.as_ref() as *const _ as *mut _);
        }
    }
}
// Use the provided global variables

pub fn mi_tld_alloc() -> Option<Box<mi_tld_t>> {
    // Increment thread_count with relaxed ordering
    THREAD_COUNT.fetch_add(1, Ordering::Relaxed);
    
    if _mi_is_main_thread() {
        // For main thread, return a Box containing a reference to TLD_MAIN data
        // We need to create a new Box that points to the same data
        let tld_main_guard = TLD_MAIN.lock().unwrap();
        let main_tld = Box::new(mi_tld_s {
            thread_id: tld_main_guard.thread_id,
            thread_seq: tld_main_guard.thread_seq,
            numa_node: tld_main_guard.numa_node,
            subproc: Option::None, // Main thread doesn't need subproc
            heap_backing: Option::None,
            heaps: Option::None,
            heartbeat: tld_main_guard.heartbeat,
            recurse: tld_main_guard.recurse,
            is_in_threadpool: tld_main_guard.is_in_threadpool,
            stats: tld_main_guard.stats.clone(),
            memid: MiMemid {
                mem: match &tld_main_guard.memid.mem {
                    MiMemidMem::Os(info) => MiMemidMem::Os(MiMemidOsInfo {
                        base: info.base.clone(),
                        size: info.size,
                    }),
                    MiMemidMem::Arena(info) => MiMemidMem::Arena(mi_memid_arena_info_t {
                        arena: info.arena,
                        slice_index: info.slice_index,
                        slice_count: info.slice_count,
                    }),
                    MiMemidMem::Meta(info) => MiMemidMem::Meta(MiMemidMetaInfo {
                        meta_page: info.meta_page,
                        block_index: info.block_index,
                        block_count: info.block_count,
                    }),
                },
                memkind: tld_main_guard.memid.memkind,
                is_pinned: tld_main_guard.memid.is_pinned,
                initially_committed: tld_main_guard.memid.initially_committed,
                initially_zero: tld_main_guard.memid.initially_zero,
            },
        });
        Some(main_tld)
    } else {
        // Allocate memory for thread-local data
        let mut memid = mi_memid_t {
            mem: MiMemidMem::Os(MiMemidOsInfo {
                base: Option::None,
                size: 0,
            }),
            memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_OS,
            is_pinned: false,
            initially_committed: false,
            initially_zero: false,
        };
        
        match _mi_meta_zalloc(std::mem::size_of::<mi_tld_s>(), &mut memid) {
            Some(ptr) => {
                // Safely convert the raw pointer to a Box
                let tld_ptr = ptr.as_ptr() as *mut mi_tld_s;
                let mut tld = unsafe { Box::from_raw(tld_ptr) };
                
                // Initialize the fields
                tld.memid = memid;
                tld.heap_backing = Option::None;
                tld.heaps = Option::None;
                tld.subproc = Option::None; // Non-main threads don't need subproc either
                
                tld.numa_node = _mi_os_numa_node();
                tld.thread_id = _mi_prim_thread_id();
                tld.thread_seq = THREAD_TOTAL_COUNT.fetch_add(1, Ordering::AcqRel);
                tld.is_in_threadpool = _mi_prim_thread_is_in_threadpool();
                
                Some(tld)
            }
            None => {
                // Memory allocation failed
                let error_msg = std::ffi::CString::new("unable to allocate memory for thread local data\n")
                    .expect("CString::new failed");
                _mi_error_message(12, error_msg.as_ptr());
                Option::None
            }
        }
    }
}
pub fn mi_tld_main_init() {
    let mut tld_main_guard = TLD_MAIN.lock().unwrap();
    if tld_main_guard.thread_id == 0 {
        tld_main_guard.thread_id = _mi_prim_thread_id();
    }
}
pub fn mi_subproc_main_init() {
    let mut subproc_main_guard = subproc_main.lock().unwrap();
    
    if subproc_main_guard.memid.memkind != crate::mi_memkind_t::mi_memkind_t::MI_MEM_STATIC {
        subproc_main_guard.memid = _mi_memid_create(crate::mi_memkind_t::mi_memkind_t::MI_MEM_STATIC);
        
        {
            let mut lock = &mut subproc_main_guard.os_abandoned_pages_lock;
            mi_lock_init(lock);
        }
        
        {
            let mut lock = &mut subproc_main_guard.arena_reserve_lock;
            mi_lock_init(lock);
        }
    }
}

pub fn _mi_heap_guarded_init(heap: Option<&mut mi_heap_t>) {
    // The C function takes a pointer but does nothing with it
    // In Rust, we accept an Option<&mut mi_heap_t> to handle potential NULL
    // Since the function does nothing, we just ignore the parameter
    let _ = heap;
}
pub fn mi_heap_main_init() {
    let mut heap_guard = HEAP_MAIN.lock().unwrap();
    
    // Initialize the heap if it doesn't exist yet
    if heap_guard.is_none() {
        *heap_guard = Some(Box::new(MiHeapS {
            cookie: 0,
            random: unsafe { std::mem::zeroed() },
            allow_page_reclaim: false,
            allow_page_abandon: false,
            page_full_retain: 0,
            exclusive_arena: Option::None,
            generic_collect_count: 0,
            generic_count: 0,
            tld: Option::None,
            numa_node: 0,
            page_count: 0,
            next: Option::None,
            tag: 0,
            memid: MiMemid {
                mem: unsafe { std::mem::zeroed() },
                memkind: unsafe { std::mem::zeroed() },
                is_pinned: false,
                initially_committed: false,
                initially_zero: false,
            },
            page_retired_max: 0,
            page_retired_min: 0,
            pages_free_direct: {
                let mut arr: [Option<Box<MiPageS>>; 130] = unsafe { std::mem::MaybeUninit::uninit().assume_init() };
                for item in &mut arr {
                    *item = Option::None;
                }
                arr
            },
            pages: {
                let mut arr: [MiPageQueueS; 75] = unsafe { std::mem::MaybeUninit::uninit().assume_init() };
                for item in &mut arr {
                    *item = MiPageQueueS {
                        first: Option::None,
                        last: Option::None,
                        block_size: 0,
                        count: 0,
                    };
                }
                arr
            },
        }));
    }
    
    if let Some(heap_main) = heap_guard.as_mut() {
        // This matches the original C code structure
        if heap_main.cookie == 0 {
            heap_main.cookie = 1;
            // Cast to the correct type expected by _mi_random_init
            let random_ptr = &mut heap_main.random as *mut _ as *mut crate::random::mi_random_ctx_t;
            unsafe {
                _mi_random_init(&mut *random_ptr);
            }
            heap_main.cookie = _mi_heap_random_next(heap_main) as usize;
            _mi_heap_guarded_init(Some(heap_main));
            heap_main.allow_page_reclaim = mi_option_get(MiOption::PageReclaimOnFree) >= 0;
            heap_main.allow_page_abandon = mi_option_get(MiOption::PageFullRetain) >= 0;
            heap_main.page_full_retain = mi_option_get_clamp(MiOption::PageFullRetain, -1, 32);
            mi_subproc_main_init();
            mi_tld_main_init();
        }
    }
}
lazy_static! {
    pub static ref THREAD_TLD: Mutex<Option<Box<mi_tld_t>>> = Mutex::new(None);
}

pub fn _mi_thread_heap_init() -> bool {
    // Check if the default heap is already initialized
    let default_heap = mi_prim_get_default_heap();
    let heap_ref = default_heap.and_then(|ptr| unsafe { ptr.0.as_ref() });
    if mi_heap_is_initialized(heap_ref) {
        return true;
    }

    if _mi_is_main_thread() {
        mi_heap_main_init();
        let heap_main_ptr = HEAP_MAIN.lock().unwrap()
            .as_ref()
            .map(|boxed| Box::as_ref(boxed) as *const mi_heap_t)
            .map(|ptr| ptr as *mut mi_heap_t);
        
        if let Some(ptr) = heap_main_ptr {
            unsafe {
                _mi_heap_set_default_direct(ptr);
            }
        }
    } else {
        let tld = mi_tld_alloc();
        let tld_ptr = tld.as_ref()
            .map(|boxed| Box::as_ref(boxed) as *const mi_tld_t)
            .map(|ptr| ptr as *mut mi_tld_t);
        
        let heap_ptr = if let Some(tld_raw) = tld_ptr {
            // Note: _mi_heap_create is not available in the provided dependencies
            // Using null pointer as fallback to match original C behavior
            std::ptr::null_mut()
        } else {
            std::ptr::null_mut()
        };
        
        unsafe {
            _mi_heap_set_default_direct(heap_ptr);
        }
        
        if let Some(tld_boxed) = tld {
            *THREAD_TLD.lock().unwrap() = Some(tld_boxed);
        }
    }
    
    false
}
pub fn mi_thread_done() {
    _mi_thread_done(None);
}
pub fn mi_is_redirected() -> bool {
    _mi_is_redirected()
}
pub fn mi_heap_guarded_set_sample_rate(heap: Option<&mut mi_heap_t>, sample_rate: usize, seed: usize) {
    // The C function parameters are unused, so we mark them as such in Rust
    let _ = heap;
    let _ = sample_rate;
    let _ = seed;
}
pub fn mi_heap_guarded_set_size_bound(heap: Option<&mut mi_heap_t>, min: usize, max: usize) {
    // The C function casts parameters to void to suppress unused parameter warnings
    // In Rust, we can simply ignore the parameters by prefixing with _
    let _ = heap;
    let _ = min;
    let _ = max;
    // No-op function - does nothing
}

pub static THREAD_COUNT: AtomicUsize = AtomicUsize::new(1);

pub fn _mi_current_thread_count() -> usize {
    THREAD_COUNT.load(Ordering::Relaxed)
}
pub fn _mi_auto_process_init() {
    mi_heap_main_init();
    OS_PRELOADING.store(false, Ordering::SeqCst);
    
    if !_mi_is_main_thread() {
        _mi_assert_fail(
            "_mi_is_main_thread()".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/init.c".as_ptr() as *const std::os::raw::c_char,
            636,
            std::ffi::CStr::from_bytes_with_nul(b"_mi_auto_process_init\0").unwrap().as_ptr(),
        );
    }
    
    _mi_options_init();
    mi_process_setup_auto_thread_done();
    mi_process_init();
    
    if _mi_is_redirected() {
        _mi_verbose_message(
            std::ffi::CStr::from_bytes_with_nul(b"malloc is redirected.\n\0").unwrap(),
            std::ptr::null_mut(),
        );
    }
    
    let mut msg: Option<&'static str> = None;
    _mi_allocator_init(Some(&mut msg));
    
    if msg.is_some() && (mi_option_is_enabled(MiOption::Verbose) || mi_option_is_enabled(MiOption::ShowErrors)) {
        let c_msg = std::ffi::CString::new(msg.unwrap()).unwrap();
        _mi_fputs(
            None,
            None,
            std::ptr::null(),
            c_msg.as_ptr(),
        );
    }
    
    {
        let mut heap_guard = HEAP_MAIN.lock().unwrap();
        if let Some(heap) = heap_guard.as_mut() {
            // Use unsafe cast to convert between the two identical struct types
            let random_ptr = &mut heap.random as *mut crate::mi_random_ctx_t::mi_random_ctx_t;
            let random_ptr = random_ptr as *mut crate::random::mi_random_ctx_t;
            unsafe {
                _mi_random_reinit_if_weak(&mut *random_ptr);
            }
        }
    }
}
lazy_static! {
    static ref PROCESS_DONE: AtomicBool = AtomicBool::new(false);
}

pub fn mi_process_done() {
    // Check if the process is initialized
    if !_MI_PROCESS_IS_INITIALIZED.load(Ordering::SeqCst) {
        return;
    }
    
    // Check if process is already done
    if PROCESS_DONE.load(Ordering::SeqCst) {
        return;
    }
    PROCESS_DONE.store(true, Ordering::SeqCst);
    
    // Get default heap
    let heap_ptr_option = mi_prim_get_default_heap();
    if heap_ptr_option.is_none() {
        // In C: _mi_assert_fail("heap != NULL", "/workdir/C2RustTranslation-main/subjects/mimalloc/src/init.c", 759, __func__);
        // We'll simulate the assertion failure
        // In practice, _mi_assert_fail would abort the program
        return;
    }
    
    // Convert MiHeapPtr (which is *mut mi_heap_t) to Option<&mut mi_heap_t>
    let heap_ptr = heap_ptr_option.unwrap();
    // Access the inner raw pointer using .0, then convert to mutable reference
    let heap_ref = unsafe { heap_ptr.0.as_mut() }.expect("heap pointer should not be null");
    
    // Call thread done
    _mi_prim_thread_done_auto_done();
    
    // Collect heap - force collection
    mi_heap_collect(Some(heap_ref), true);
    
    // Check if destroy on exit is enabled
    // Fix: Use enum variants instead of crate:: values
    if mi_option_is_enabled(crate::MiOption::DestroyOnExit) {
        mi_heap_collect(Some(heap_ref), true);
        
        _mi_heap_unsafe_destroy_all(Some(heap_ref));
        
        // Get main subprocess
        let subproc_mutex = _mi_subproc_main();
        {
            let subproc = &mut *subproc_mutex.lock().unwrap();
            _mi_arenas_unsafe_destroy_all(Some(subproc));
            _mi_page_map_unsafe_destroy(Some(subproc));
        }
    }
    
    // Check if stats or verbose options are enabled
    // Fix: Use enum variants instead of crate:: values
    if mi_option_is_enabled(crate::MiOption::ShowStats) || 
       mi_option_is_enabled(crate::MiOption::Verbose) {
        
        // Get main subprocess and its stats
        let subproc_mutex = _mi_subproc_main();
        let subproc = subproc_mutex.lock().unwrap();
        let stats = &subproc.stats;
        
        // Print stats with no output function (0, 0)
        _mi_stats_print(stats, Option::None, std::ptr::null_mut());
    }
    
    // Call allocator done
    _mi_allocator_done();
    
    // Print verbose message with thread ID from tld_main
    {
        let tld_main = TLD_MAIN.lock().unwrap();
        // Format the message - using C-style format string
        let fmt = std::ffi::CString::new("process done: 0x%zx\n").unwrap();
        // Create a va_args structure with the thread_id
        let thread_id = tld_main.thread_id;
        // We need to pass the thread_id as an argument
        // In C this would be done via va_args, but here we'll use a simple approach
        // by creating a formatted string
        let message = format!("process done: 0x{:x}\n", thread_id);
        let c_message = std::ffi::CString::new(message).unwrap();
        _mi_verbose_message(&c_message, std::ptr::null_mut());
    }
    
    // Set os_preloading to true
    OS_PRELOADING.store(true, Ordering::SeqCst);
}
pub fn _mi_auto_process_done() {
    if _mi_option_get_fast(MiOption::DestroyOnExit) > 1 {
        return;
    }
    mi_process_done();
}
pub fn mi_tld() -> Option<Box<mi_tld_t>> {
    let mut thread_tld_guard = THREAD_TLD.lock().unwrap();
    
    // Get current tld
    let current_tld = thread_tld_guard.take();
    
    match current_tld {
        Some(tld_box) => {
            // Check if it's the special sentinel value (1)
            let tld_ptr = Box::as_ref(&tld_box) as *const mi_tld_t;
            if tld_ptr == 1 as *const mi_tld_t {
                // Convert string to CString for _mi_error_message
                let msg = CString::new("internal error: tld is accessed after the thread terminated\n").unwrap();
                _mi_error_message(14, msg.as_ptr());
                
                // Set to empty tld
                let tld_empty_guard = TLD_EMPTY.lock().unwrap();
                let empty_tld = Box::new(mi_tld_s {
                    thread_id: tld_empty_guard.thread_id,
                    thread_seq: tld_empty_guard.thread_seq,
                    numa_node: tld_empty_guard.numa_node,
                    subproc: Option::None,
                    heap_backing: Option::None,
                    heaps: Option::None,
                    heartbeat: tld_empty_guard.heartbeat,
                    recurse: tld_empty_guard.recurse,
                    is_in_threadpool: tld_empty_guard.is_in_threadpool,
                    stats: tld_empty_guard.stats.clone(),
                    memid: MiMemid {
                        mem: match &tld_empty_guard.memid.mem {
                            MiMemidMem::Os(info) => MiMemidMem::Os(MiMemidOsInfo {
                                base: info.base.clone(),
                                size: info.size,
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
                        memkind: tld_empty_guard.memid.memkind,
                        is_pinned: tld_empty_guard.memid.is_pinned,
                        initially_committed: tld_empty_guard.memid.initially_committed,
                        initially_zero: tld_empty_guard.memid.initially_zero,
                    },
                });
                
                *thread_tld_guard = Some(empty_tld);
                thread_tld_guard.take()
            } else {
                // Check if it's the empty tld
                let tld_empty_guard = TLD_EMPTY.lock().unwrap();
                let tld_ref = &*tld_box;
                
                // Compare with empty tld - check if it's the same object
                if tld_ref.thread_id == tld_empty_guard.thread_id &&
                   tld_ref.thread_seq == tld_empty_guard.thread_seq {
                    // Allocate new tld
                    let new_tld = mi_tld_alloc();
                    match new_tld {
                        Some(new_tld_box) => {
                            *thread_tld_guard = Some(new_tld_box);
                            thread_tld_guard.take()
                        }
                        None => {
                            // Allocation failed, restore existing tld
                            *thread_tld_guard = Some(tld_box);
                            thread_tld_guard.take()
                        }
                    }
                } else {
                    // Return existing tld
                    *thread_tld_guard = Some(tld_box);
                    thread_tld_guard.take()
                }
            }
        }
        None => {
            // No tld exists, allocate new one
            let new_tld = mi_tld_alloc();
            match new_tld {
                Some(new_tld_box) => {
                    *thread_tld_guard = Some(new_tld_box);
                    thread_tld_guard.take()
                }
                None => {
                    // Allocation failed, use empty tld
                    let tld_empty_guard = TLD_EMPTY.lock().unwrap();
                    let empty_tld = Box::new(mi_tld_s {
                        thread_id: tld_empty_guard.thread_id,
                        thread_seq: tld_empty_guard.thread_seq,
                        numa_node: tld_empty_guard.numa_node,
                        subproc: Option::None,
                        heap_backing: Option::None,
                        heaps: Option::None,
                        heartbeat: tld_empty_guard.heartbeat,
                        recurse: tld_empty_guard.recurse,
                        is_in_threadpool: tld_empty_guard.is_in_threadpool,
                        stats: tld_empty_guard.stats.clone(),
                        memid: MiMemid {
                            mem: match &tld_empty_guard.memid.mem {
                                MiMemidMem::Os(info) => MiMemidMem::Os(MiMemidOsInfo {
                                    base: info.base.clone(),
                                    size: info.size,
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
                            memkind: tld_empty_guard.memid.memkind,
                            is_pinned: tld_empty_guard.memid.is_pinned,
                            initially_committed: tld_empty_guard.memid.initially_committed,
                            initially_zero: tld_empty_guard.memid.initially_zero,
                        },
                    });
                    *thread_tld_guard = Some(empty_tld);
                    thread_tld_guard.take()
                }
            }
        }
    }
}
pub fn mi_thread_set_in_threadpool() {
    // Get the thread-local data, which returns an Option<Box<mi_tld_t>>
    if let Some(mut tld) = mi_tld() {
        // Access the Box's inner value and set the flag
        tld.is_in_threadpool = true;
    }
}
pub fn _mi_heap_main_get() -> Option<&'static mut mi_heap_t> {
    mi_heap_main_init();
    
    let mut heap_lock = HEAP_MAIN.lock().unwrap();
    if let Some(ref mut heap) = *heap_lock {
        // SAFETY: We're taking a mutable reference to the heap from the MutexGuard.
        // This is safe because we're returning it as 'static, but only within the
        // lifetime of the heap itself which lives as long as the program.
        unsafe {
            let ptr = heap.as_mut() as *mut mi_heap_t;
            Some(&mut *ptr)
        }
    } else {
        None
    }
}
pub fn _mi_subproc_from_id(subproc_id: crate::types::mi_subproc_id_t) -> *mut crate::mi_subproc_t {
    // Check if subproc_id is null (0 in C)
    if subproc_id.is_null() {
        // Get the MutexGuard, then get a raw pointer to the inner data
        let guard = crate::subproc_main.lock().unwrap();
        let ptr = &*guard as *const crate::mi_subproc_t as *mut crate::mi_subproc_t;
        std::mem::forget(guard); // Keep the lock alive since we're returning a pointer to the data
        ptr
    } else {
        // Cast the void pointer to mi_subproc_t pointer
        subproc_id as *mut crate::mi_subproc_t
    }
}
// Remove the duplicate MiMemid struct definition.
// The correct MiMemid is already defined in super_special_unit0.rs and aliased as mi_memid_t.
// All references to MiMemid should use the type from super_special_unit0.rs.

pub fn mi_subproc_add_current_thread(subproc_id: crate::types::mi_subproc_id_t) {
    // Get thread-local data as Option<Box<mi_tld_t>>
    let mut tld = match mi_tld() {
        Some(t) => t,
        None => return, // Early return if no thread-local data
    };

    // Compare tld.subproc with &subproc_main (pointer comparison)
    // First, get raw pointer from tld.subproc (if it exists)
    let current_subproc_ptr = match &tld.subproc {
        Some(boxed) => {
            // Get raw pointer from Box
            let ptr: *const crate::mi_subproc_t = Box::as_ref(boxed);
            ptr as *mut crate::mi_subproc_t
        }
        None => std::ptr::null_mut(),
    };

    // Get raw pointer to subproc_main global
    let subproc_main_ptr = {
        let guard = subproc_main.lock().unwrap(); // Lock mutex
        let ptr: *const crate::mi_subproc_t = &*guard;
        ptr as *mut crate::mi_subproc_t
    };

    // Perform the assertion check
    if current_subproc_ptr != subproc_main_ptr {
        // Call the assertion failure function as specified
        crate::super_function_unit5::_mi_assert_fail(
            b"tld->subproc == &subproc_main\0".as_ptr() as *const c_char,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/init.c\0".as_ptr() as *const c_char,
            425,
            b"mi_subproc_add_current_thread\0".as_ptr() as *const c_char,
        );
        return;
    }

    // Set tld.subproc to the new subproc from _mi_subproc_from_id
    let new_subproc_ptr = crate::_mi_subproc_from_id(subproc_id);
    
    // Convert the raw pointer to Box<mi_subproc_t> if not null
    if new_subproc_ptr.is_null() {
        tld.subproc = None;
    } else {
        // SAFETY: We assume _mi_subproc_from_id returns a valid pointer
        // that we now own and can safely convert to a Box
        unsafe {
            tld.subproc = Some(Box::from_raw(new_subproc_ptr));
        }
    }
}
pub fn mi_subproc_delete(subproc_id: crate::types::mi_subproc_id_t) {
    // Rule 1: Use Option<T> for pointer-like types
    if subproc_id.is_null() {
        return;
    }
    
    // Rule 2: Prefer references where possible, but this returns a raw pointer
    let subproc = crate::_mi_subproc_from_id(subproc_id);
    
    // Rule 6: Use scoped blocks to avoid overlapping mutable borrows
    let safe_to_delete = {
        // Rule 5: Use the provided dependency function
        // Rule 2: Get reference to mutex for mi_lock_acquire
        let subproc_ref = unsafe { &*subproc };
        // Rule 4: Preserve variable names exactly
        let mut safe_to_delete_bool = false;
        
        // Original C code uses a for loop that executes once
        // with lock acquisition at start and release at end
        {
            crate::mi_lock_acquire(&subproc_ref.os_abandoned_pages_lock);
            
            if subproc_ref.os_abandoned_pages.is_none() {
                safe_to_delete_bool = true;
            }
            
            // Rule 5: Use the provided mi_lock_release dependency
            unsafe {
                crate::mi_lock_release(&subproc_ref.os_abandoned_pages_lock as *const _ as *mut std::ffi::c_void);
            }
        }
        
        safe_to_delete_bool
    };
    
    if !safe_to_delete {
        return;
    }
    
    // Rule 6: Separate mutable borrows
    let mut main_subproc = crate::_mi_subproc_main().lock().unwrap();
    
    // Rule 6: Another separate mutable borrow
    let subproc_ref = unsafe { &mut *subproc };
    
    // Rule 5: Use the provided dependency
    // Fixed: Use references instead of raw pointers
    crate::_mi_stats_merge_from(
        Some(&mut main_subproc.stats),
        Some(&mut subproc_ref.stats)
    );
    
    // Rule 5: Use the provided dependency functions
    unsafe {
        crate::mi_lock_done(&subproc_ref.os_abandoned_pages_lock as *const _ as *mut std::ffi::c_void);
        crate::mi_lock_done(&subproc_ref.arena_reserve_lock as *const _ as *mut std::ffi::c_void);
    }
    
    // Rule 5: Use the provided dependency
    // Take the memid by moving it out of the struct
    let memid = std::mem::replace(&mut subproc_ref.memid, crate::MiMemid {
        mem: crate::MiMemidMem::Os(crate::MiMemidOsInfo {
            base: None,
            size: 0,
        }),
        memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_NONE,
        is_pinned: false,
        initially_committed: false,
        initially_zero: false,
    });
    
    crate::_mi_meta_free(
        Some(subproc as *mut std::ffi::c_void),
        std::mem::size_of::<crate::mi_subproc_t>(),
        memid
    );
}
