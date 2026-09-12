use crate::*;

pub type mi_forall_set_fun_t = unsafe extern "C" fn(
    slice_index: usize,
    slice_count: usize,
    arena: *mut std::ffi::c_void,
    arg: *mut ::std::ffi::c_void,
) -> bool;

