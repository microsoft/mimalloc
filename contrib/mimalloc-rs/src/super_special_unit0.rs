use crate::*;
use std::ffi::c_void;
use std::sync::Mutex;
use std::sync::atomic::AtomicI64;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::AtomicUsize;


pub struct MiMemidOsInfo {
    pub base: Option<Vec<u8>>,
    pub size: usize,
}

pub struct MiMemidMetaInfo {
    pub meta_page: Option<*mut c_void>,
    pub block_index: u32,
    pub block_count: u32,
}

pub struct MiMemid {
    pub mem: MiMemidMem,
    pub memkind: crate::mi_memkind_t::mi_memkind_t,
    pub is_pinned: bool,
    pub initially_committed: bool,
    pub initially_zero: bool,
}

pub type mi_memid_t = MiMemid;

pub enum MiMemidMem {
    Os(MiMemidOsInfo),
    Arena(mi_memid_arena_info_t),
    Meta(MiMemidMetaInfo),
}

#[repr(C)]
pub struct MiPageS {
    pub xthread_id: AtomicUsize,
    pub free: Option<*mut crate::mi_block_t::MiBlock>,
    pub used: u16,
    pub capacity: u16,
    pub reserved: u16,
    pub retire_expire: u8,
    pub local_free: Option<*mut crate::mi_block_t::MiBlock>,
    pub xthread_free: AtomicUsize,
    pub block_size: usize,
    pub page_start: Option<*mut u8>,
    pub heap_tag: u8,
    pub free_is_zero: bool,
    pub keys: [usize; 2],
    pub heap: Option<*mut mi_heap_t>,
    pub next: Option<*mut MiPageS>,
    pub prev: Option<*mut MiPageS>,
    pub slice_committed: usize,
    pub memid: MiMemid,
}

pub type mi_page_t = MiPageS;

pub struct MiHeapS {
    pub tld: Option<Box<mi_tld_t>>,
    pub exclusive_arena: Option<Box<mi_arena_t>>,
    pub numa_node: i32,
    pub cookie: usize,
    pub random: crate::mi_random_ctx_t::mi_random_ctx_t,
    pub page_count: usize,
    pub page_retired_min: usize,
    pub page_retired_max: usize,
    pub generic_count: i64,
    pub generic_collect_count: i64,
    pub next: Option<Box<MiHeapS>>,
    pub page_full_retain: i64,
    pub allow_page_reclaim: bool,
    pub allow_page_abandon: bool,
    pub tag: u8,
    pub pages_free_direct: [Option<Box<mi_page_t>>;
        (128
            + (((std::mem::size_of::<crate::mi_padding_t::mi_padding_t>() + (1 << 3)) - 1) / (1 << 3)))
            + 1],
    pub pages: [mi_page_queue_t; (73 + 1) + 1],
    pub memid: MiMemid,
}

pub type mi_heap_t = MiHeapS;

pub struct MiTldS {
    pub thread_id: usize,
    pub thread_seq: usize,
    pub numa_node: i32,
    pub subproc: Option<Box<mi_subproc_t>>,
    pub heap_backing: Option<Box<mi_heap_t>>,
    pub heaps: Option<Box<mi_heap_t>>,
    pub heartbeat: u64,
    pub recurse: bool,
    pub is_in_threadpool: bool,
    pub stats: crate::mi_stats_t::mi_stats_t,
    pub memid: MiMemid,
}

pub struct mi_tld_s {
    pub thread_id: usize,
    pub thread_seq: usize,
    pub numa_node: i32,
    pub subproc: Option<Box<mi_subproc_t>>,
    pub heap_backing: Option<Box<mi_heap_t>>,
    pub heaps: Option<Box<mi_heap_t>>,
    pub heartbeat: u64,
    pub recurse: bool,
    pub is_in_threadpool: bool,
    pub stats: crate::mi_stats_t::mi_stats_t,
    pub memid: MiMemid,
}

pub type mi_tld_t = mi_tld_s;

pub struct MiPageQueueS {
    pub first: Option<*mut mi_page_t>,
    pub last: Option<*mut mi_page_t>,
    pub count: usize,
    pub block_size: usize,
}

pub type mi_page_queue_t = MiPageQueueS;

#[repr(C)]
pub struct mi_memid_arena_info_t {
    pub arena: Option<*mut mi_arena_t>,
    pub slice_index: u32,
    pub slice_count: u32,
}

#[repr(C)]
pub struct mi_subproc_t {
    pub arena_count: AtomicUsize,
    pub arenas: [AtomicPtr<mi_arena_t>; 160],
    pub arena_reserve_lock: Mutex<()>,
    pub purge_expire: AtomicI64,
    pub abandoned_count: [AtomicUsize; 75],
    pub os_abandoned_pages: Option<*mut mi_page_t>,
    pub os_abandoned_pages_lock: Mutex<()>,
    pub memid: MiMemid,
    pub stats: crate::mi_stats_t::mi_stats_t,
}

#[repr(C)]
pub struct MiArenaS {
    pub memid: MiMemid,
    pub subproc: Option<Box<mi_subproc_t>>,
    pub slice_count: usize,
    pub info_slices: usize,
    pub numa_node: i32,
    pub is_exclusive: bool,
    pub purge_expire: AtomicI64,
    pub commit_fun: Option<crate::mi_commit_fun_t::MiCommitFun>,
    pub commit_fun_arg: Option<*mut std::ffi::c_void>,
    pub slices_free: Option<Box<crate::mi_bbitmap_t::mi_bbitmap_t>>,
    pub slices_committed: Option<Box<crate::mi_bchunkmap_t::mi_bchunkmap_t>>,
    pub slices_dirty: Option<Box<crate::mi_bchunkmap_t::mi_bchunkmap_t>>,
    pub slices_purge: Option<Box<crate::mi_bchunkmap_t::mi_bchunkmap_t>>,
    pub pages: Option<Box<crate::mi_bchunkmap_t::mi_bchunkmap_t>>,
    pub pages_abandoned: [Option<Box<crate::mi_bchunkmap_t::mi_bchunkmap_t>>; 75],
}

pub type mi_arena_t = MiArenaS;

unsafe impl Send for mi_page_queue_t {}
unsafe impl Sync for mi_page_queue_t {}

unsafe impl Send for mi_memid_t {}
unsafe impl Sync for mi_memid_t {}

unsafe impl Send for mi_memid_arena_info_t {}
unsafe impl Sync for mi_memid_arena_info_t {}

unsafe impl Send for mi_page_t {}
unsafe impl Sync for mi_page_t {}

unsafe impl Send for mi_heap_t {}
unsafe impl Sync for mi_heap_t {}

