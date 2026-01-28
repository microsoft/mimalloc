use crate::*;

#[derive(Clone)]
pub struct mi_random_ctx_t {
    pub input: [u32; 16],
    pub output: [u32; 16],
    pub output_available: i32,
    pub weak: bool,
}

