use crate::*;
use crate::mi_bchunk_t::mi_bchunk_t;


#[repr(C)]
pub struct MiBitmap {
    pub chunk_count: std::sync::atomic::AtomicUsize,
    pub _padding: [usize; (((1 << (6 + 3)) / 8) / (1 << 3)) - 1],
    pub chunkmap: mi_bchunk_t,
    pub chunks: [mi_bchunk_t; 64],
}
pub type mi_bitmap_t = MiBitmap;

