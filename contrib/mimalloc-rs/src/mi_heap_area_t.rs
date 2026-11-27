use crate::*;

#[derive(Clone)]
pub struct mi_heap_area_t {
    pub blocks: Option<Vec<u8>>,
    pub reserved: usize,
    pub committed: usize,
    pub used: usize,
    pub block_size: usize,
    pub full_block_size: usize,
    pub heap_tag: i32,
}

