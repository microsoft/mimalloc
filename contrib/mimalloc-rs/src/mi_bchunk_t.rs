use crate::*;

#[repr(C)]
pub struct mi_bchunk_t {
    pub bfields: [std::sync::atomic::AtomicUsize; 8], // 8 elements: (1 << (6 + 3)) / (1 << (3 + 3)) = 512 / 64 = 8
}

