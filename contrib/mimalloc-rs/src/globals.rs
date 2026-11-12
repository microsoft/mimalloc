use crate::*;
use lazy_static::lazy_static;
use std::mem::zeroed;
use std::sync::Mutex;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::AtomicI64;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::AtomicU64;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;


lazy_static! {
    pub static ref _MI_PROCESS_IS_INITIALIZED: AtomicBool = AtomicBool::new(false);
}


pub static THREAD_COUNT: AtomicUsize = AtomicUsize::new(1);


pub static THREAD_TOTAL_COUNT: AtomicUsize = AtomicUsize::new(0);


lazy_static! {
    pub static ref OS_PRELOADING: AtomicBool = AtomicBool::new(true);
}


pub static _MI_CPU_HAS_FSRM: AtomicBool = AtomicBool::new(false);


pub static _MI_CPU_HAS_ERMS: AtomicBool = AtomicBool::new(false);


pub static _MI_CPU_HAS_POPCNT: AtomicBool = AtomicBool::new(false);


pub static MI_MAX_ERROR_COUNT: AtomicI64 = AtomicI64::new(16);


pub static MI_MAX_WARNING_COUNT: AtomicI64 = AtomicI64::new(16);


lazy_static! {
    pub static ref MI_OUTPUT_BUFFER: Mutex<[u8; ((16 * 1024) + 1)]> =
        Mutex::new([0; ((16 * 1024) + 1)]);
}


pub static OUT_LEN: AtomicUsize = AtomicUsize::new(0);


pub static MI_OUT_ARG: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());


pub static ERROR_COUNT: AtomicUsize = AtomicUsize::new(0);


pub static WARNING_COUNT: AtomicUsize = AtomicUsize::new(0);


lazy_static! {
    pub static ref RECURSE: AtomicBool = AtomicBool::new(false);
}


pub static MI_ERROR_ARG: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());


pub static MI_HUGE_START: AtomicUsize = AtomicUsize::new(0);


pub static MI_NUMA_NODE_COUNT: AtomicUsize = AtomicUsize::new(0);


pub static DEFERRED_ARG: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());


pub static MI_PAGE_MAP_COUNT: AtomicUsize = AtomicUsize::new(0);


lazy_static! {
    pub static ref MI_PAGE_MAP_MAX_ADDRESS: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
}


lazy_static! {
    pub static ref environ: Mutex<Option<Vec<String>>> = Mutex::new(None);
}


lazy_static! {
    pub static ref _MI_HEAP_DEFAULT_KEY: AtomicI32 = AtomicI32::new(-1);
}


pub static OK: AtomicI32 = AtomicI32::new(0);


pub static FAILED: AtomicI32 = AtomicI32::new(0);


pub static THREADS: AtomicI32 = AtomicI32::new(32);


pub static SCALE: AtomicI32 = AtomicI32::new(50);


pub static ITER: AtomicI32 = AtomicI32::new(50);


pub static ALLOW_LARGE_OBJECTS: AtomicBool = AtomicBool::new(false);


pub static USE_ONE_SIZE: AtomicUsize = AtomicUsize::new(0);


pub static MAIN_PARTICIPATES: AtomicBool = AtomicBool::new(false);


lazy_static! {
    pub static ref TRANSFER: [AtomicPtr<()>; 1000] = {
        let mut array: [AtomicPtr<()>; 1000] = unsafe { std::mem::MaybeUninit::uninit().assume_init() };
        for elem in &mut array {
            *elem = AtomicPtr::new(std::ptr::null_mut());
        }
        array
    };
}


pub const COOKIE: AtomicU64 = AtomicU64::new(0x1ce4e5b9);

lazy_static::lazy_static! {
    pub static ref THREAD_ENTRY_FUN: Mutex<Option<Box<dyn Fn(iptr) + Send + Sync>>> = 
        Mutex::new(Option::None);
}

// Alternative atomic version for thread-safe function pointer updates
static THREAD_ENTRY_FUN_ATOMIC: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());

// Helper type alias for clarity
type iptr = isize; // matches C's intptr_t

// Use the existing mi_page_t type from dependencies instead of redefining it
// Forward declaration of mi_page_t (already defined elsewhere)

// First, ensure mi_page_t is defined in scope
// Since it's a dependency, we need to reference it from the crate root

// mi_page_t should be available from the mimalloc implementation
// We'll reference it through the appropriate module

// Define _mi_page_empty as a static empty page
lazy_static::lazy_static! {
    pub static ref _mi_page_empty: crate::mi_page_t = {
        use std::sync::atomic::AtomicUsize;
        
        let empty_memid = MiMemid {
            mem: MiMemidMem::Os(MiMemidOsInfo {
                base: None,
                size: 0,
            }),
            memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_STATIC,
            is_pinned: false,
            initially_committed: true,
            initially_zero: true,
        };
        
        crate::mi_page_t {
            xthread_id: AtomicUsize::new(0),
            free: None,
            used: 0,
            capacity: 0,
            reserved: 0,
            retire_expire: 0,
            local_free: None,
            xthread_free: AtomicUsize::new(0),
            block_size: 0,
            page_start: None,
            heap_tag: 0,
            free_is_zero: true,
            keys: [0; 2],
            heap: None,
            next: None,
            prev: None,
            slice_committed: 0,
            memid: empty_memid,
        }
    };
}

lazy_static::lazy_static! {
    pub static ref _MI_HEAP_EMPTY: std::sync::Mutex<mi_heap_t> = {
        // Use zeroed initialization for complex nested structs
        let empty_page_queue = mi_page_queue_t {
            first: Some(std::ptr::null_mut()),
            last: Some(std::ptr::null_mut()),
            count: 0,
            block_size: 0,
        };
        let empty_pages: [mi_page_queue_t; 75] = std::array::from_fn(|_| mi_page_queue_t {
            first: Some(std::ptr::null_mut()),
            last: Some(std::ptr::null_mut()),
            count: 0,
            block_size: 0,
        });
        
        let empty_random = crate::mi_random_ctx_t::mi_random_ctx_t {
            input: [0; 16],
            output: [0; 16],
            output_available: 0,
            weak: false,
        };
        
        let empty_memid = MiMemid {
            mem: MiMemidMem::Os(MiMemidOsInfo {
                base: None,
                size: 0,
            }),
            memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_STATIC,
            is_pinned: false,
            initially_committed: true,
            initially_zero: true,
        };
        
        std::sync::Mutex::new(mi_heap_t {
            tld: None,
            exclusive_arena: None,
            numa_node: 0,
            cookie: 0,
            random: empty_random,
            page_count: 0,
            page_retired_min: 0,
            page_retired_max: 0,
            generic_count: 0,
            generic_collect_count: 0,
            next: None,
            page_full_retain: 0,
            allow_page_reclaim: false,
            allow_page_abandon: false,
            tag: 0,
            pages_free_direct: std::array::from_fn(|_| None),
            pages: empty_pages,
            memid: empty_memid,
        })
    };
}

lazy_static! {
    pub static ref _MI_PAGE_MAP: AtomicPtr<*mut *mut mi_page_t> = AtomicPtr::new(std::ptr::null_mut());
}

// Create a wrapper type for the raw pointer to implement Send/Sync
pub struct MiHeapPtr(pub *mut mi_heap_t);

unsafe impl Send for MiHeapPtr {}
unsafe impl Sync for MiHeapPtr {}

lazy_static! {
    pub static ref _mi_heap_default: Mutex<Option<MiHeapPtr>> = Mutex::new(None);
}

pub struct mi_meta_page_t {
    pub next: std::sync::atomic::AtomicPtr<mi_meta_page_t>,
    pub memid: crate::mi_memid_t,
    pub blocks_free: crate::mi_bbitmap_t::mi_bbitmap_t,
}


unsafe impl Send for mi_subproc_t {}
unsafe impl Sync for mi_subproc_t {}

lazy_static! {
    // C: static mi_subproc_t subproc_main = {0};
    pub static ref subproc_main: Mutex<mi_subproc_t> = {
        // These types do not implement `Default`; match C `{0}` semantics via zero-init.
        let memkind: crate::mi_memkind_t::mi_memkind_t = unsafe { std::mem::zeroed() };
        let stats: crate::mi_stats_t::mi_stats_t = unsafe { std::mem::zeroed() };

        Mutex::new(mi_subproc_t {
            arena_count: AtomicUsize::new(0),
            arenas: std::array::from_fn(|_| AtomicPtr::new(std::ptr::null_mut())), // 160
            arena_reserve_lock: Mutex::new(()),
            purge_expire: AtomicI64::new(0),
            abandoned_count: std::array::from_fn(|_| AtomicUsize::new(0)), // 75
            os_abandoned_pages: Option::None,
            os_abandoned_pages_lock: Mutex::new(()),
            memid: MiMemid {
                mem: MiMemidMem::Os(MiMemidOsInfo {
                    base: Option::None,
                    size: 0,
                }),
                memkind,
                is_pinned: false,
                initially_committed: false,
                initially_zero: false,
            },
            stats,
        })
    };
}

lazy_static! {
    pub static ref HEAP_MAIN: Mutex<Option<Box<MiHeapS>>> = Mutex::new(None);
}

lazy_static! {
    // C: mi_stats_t _mi_stats_main = {2, ...all zeros...};
    //
    // Keep it thread-safe and avoid `static mut` by storing it behind a Mutex.
    // The C initializer sets `version = 2` and everything else to zero.
    pub static ref _mi_stats_main: std::sync::Mutex<crate::mi_stats_t::mi_stats_t> =
        std::sync::Mutex::new({
            // SAFETY: mi_stats_t is used as a plain-data stats struct; an all-zero value
            // matches the C `{0,...}` initialization. We then set `version` to 2.
            let mut s: crate::mi_stats_t::mi_stats_t = unsafe { std::mem::zeroed() };
            s.version = 2;
            s
        });
}

lazy_static::lazy_static! {
    pub static ref MI_OPTIONS: std::sync::Mutex<[crate::mi_option_desc_t::mi_option_desc_t; 43]> = std::sync::Mutex::new([
        // 43 elements total
        crate::mi_option_desc_t::mi_option_desc_t { value: 1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ShowErrors, name: Some("show_errors"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ShowStats, name: Some("show_stats"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::Verbose, name: Some("verbose"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::EagerCommit, name: Some("eager_commit"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 2, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ArenaEagerCommit, name: Some("arena_eager_commit"), legacy_name: Some("eager_region_commit") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PurgeDecommits, name: Some("purge_decommits"), legacy_name: Some("reset_decommits") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 2, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::AllowLargeOsPages, name: Some("allow_large_os_pages"), legacy_name: Some("large_os_pages") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ReserveHugeOsPages, name: Some("reserve_huge_os_pages"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: -1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ReserveHugeOsPagesAt, name: Some("reserve_huge_os_pages_at"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ReserveOsMemory, name: Some("reserve_os_memory"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DeprecatedSegmentCache, name: Some("deprecated_segment_cache"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DeprecatedPageReset, name: Some("deprecated_page_reset"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::AbandonedPagePurge, name: Some("abandoned_page_purge"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DeprecatedSegmentReset, name: Some("deprecated_segment_reset"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::EagerCommitDelay, name: Some("eager_commit_delay"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 1000, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PurgeDelay, name: Some("purge_delay"), legacy_name: Some("reset_delay") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::UseNumaNodes, name: Some("use_numa_nodes"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DisallowOsAlloc, name: Some("disallow_os_alloc"), legacy_name: Some("limit_os_alloc") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 100, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::OsTag, name: Some("os_tag"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 32, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::MaxErrors, name: Some("max_errors"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 32, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::MaxWarnings, name: Some("max_warnings"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 10, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DeprecatedMaxSegmentReclaim, name: Some("deprecated_max_segment_reclaim"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DestroyOnExit, name: Some("destroy_on_exit"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 1024 * 1024, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ArenaReserve, name: Some("arena_reserve"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::ArenaPurgeMult, name: Some("arena_purge_mult"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DeprecatedPurgeExtendDelay, name: Some("deprecated_purge_extend_delay"), legacy_name: Some("decommit_extend_delay") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::DisallowArenaAlloc, name: Some("disallow_arena_alloc"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 400, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::RetryOnOom, name: Some("retry_on_oom"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::VisitAbandoned, name: Some("visit_abandoned"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::GuardedMin, name: Some("guarded_min"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: (1024 * 1024) * 1024, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::GuardedMax, name: Some("guarded_max"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::GuardedPrecise, name: Some("guarded_precise"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::GuardedSampleRate, name: Some("guarded_sample_rate"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::GuardedSampleSeed, name: Some("guarded_sample_seed"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 10000, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::GenericCollect, name: Some("generic_collect"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PageReclaimOnFree, name: Some("page_reclaim_on_free"), legacy_name: Some("abandoned_reclaim_on_free") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 2, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PageFullRetain, name: Some("page_full_retain"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 4, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PageMaxCandidates, name: Some("page_max_candidates"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::MaxVabits, name: Some("max_vabits"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PagemapCommit, name: Some("pagemap_commit"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 0, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PageCommitOnDemand, name: Some("page_commit_on_demand"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: -1, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PageMaxReclaim, name: Some("page_max_reclaim"), legacy_name: Some("") },
        crate::mi_option_desc_t::mi_option_desc_t { value: 32, init: crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT, option: MiOption::PageCrossThreadMaxReclaim, name: Some("page_cross_thread_max_reclaim"), legacy_name: Some("") }
    ]);
}


lazy_static! {
    pub static ref MI_OUT_DEFAULT: AtomicPtr<MiOutputFun> = AtomicPtr::new(std::ptr::null_mut());
}

pub type mi_error_fun = fn(err: i32, arg: Option<&mut ()>);


lazy_static! {
    pub static ref MI_OS_MEM_CONFIG: Mutex<MiOsMemConfig> = Mutex::new(MiOsMemConfig {
        page_size: 4096,
        large_page_size: 0,
        alloc_granularity: 4096,
        physical_memory_in_kib: 32 * (1024 * 1024),
        virtual_address_bits: 47,
        has_overcommit: true,
        has_partial_free: false,
        has_virtual_reserve: true,
    });
}


lazy_static! {
    pub static ref DEFERRED_FREE: AtomicPtr<MiDeferredFreeFun> = AtomicPtr::new(std::ptr::null_mut());
}

lazy_static::lazy_static! {
    pub static ref MI_PAGE_MAP_MEMID: std::sync::Mutex<MiMemid> =
        std::sync::Mutex::new(MiMemid {
            mem: MiMemidMem::Os(MiMemidOsInfo {
                base: None,
                size: 0,
            }),
            memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_NONE,
            is_pinned: false,
            initially_committed: false,
            initially_zero: false,
        });
}


pub static MI_PAGE_MAP_COMMIT: AtomicUsize = AtomicUsize::new(0);


lazy_static! {
    pub static ref MI_PROCESS_START: AtomicI64 = AtomicI64::new(0);
}


lazy_static! {
    pub static ref MI_CLOCK_DIFF: AtomicI64 = AtomicI64::new(0);
}

// Remove duplicate unsafe Send/Sync implementations since they already exist in dependencies
// Only keep implementations for types that don't already have them

// Add unsafe Send/Sync implementations for types containing raw pointers
// Removed duplicate implementations for mi_subproc_t since they already exist in dependencies
unsafe impl Send for MiTldS {}
unsafe impl Sync for MiTldS {}

lazy_static! {
    pub static ref TLD_MAIN: std::sync::Mutex<crate::MiTldS> = {
        // Create zeroed instances for initialization
        let mut stats: crate::mi_stats_t::mi_stats_t = unsafe { std::mem::zeroed() };
        stats.version = 2;
        
        // Initialize memid with MI_MEM_STATIC
        let memid = crate::MiMemid {
            mem: crate::MiMemidMem::Os(crate::MiMemidOsInfo {
                base: Option::None,
                size: 0,
            }),
            memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_STATIC,
            is_pinned: true,
            initially_committed: true,
            initially_zero: false,
        };
        
        std::sync::Mutex::new(crate::MiTldS {
            thread_id: 0,
            thread_seq: 0,
            numa_node: 0,
            subproc: Option::None,
            heap_backing: Option::None,
            heaps: Option::None,
            heartbeat: 0,
            recurse: false,
            is_in_threadpool: false,
            stats: stats,
            memid: memid,
        })
    };
}

// Remove the duplicate Send/Sync implementations since they already exist
// Lines 777-784 should be removed entirely

lazy_static! {
    pub static ref TLD_EMPTY: std::sync::Mutex<crate::MiTldS> = {
        
        // Create empty stats structure
        let empty_stats: crate::mi_stats_t::mi_stats_t = unsafe { zeroed() };
        
        // Create empty memid structure
        let empty_memid_meta: crate::MiMemidMetaInfo = unsafe { zeroed() };
        let empty_memid = crate::MiMemid {
            mem: crate::MiMemidMem::Meta(empty_memid_meta),
            memkind: crate::mi_memkind_t::mi_memkind_t::MI_MEM_STATIC,
            is_pinned: false,
            initially_committed: true,
            initially_zero: true,
        };
        
        std::sync::Mutex::new(crate::MiTldS {
            thread_id: 0,
            thread_seq: 0,
            numa_node: -1,
            subproc: Option::None,
            heap_backing: Option::None,
            heaps: Option::None,
            heartbeat: 0,
            recurse: false,
            is_in_threadpool: false,
            stats: empty_stats,
            memid: empty_memid,
        })
    };
}


lazy_static! {
    pub static ref THREAD_TLD: std::sync::Mutex<std::option::Option<std::boxed::Box<mi_tld_t>>> = {
        std::sync::Mutex::new(std::option::Option::None)
    };
}

