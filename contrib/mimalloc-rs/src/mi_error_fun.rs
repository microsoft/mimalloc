use crate::*;

/// Function pointer type for error handling callbacks
pub type mi_error_fun = fn(err: i32, arg: Option<&mut ()>);

