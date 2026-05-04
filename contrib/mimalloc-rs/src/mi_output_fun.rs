use crate::*;

/// Function pointer type for output callbacks
pub type MiOutputFun = fn(msg: &str, arg: Option<&mut dyn std::any::Any>);

