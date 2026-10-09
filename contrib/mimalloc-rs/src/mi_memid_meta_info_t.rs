use crate::*;
use std::os::raw::c_void;


#[derive(Clone)]
pub struct MiMemidMetaInfo {
    pub meta_page: Option<*mut c_void>,
    pub block_index: u32,
    pub block_count: u32,
}

