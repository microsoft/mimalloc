use crate::*;

#[repr(C)]
#[derive(Clone)]
pub struct Timeval {
    pub tv_sec: i64,      // 1 element: seconds
    pub tv_usec: i64, // 1 element: microseconds
}

