use crate::*;
use crate::int64_t;


#[derive(Clone)]
pub struct mi_stat_count_t {
    pub total: int64_t,
    pub peak: int64_t,
    pub current: int64_t,
}

