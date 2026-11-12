use crate::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum mi_memkind_t {
    MI_MEM_NONE,
    MI_MEM_EXTERNAL,
    MI_MEM_STATIC,
    MI_MEM_META,
    MI_MEM_OS,
    MI_MEM_OS_HUGE,
    MI_MEM_OS_REMAP,
    MI_MEM_ARENA,
}

