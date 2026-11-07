use crate::*;


pub struct __kernel_sighandler_t(pub Option<Box<dyn Fn(i32)>>);

