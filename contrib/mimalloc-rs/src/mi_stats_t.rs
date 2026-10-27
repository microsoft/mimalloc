use crate::*;


pub const MI_CBIN_COUNT: usize = 128;

#[repr(C)]
#[derive(Clone)]
pub struct mi_stats_t {
    pub version: i32,
    pub pages: crate::mi_stat_count_t::mi_stat_count_t,
    pub reserved: crate::mi_stat_count_t::mi_stat_count_t,
    pub committed: crate::mi_stat_count_t::mi_stat_count_t,
    pub reset: crate::mi_stat_count_t::mi_stat_count_t,
    pub purged: crate::mi_stat_count_t::mi_stat_count_t,
    pub page_committed: crate::mi_stat_count_t::mi_stat_count_t,
    pub pages_abandoned: crate::mi_stat_count_t::mi_stat_count_t,
    pub threads: crate::mi_stat_count_t::mi_stat_count_t,
    pub malloc_normal: crate::mi_stat_count_t::mi_stat_count_t,
    pub malloc_huge: crate::mi_stat_count_t::mi_stat_count_t,
    pub malloc_requested: crate::mi_stat_count_t::mi_stat_count_t,
    pub mmap_calls: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub commit_calls: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub reset_calls: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub purge_calls: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub arena_count: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub malloc_normal_count: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub malloc_huge_count: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub malloc_guarded_count: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub arena_rollback_count: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub arena_purges: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub pages_extended: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub pages_retire: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub page_searches: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub segments: crate::mi_stat_count_t::mi_stat_count_t,
    pub segments_abandoned: crate::mi_stat_count_t::mi_stat_count_t,
    pub segments_cache: crate::mi_stat_count_t::mi_stat_count_t,
    pub _segments_reserved: crate::mi_stat_count_t::mi_stat_count_t,
    pub pages_reclaim_on_alloc: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub pages_reclaim_on_free: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub pages_reabandon_full: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub pages_unabandon_busy_wait: crate::mi_stat_counter_t::mi_stat_counter_t,
    pub _stat_reserved: [crate::mi_stat_count_t::mi_stat_count_t; 4],
    pub _stat_counter_reserved: [crate::mi_stat_counter_t::mi_stat_counter_t; 4],
    pub malloc_bins: [crate::mi_stat_count_t::mi_stat_count_t; 74],
    pub page_bins: [crate::mi_stat_count_t::mi_stat_count_t; 74],
    pub chunk_bins: [crate::mi_stat_count_t::mi_stat_count_t; MI_CBIN_COUNT],
}



