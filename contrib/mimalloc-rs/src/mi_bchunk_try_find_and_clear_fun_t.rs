use crate::*;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;


pub type mi_bchunk_try_find_and_clear_fun_t = fn(chunk: &mi_bchunk_t, n: usize, idx: &mut usize) -> bool;

#[repr(C)]
pub struct mi_bchunk_t {
    pub bfields: [AtomicUsize; 8],
}

