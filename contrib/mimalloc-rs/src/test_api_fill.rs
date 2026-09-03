use crate::*;
use std::sync::atomic::AtomicI8;
use std::sync::atomic::Ordering;


pub type int8_t = i8;

pub static mut INT8_T: AtomicI8 = AtomicI8::new(0);

pub fn check_zero_init(p: Option<&[u8]>, size: usize) -> bool {
    // Check if the pointer is None (equivalent to NULL check in C)
    let Some(p) = p else {
        return false;
    };
    
    // Ensure the slice length matches the provided size
    if p.len() != size {
        return false;
    }
    
    // Check if all bytes are zero
    p.iter().all(|&byte| byte == 0)
}
pub fn check_debug_fill_uninit(p: Option<&[u8]>, size: usize) -> bool {
    // Check if p is None (equivalent to checking for NULL in C)
    if p.is_none() {
        return false;
    }
    
    // Unwrap safely: If `p` is `Some`, it will be a valid slice reference
    let p = p.unwrap();
    
    // Ensure the slice length matches the size parameter
    if p.len() != size {
        return false;
    }
    
    // Check if all bytes in the slice are equal to 0xD0
    p.iter().all(|&byte| byte == 0xD0)
}
pub fn check_debug_fill_freed(p: *const u8, size: usize) -> bool {
    // Check if p is null (equivalent to checking for NULL in C)
    if p.is_null() {
        return false;
    }
    
    // Create a slice from the raw pointer for safe iteration
    let slice = unsafe { std::slice::from_raw_parts(p, size) };
    
    // Check if all bytes in the slice are 0xDF
    slice.iter().all(|&byte| byte == 0xDF)
}
