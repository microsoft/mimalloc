use crate::*;

#[repr(C)]
pub struct mi_visit_blocks_args_t {
    pub visit_blocks: bool,
    pub visitor: Option<unsafe extern "C" fn(*const std::ffi::c_void, *const crate::mi_block_visit_fun::mi_heap_area_t, usize, usize, *mut std::ffi::c_void) -> bool>,
    pub arg: *mut std::ffi::c_void,
}

