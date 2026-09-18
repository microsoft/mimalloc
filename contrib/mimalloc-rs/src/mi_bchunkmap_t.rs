use crate::*;
use std::sync::atomic::AtomicUsize;


#[repr(C)]

pub struct mi_bchunk_t {
    pub bfields: [AtomicUsize; 8],
}

pub type mi_bchunkmap_t = mi_bchunk_t;

