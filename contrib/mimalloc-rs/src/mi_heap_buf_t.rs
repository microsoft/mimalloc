use crate::*;

#[derive(Clone)]
pub struct MiHeapBuf {
    pub buf: Option<Vec<u8>>,
    pub size: usize,
    pub used: usize,
    pub can_realloc: bool,
}

