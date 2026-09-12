use crate::*;
use std::ffi::c_void;


pub struct MiHeapAreaExT {
    pub page: Option<Box<mi_page_t>>,
    pub area: crate::mi_heap_area_t::mi_heap_area_t,
}

pub type mi_heap_area_ex_t = MiHeapAreaExT;

pub type mi_heap_area_visit_fun = fn(
    heap: Option<&crate::MiHeapS>,
    area: Option<&mi_heap_area_ex_t>,
    arg: Option<&c_void>,
) -> bool;

