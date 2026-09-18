use crate::*;

#[derive(Clone)]
pub struct __kernel_fd_set {
    pub fds_bits: [u64; 1024 / (8 * std::mem::size_of::<u64>())],
}

