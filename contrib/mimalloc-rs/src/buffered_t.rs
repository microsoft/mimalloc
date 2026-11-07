use crate::*;

/// Buffered output structure for logging/stats printing
#[repr(C)]
pub struct buffered_t {
    /// Output function callback
    pub out: Option<MiOutputFun>,
    /// Argument passed to the output function (as a raw pointer)
    pub arg: *mut std::ffi::c_void,
    /// Buffer for storing formatted output
    pub buf: *mut std::os::raw::c_char,
    /// Number of bytes currently used in the buffer
    pub used: usize,
    /// Total capacity of the buffer
    pub count: usize,
}

