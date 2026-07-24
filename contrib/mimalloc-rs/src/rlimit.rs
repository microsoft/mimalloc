use crate::*;

#[repr(C)]
#[derive(Clone, Debug, Copy, PartialEq, Eq)]
pub struct Rlimit {
    pub rlim_cur: rlim_t,
    pub rlim_max: rlim_t,
}

