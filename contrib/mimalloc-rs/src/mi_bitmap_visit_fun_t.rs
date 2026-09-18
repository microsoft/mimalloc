use crate::*;

pub type mi_bitmap_visit_fun_t = Option<unsafe extern "C" fn(*mut crate::mi_bchunkmap_t::mi_bchunkmap_t, usize, usize, *mut usize, *mut core::ffi::c_void, *mut core::ffi::c_void) -> bool>;

