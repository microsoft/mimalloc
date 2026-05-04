use crate::*;

/// Function pointer type for commit operations
pub type MiCommitFun = fn(
    commit: bool,
    start: *mut std::ffi::c_void,
    size: usize,
    is_zero: *mut bool,
    user_arg: *mut std::ffi::c_void,
) -> bool;

