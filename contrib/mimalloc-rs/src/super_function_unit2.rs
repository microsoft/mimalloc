use crate::*;
use std::ffi::CStr;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
pub fn mi_thread_init() {
    mi_process_init();
    if _mi_thread_heap_init() {
        return;
    }
    
    let mut subproc = _mi_subproc_main().lock().unwrap();
    mi_stat_increase_mt(&mut subproc.stats.threads, 1);
}


pub type mi_atomic_once_t = AtomicUsize;

lazy_static::lazy_static! {
    pub static ref _MI_PROCESS_IS_INITIALIZED: AtomicBool = AtomicBool::new(false);
}

pub fn mi_process_init() {
    static PROCESS_INIT: mi_atomic_once_t = AtomicUsize::new(0);
    
    mi_heap_main_init();
    
    if !mi_atomic_once(&PROCESS_INIT) {
        return;
    }
    
    _MI_PROCESS_IS_INITIALIZED.store(true, Ordering::SeqCst);
    
    let mut thread_id = _mi_thread_id();
    let fmt = CStr::from_bytes_with_nul(b"process init: 0x%zx\n\0").unwrap();
    _mi_verbose_message(fmt, &mut thread_id as *mut _ as *mut std::ffi::c_void);
    
    mi_detect_cpu_features();
    _mi_stats_init();
    _mi_os_init();
    _mi_page_map_init();
    mi_heap_main_init();
    mi_tld_main_init();
    mi_subproc_main_init();
    mi_process_setup_auto_thread_done();
    mi_thread_init();
    
    if mi_option_is_enabled(MiOption::ReserveHugeOsPages) {
        let pages = mi_option_get_clamp(MiOption::ReserveHugeOsPages, 0, 128 * 1024) as usize;
        let reserve_at = mi_option_get(MiOption::ReserveHugeOsPagesAt);
        
        if reserve_at != -1 {
            mi_reserve_huge_os_pages_at(pages, reserve_at as i32, pages as i64 * 500);
        } else {
            mi_reserve_huge_os_pages_interleave(pages, 0, pages as i64 * 500);
        }
    }
    
    if mi_option_is_enabled(MiOption::ReserveOsMemory) {
        let ksize = mi_option_get(MiOption::ReserveOsMemory);
        if ksize > 0 {
            mi_reserve_os_memory((ksize as usize) * 1024, true, true);
        }
    }
}
