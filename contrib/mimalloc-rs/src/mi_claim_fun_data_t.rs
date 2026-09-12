use crate::*;


pub struct mi_claim_fun_data_s {
    pub arena: Option<Box<mi_arena_t>>,
    pub heap_tag: mi_heaptag_t,
}

pub type mi_claim_fun_data_t = mi_claim_fun_data_s;

