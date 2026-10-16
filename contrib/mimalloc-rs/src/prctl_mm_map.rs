use crate::*;
use crate::__u32;
use crate::__u64;


#[derive(Clone)]
pub struct PrctlMmMap {
    pub start_code: __u64,
    pub end_code: __u64,
    pub start_data: __u64,
    pub end_data: __u64,
    pub start_brk: __u64,
    pub brk: __u64,
    pub start_stack: __u64,
    pub arg_start: __u64,
    pub arg_end: __u64,
    pub env_start: __u64,
    pub env_end: __u64,
    pub auxv: Option<Vec<__u64>>,
    pub auxv_size: __u32,
    pub exe_fd: __u32,
}

