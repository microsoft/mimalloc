#![cfg(feature = "override")]

use core::ffi::c_void;
use mimalloc::sys;

unsafe extern "C" {
    fn malloc(size: usize) -> *mut c_void;
    fn free(p: *mut c_void);
}

// without a global allocator, the standard `malloc` should still be redirected to mimalloc
#[test]
fn malloc_is_overridden() {
    unsafe {
        let p = malloc(100);
        assert!(!p.is_null());
        assert!(sys::mi_any_heap_contains(p));
        free(p);
    }
}
