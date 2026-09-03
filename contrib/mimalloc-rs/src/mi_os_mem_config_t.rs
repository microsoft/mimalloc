use crate::*;

#[derive(Clone)]
pub struct MiOsMemConfig {
    pub page_size: usize,
    pub large_page_size: usize,
    pub alloc_granularity: usize,
    pub physical_memory_in_kib: usize,
    pub virtual_address_bits: usize,
    pub has_overcommit: bool,
    pub has_partial_free: bool,
    pub has_virtual_reserve: bool,
}

