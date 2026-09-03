use crate::*;
use crate::mi_arena_t;
use crate::mi_heaptag_t;
pub type MiClaimFun = fn(
    slice_index: usize,
    arena: Option<&mi_arena_t>,
    heap_tag: mi_heaptag_t,
    keep_set: &mut bool,
) -> bool;

