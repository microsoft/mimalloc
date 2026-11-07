use crate::*;
use crate::types::mi_msecs_t;


#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct mi_purge_visit_info_t {
    pub now: mi_msecs_t,
    pub delay: mi_msecs_t,
    pub all_purged: bool,
    pub any_purged: bool,
}

