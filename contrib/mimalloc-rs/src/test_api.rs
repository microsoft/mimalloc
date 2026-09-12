use crate::*;
use std::ffi::CStr;
use std::ptr;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::AtomicI8;
use std::sync::atomic::Ordering;

pub fn test_stl_allocator1() -> bool {
    true
}

pub fn test_stl_allocator2() -> bool {
    true
}

pub fn test_stl_heap_allocator1() -> bool {
    true
}

pub fn test_stl_heap_allocator2() -> bool {
    true
}

pub fn test_stl_heap_allocator3() -> bool {
    true
}

pub fn test_stl_heap_allocator4() -> bool {
    true
}

pub type int8_t = i8;

pub static mut INT8_T: AtomicI8 = AtomicI8::new(0);

pub fn mem_is_zero(p: Option<&[u8]>, size: usize) -> bool {
    // Check if the pointer is None (equivalent to NULL in C)
    let Some(p) = p else {
        return false;
    };
    
    // Check if the slice length matches the expected size
    if p.len() != size {
        return false;
    }
    
    // Iterate through the slice and check if all bytes are zero
    for &byte in p {
        if byte != 0 {
            return false;
        }
    }
    
    true
}

pub static FAILED: AtomicI32 = AtomicI32::new(0);
pub static OK: AtomicI32 = AtomicI32::new(0);

pub fn check_result(result: bool, testname: &str, fname: &str, lineno: i64) -> bool {
    if !result {
        FAILED.fetch_add(1, Ordering::SeqCst);
        eprintln!("\n  FAILED: {}: {}:{}", testname, fname, lineno);
    } else {
        OK.fetch_add(1, Ordering::SeqCst);
        eprintln!("ok.");
    }
    true
}
pub fn print_test_summary() -> i32 {
    eprintln!(
        "\n\n---------------------------------------------\nsucceeded: {}\nfailed   : {}\n",
        OK.load(Ordering::Relaxed),
        FAILED.load(Ordering::Relaxed)
    );
    FAILED.load(Ordering::Relaxed)
}
pub fn test_heap1() -> bool {
    // Create a new heap - returns Option<Box<mi_heap_t>> per dependency
    let mut heap_box = match mi_heap_new() {
        Some(heap) => heap,
        None => return false,
    };
    
    // Get raw pointer for unsafe C function
    let heap_ptr = Box::as_mut(&mut heap_box);
    
    unsafe {
        // Allocate memory for two integers
        let p1 = mi_heap_malloc(heap_ptr, std::mem::size_of::<i32>()) as *mut i32;
        let p2 = mi_heap_malloc(heap_ptr, std::mem::size_of::<i32>()) as *mut i32;
        
        // Check allocations succeeded
        if p1.is_null() || p2.is_null() {
            // Destroy the heap before returning
            mi_heap_destroy(Some(heap_ptr));
            return false;
        }
        
        // Assign values - same as C: *p1 = (*p2 = 43)
        *p2 = 43;
        *p1 = 43;
        
        // Destroy the heap
        mi_heap_destroy(Some(heap_ptr));
    }
    
    // heap_box will be dropped here, but the heap is already destroyed
    // We need to prevent double-free
    std::mem::forget(heap_box);
    
    true
}
pub fn test_heap2() -> bool {
    // Create a new heap
    let mut heap = match mi_heap_new() {
        Some(h) => h,
        None => return false,
    };
    
    // Allocate two integers on the heap
    let p1 = unsafe {
        mi_heap_malloc(
            &mut *heap as *mut crate::super_special_unit0::mi_heap_t,
            std::mem::size_of::<i32>()
        ) as *mut i32
    };
    
    let p2 = unsafe {
        mi_heap_malloc(
            &mut *heap as *mut crate::super_special_unit0::mi_heap_t,
            std::mem::size_of::<i32>()
        ) as *mut i32
    };
    
    // Delete the heap (this invalidates p1 and p2)
    mi_heap_delete(Some(&mut *heap));
    
    // Write to invalid pointer (undefined behavior in C)
    unsafe {
        *p1 = 42;
    }
    
    // Free the invalid pointers
    mi_free(Some(p1 as *mut std::ffi::c_void));
    mi_free(Some(p2 as *mut std::ffi::c_void));
    
    true
}
