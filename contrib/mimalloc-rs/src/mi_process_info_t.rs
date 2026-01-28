use crate::*;

pub type mi_msecs_t = i64;

#[derive(Clone, Debug, Default)]
#[repr(C)]
pub struct mi_process_info_t {
    pub elapsed: mi_msecs_t,
    pub utime: mi_msecs_t,
    pub stime: mi_msecs_t,
    pub current_rss: usize,
    pub peak_rss: usize,
    pub current_commit: usize,
    pub peak_commit: usize,
    pub page_faults: usize,
}

