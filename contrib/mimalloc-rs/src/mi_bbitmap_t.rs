use crate::*;

#[repr(C)]
pub struct mi_bbitmap_t {
    pub chunk_count: std::sync::atomic::AtomicUsize,
    pub chunk_max_accessed: std::sync::atomic::AtomicUsize,
    pub _padding: [usize; 6], // (((1 << (6 + 3)) / 8) / (1 << 3)) - 2 = (512 / 8) / 8 - 2 = 64 / 8 - 2 = 6
    pub chunkmap: crate::mi_bchunkmap_t::mi_bchunkmap_t,
    pub chunkmap_bins: [crate::mi_bchunkmap_t::mi_bchunkmap_t; 10], // MI_CBIN_COUNT - 1, assuming MI_CBIN_COUNT = 11
    pub chunks: [crate::mi_bchunk_t::mi_bchunk_t; 64],
}

