use crate::*;

#[derive(Clone)]
pub struct Sysinfo {
    pub uptime: __kernel_long_t,
    pub loads: [__kernel_ulong_t; 3],
    pub totalram: __kernel_ulong_t,
    pub freeram: __kernel_ulong_t,
    pub sharedram: __kernel_ulong_t,
    pub bufferram: __kernel_ulong_t,
    pub totalswap: __kernel_ulong_t,
    pub freeswap: __kernel_ulong_t,
    pub procs: __u16,
    pub pad: __u16,
    pub totalhigh: __kernel_ulong_t,
    pub freehigh: __kernel_ulong_t,
    pub mem_unit: __u32,
    pub _f: [u8; (20 - (2 * (std::mem::size_of::<__kernel_ulong_t>()))) - (std::mem::size_of::<__u32>())],
}

