use std::env;
use std::ffi::CStr;
use std::ffi::c_char;
use std::ffi::c_void;
use std::process::exit;
use std::sync::atomic::Ordering;
use translate_new::*;
use translate_new::globals::{THREADS, SCALE, ITER, ALLOW_LARGE_OBJECTS};

pub fn main() {
    let args: Vec<String> = env::args().collect();
    
    unsafe {
        if args.len() >= 2 {
            if let Ok(n) = args[1].parse::<i32>() {
                if n > 0 {
                    THREADS.store(n, Ordering::SeqCst);
                }
            }
        }
        
        if args.len() >= 3 {
            if let Ok(n) = args[2].parse::<i32>() {
                if n > 0 {
                    SCALE.store(n, Ordering::SeqCst);
                }
            }
        }
        
        if args.len() >= 4 {
            if let Ok(n) = args[3].parse::<i32>() {
                if n > 0 {
                    ITER.store(n, Ordering::SeqCst);
                }
            }
        }
        
        let scale = SCALE.load(Ordering::SeqCst);
        if scale > 100 {
            ALLOW_LARGE_OBJECTS.store(true, Ordering::SeqCst);
        }
        
        let threads = THREADS.load(Ordering::SeqCst);
        let iter = ITER.load(Ordering::SeqCst);
        let allow_large = ALLOW_LARGE_OBJECTS.load(Ordering::SeqCst);
        
        println!(
            "Using {} threads with a {}% load-per-thread and {} iterations {}",
            threads,
            scale,
            iter,
            if allow_large { "(allow large objects)" } else { "" }
        );
    }
    
    mi_stats_reset();
    
    unsafe {
        srand(0x7feb352d);
    }
    
    test_stress();
    mi_debug_show_arenas();
    mi_collect(true);
    
    let json = mi_stats_get_json(0, std::ptr::null_mut());
    if !json.is_null() {
        unsafe {
            let c_str = CStr::from_ptr(json);
            eprint!("{}", c_str.to_string_lossy());
        }
        mi_free(Some(json as *mut std::ffi::c_void));
    }
    
    mi_collect(true);
    mi_stats_print(Option::None);
    
    exit(0);
}

extern "C" {
    fn srand(seed: u32);
}

pub fn mi_collect(force: bool) {}
pub fn mi_debug_show_arenas() {}
pub fn mi_free(p: Option<*mut std::ffi::c_void>) {}
pub fn mi_stats_get_json(output_size: usize, output_buf: *mut c_char) -> *mut c_char {
    std::ptr::null_mut()
}
pub fn mi_stats_print(out: Option<crate::MiOutputFun>) {}
pub fn mi_stats_reset() {}
pub fn test_stress() {}
