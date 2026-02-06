use crate::*;


pub struct MiDeferredFreeFun {
    pub force: bool,
    pub heartbeat: u64,
    pub arg: Option<Box<dyn std::any::Any>>,
}

