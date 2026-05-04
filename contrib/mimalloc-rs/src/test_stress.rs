use crate::*;
use libc::pthread_t;
use libc;
use std::ffi::c_void;
use std::process::abort;
use std::sync::Mutex;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::AtomicU64;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
pub fn atomic_exchange_ptr(p: &AtomicPtr<()>, newval: *mut ()) -> *mut () {
    p.swap(newval, Ordering::SeqCst)
}

pub type random_t = AtomicUsize;

pub fn pick(r: &random_t) -> usize {
    let mut x = r.load(Ordering::Relaxed);
    x ^= x >> 16;
    x = x.wrapping_mul(0x7feb352d);
    x ^= x >> 15;
    x = x.wrapping_mul(0x846ca68b);
    x ^= x >> 16;
    r.store(x, Ordering::Relaxed);
    x
}

type iptr = isize;

static THREAD_ENTRY_FUN_ATOMIC: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());

pub fn thread_entry(param: Option<*mut ()>) -> Option<*mut ()> {
    let param_ptr = param?;
    
    // Convert the raw pointer to iptr (isize)
    let param_value = param_ptr as iptr;
    
    // Get the function pointer from the atomic
    let fun_ptr = THREAD_ENTRY_FUN_ATOMIC.load(Ordering::Acquire);
    
    if fun_ptr.is_null() {
        return None;
    }
    
    // SAFETY: We assume the function pointer stored in THREAD_ENTRY_FUN_ATOMIC
    // is valid and has the correct signature (fn(iptr))
    unsafe {
        let fun: fn(iptr) = std::mem::transmute(fun_ptr);
        fun(param_value);
    }
    
    Some(std::ptr::null_mut())
}
pub fn chance(perc: usize, r: &random_t) -> bool {
    (pick(r) % 100) <= perc
}
pub const COOKIE: AtomicU64 = AtomicU64::new(0x1ce4e5b9);

pub fn free_items(p: Option<*mut c_void>) {
    if let Some(p) = p {
        let q = p as *mut u64;
        let items = unsafe { *q.offset(0) } ^ COOKIE.load(Ordering::Relaxed);
        
        for i in 0..items {
            let value = unsafe { *q.offset(i as isize) } ^ COOKIE.load(Ordering::Relaxed);
            if value != (items - i) {
                eprintln!("memory corruption at block {:p} at {}", p, i);
                abort();
            }
        }
        
        // Call the mi_free function from dependencies
        mi_free(Some(p));
    } else {
        // If p is None, call mi_free with None
        mi_free(None);
    }
}
// Add libc import since it's used in this module

// Global variables from dependencies
pub static MAIN_PARTICIPATES: AtomicBool = AtomicBool::new(false);
lazy_static::lazy_static! {
    pub static ref THREAD_ENTRY_FUN: Mutex<Option<Box<dyn Fn(iptr) + Send + Sync>>> = 
        Mutex::new(Option::None);
}

// Wrapper function to convert the C thread entry signature to the Rust thread_entry function.
extern "C" fn thread_entry_wrapper(param: *mut c_void) -> *mut c_void {
    // Call the Rust thread_entry function, converting the param to Option<*mut ()>
    let result = thread_entry(Some(param as *mut ()));
    // Convert the result to *mut c_void (or null)
    result.map(|p| p as *mut c_void).unwrap_or(std::ptr::null_mut())
}

pub fn run_os_threads(nthreads: usize, fun: Option<Box<dyn Fn(iptr) + Send + Sync>>) {
    // Store the function pointer in the global variable (even if None, to match C behavior)
    *THREAD_ENTRY_FUN.lock().unwrap() = fun;
    
    // Allocate threads array using mi_calloc - store as raw pointer to pthread_t equivalent
    let threads_ptr = unsafe { 
        mi_calloc(nthreads, std::mem::size_of::<pthread_t>()) as *mut pthread_t 
    };
    if threads_ptr.is_null() {
        return;
    }
    
    // Initialize the array to zero (mimicking memset in original C code)
    unsafe {
        std::ptr::write_bytes(
            threads_ptr as *mut u8, 
            0, 
            nthreads * std::mem::size_of::<pthread_t>()
        );
    }
    
    // Create threads
    let start = if MAIN_PARTICIPATES.load(Ordering::SeqCst) { 1 } else { 0 };
    
    for i in start..nthreads {
        let thread_ptr_i = unsafe { threads_ptr.add(i) };
        let param = i as *mut c_void;
        
        // Create thread using pthread_create
        let result = unsafe {
            libc::pthread_create(
                thread_ptr_i,
                std::ptr::null(),
                thread_entry_wrapper,  // Pass function pointer directly, not wrapped in Some()
                param,
            )
        };
        
        if result != 0 {
            // Handle thread creation error if needed
        }
    }
    
    // Main thread participates if needed
    if MAIN_PARTICIPATES.load(Ordering::SeqCst) {
        if let Some(ref f) = *THREAD_ENTRY_FUN.lock().unwrap() {
            f(0);
        }
    }
    
    // Join all threads
    for i in start..nthreads {
        let thread_ptr_i = unsafe { threads_ptr.add(i) };
        
        unsafe {
            libc::pthread_join(*thread_ptr_i, std::ptr::null_mut());
        }
    }
    
    // Free the allocated memory
    unsafe {
        crate::mi_free(Some(threads_ptr as *mut c_void));
    }
}
pub fn alloc_items(items: usize, r: &AtomicUsize) -> Option<Vec<u64>> {
    let mut items = items;
    let allow_large_objects = crate::ALLOW_LARGE_OBJECTS.load(Ordering::Relaxed);
    
    if crate::chance(1, r) {
        if crate::chance(1, r) && allow_large_objects {
            items *= 10000;
        } else if crate::chance(10, r) && allow_large_objects {
            items *= 1000;
        } else {
            items *= 100;
        }
    }
    
    if (32..=40).contains(&items) {
        items *= 2;
    }
    
    let use_one_size = crate::USE_ONE_SIZE.load(Ordering::Relaxed);
    if use_one_size > 0 {
        items = use_one_size / std::mem::size_of::<u64>();
    }
    
    if items == 0 {
        items = 1;
    }
    
    let cookie = crate::globals::COOKIE.load(Ordering::Relaxed);
    
    unsafe {
        let p = crate::mi_calloc(items, std::mem::size_of::<u64>());
        if p.is_null() {
            return None;
        }
        
        let p_slice = std::slice::from_raw_parts_mut(p as *mut u64, items);
        
        for i in 0..items {
            assert_eq!(p_slice[i], 0);
            p_slice[i] = (items - i) as u64 ^ cookie;
        }
        
        Some(Vec::from_raw_parts(p as *mut u64, items, items))
    }
}
pub fn stress(tid: isize) {
    let mut r = AtomicUsize::new(((tid + 1) * 43) as usize);
    let max_item_shift = 5;
    let max_item_retained_shift = max_item_shift + 2;
    let allocs = (100 * (crate::SCALE.load(Ordering::Relaxed) as usize)) * (((tid % 8) + 1) as usize);
    let retain = allocs / 2;
    let mut data: Vec<Option<*mut std::ffi::c_void>> = Vec::new();
    let mut data_idx = 0;
    let mut data_size = 0;
    let mut data_top = 0;
    
    let mut retained: Vec<Option<*mut std::ffi::c_void>> = {
        let ptr = crate::mi_calloc(retain, std::mem::size_of::<*mut std::ffi::c_void>());
        if ptr.is_null() {
            Vec::new()
        } else {
            unsafe {
                Vec::from_raw_parts(
                    ptr as *mut Option<*mut std::ffi::c_void>,
                    retain,
                    retain,
                )
            }
        }
    };
    let mut retain_top = 0;
    
    let mut allocs_remaining = allocs;
    let mut retain_remaining = retain;
    
    while (allocs_remaining > 0) || (retain_remaining > 0) {
        if (retain_remaining == 0) || (crate::chance(50, &r) && (allocs_remaining > 0)) {
            allocs_remaining -= 1;
            if data_top >= data_size {
                data_size += 100000;
                let new_capacity = data_size;
                let new_ptr = crate::mi_realloc(
                    if data_idx == 0 { Option::None } else { Some(data_idx as *mut std::ffi::c_void) },
                    new_capacity * std::mem::size_of::<*mut std::ffi::c_void>()
                );
                
                if let Some(new_ptr) = new_ptr {
                    data_idx = new_ptr as usize;
                    unsafe {
                        let slice = std::slice::from_raw_parts_mut(
                            new_ptr as *mut Option<*mut std::ffi::c_void>,
                            new_capacity
                        );
                        for i in data_top..new_capacity {
                            slice[i] = Option::None;
                        }
                    }
                }
            }
            
            if data_idx != 0 {
                unsafe {
                    let slice = std::slice::from_raw_parts_mut(
                        data_idx as *mut Option<*mut std::ffi::c_void>,
                        data_size
                    );
                    let item_size = 1 << (crate::pick(&r) % max_item_shift);
                    slice[data_top] = crate::alloc_items(item_size, &r).map(|v| Box::into_raw(v.into_boxed_slice()) as *mut std::ffi::c_void);
                }
                data_top += 1;
            }
        } else {
            if retain_top < retained.len() {
                let item_size = 1 << (crate::pick(&r) % max_item_retained_shift);
                retained[retain_top] = crate::alloc_items(item_size, &r).map(|v| Box::into_raw(v.into_boxed_slice()) as *mut std::ffi::c_void);
                retain_top += 1;
                retain_remaining -= 1;
            }
        }
        
        if crate::chance(66, &r) && (data_top > 0) && (data_idx != 0) {
            let idx = crate::pick(&r) % data_top;
            unsafe {
                let slice = std::slice::from_raw_parts_mut(
                    data_idx as *mut Option<*mut std::ffi::c_void>,
                    data_size
                );
                crate::free_items(slice[idx]);
                slice[idx] = Option::None;
            }
        }
        
        if crate::chance(25, &r) && (data_top > 0) && (data_idx != 0) {
            let idx = crate::pick(&r) % data_top;
            let transfer_idx = crate::pick(&r) % 1000;
            
            unsafe {
                let slice = std::slice::from_raw_parts_mut(
                    data_idx as *mut Option<*mut std::ffi::c_void>,
                    data_size
                );
                let p = slice[idx];
                // Convert *mut c_void to *mut () for atomic_exchange_ptr
                let p_ptr = p.unwrap_or(std::ptr::null_mut()) as *mut ();
                let q = crate::atomic_exchange_ptr(&crate::TRANSFER[transfer_idx], p_ptr);
                // Convert *mut () back to *mut c_void
                slice[idx] = if q.is_null() { Option::None } else { Some(q as *mut std::ffi::c_void) };
            }
        }
    }
    
    for i in 0..retain_top {
        crate::free_items(retained[i]);
    }
    
    if data_idx != 0 {
        for i in 0..data_top {
            unsafe {
                let slice = std::slice::from_raw_parts(
                    data_idx as *const Option<*mut std::ffi::c_void>,
                    data_size
                );
                crate::free_items(slice[i]);
            }
        }
    }
    
    if !retained.is_empty() {
        let ptr = retained.as_mut_ptr() as *mut std::ffi::c_void;
        std::mem::forget(retained);
        crate::mi_free(Some(ptr));
    }
    
    if data_idx != 0 {
        crate::mi_free(Some(data_idx as *mut std::ffi::c_void));
    }
}
pub fn test_stress() {
    // Simple pseudo-random number generator (like original C's rand())
    let mut r = unsafe { libc::rand() } as usize;
    let r_atomic = std::sync::atomic::AtomicUsize::new(r);
    
    let iter = crate::ITER.load(Ordering::Relaxed);
    let threads = crate::THREADS.load(Ordering::Relaxed);
    
    for n in 0..iter {
        crate::run_os_threads(threads as usize, Some(Box::new(crate::stress)));
        
        for i in 0..1000 {
            // Update the atomic with current r value
            r_atomic.store(r, Ordering::Relaxed);
            if crate::chance(50, &r_atomic) || ((n + 1) == iter) {
                let p = crate::atomic_exchange_ptr(&crate::TRANSFER[i], std::ptr::null_mut());
                crate::free_items(Some(p as *mut c_void));
            }
            // Update r for next iteration (simple LCG)
            r = r.wrapping_mul(1103515245).wrapping_add(12345);
        }
        
        if ((n + 1) % 10) == 0 {
            println!("- iterations left: {:3}", iter - (n + 1));
            crate::mi_debug_show_arenas();
        }
    }
    
    for i in 0..1000 {
        let p = crate::atomic_exchange_ptr(&crate::TRANSFER[i], std::ptr::null_mut());
        if !p.is_null() {
            crate::free_items(Some(p as *mut c_void));
        }
    }
}
