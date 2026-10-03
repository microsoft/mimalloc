use crate::*;

pub type HeapPageVisitorFun = fn(
    heap: Option<&crate::MiHeapS>,
    pq: Option<&crate::MiPageQueueS>,
    page: Option<&crate::MiPageS>,
    arg1: Option<&std::ffi::c_void>,
    arg2: Option<&std::ffi::c_void>,
) -> bool;

