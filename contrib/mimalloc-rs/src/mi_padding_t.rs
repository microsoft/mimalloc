use crate::*;

#[repr(C)]
pub struct mi_padding_t {
    pub canary: u32,
    pub delta: u32,
}

