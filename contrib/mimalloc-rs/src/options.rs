use MiOption as MiOption;
use crate::*;
use crate::ERROR_COUNT;
use crate::MI_MAX_ERROR_COUNT;
use crate::super_function_unit4::convert_mi_option;
use crate::super_function_unit4::mi_option_is_enabled;
use lazy_static::lazy_static;
use std::ffi::CStr;
use std::ffi::c_char;
use std::ffi::c_void;
use std::process::abort;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::Ordering;
pub fn mi_version() -> i32 {
    316
}

pub fn mi_error_default(err: i32) {
    if err == 14 {
        abort();
    }
}
pub fn mi_option_has_size_in_kib(option: MiOption) -> bool {
    option == MiOption::ReserveOsMemory || option == MiOption::ArenaReserve
}
pub fn mi_recurse_exit_prim() {
    RECURSE.store(false, std::sync::atomic::Ordering::SeqCst);
}
pub fn mi_recurse_exit() {
    mi_recurse_exit_prim();
}

lazy_static! {
    pub static ref RECURSE: AtomicBool = AtomicBool::new(false);
}

pub fn mi_recurse_enter_prim() -> bool {
    // Compare and swap: if RECURSE is false, set it to true and return true
    // If RECURSE is already true, return false
    RECURSE.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_ok()
}
pub fn mi_recurse_enter() -> bool {
    mi_recurse_enter_prim()
}
pub fn _mi_option_get_fast(option: MiOption) -> i64 {
    // The C code uses an assert-like macro to check bounds.
    // In Rust, we can use debug_assert! for similar behavior in debug builds.
    // The condition checks that option is within the valid range.
    // Since MiOption is an enum, we can check if it's less than MiOption::MiOptionLast.
    // We'll convert to i32 and check against the sentinel last value.
    debug_assert!((option as i32) >= 0 && (option as i32) < MiOption::MiOptionLast as i32, "option >= 0 && option < _mi_option_last");

    // Access the global MI_OPTIONS array
    let mi_options = crate::MI_OPTIONS.lock().unwrap();
    
    // Convert the enum to its underlying i32 value to use as an index
    let index = option as i32;
    
    // Get the descriptor for the given option
    let desc = &mi_options[index as usize];
    
    // Another debug assertion to ensure the descriptor matches the option
    debug_assert!(desc.option == option, "desc->option == option");
    
    // Return the value field as i64
    desc.value as i64
}
pub fn mi_option_set(option: crate::mi_option_t::MiOption, value: i64) {
    // Check if option is within valid range (0 to _mi_option_last-1)
    // We need to get the index and check it's within bounds
    let index = option as usize;
    
    // Get the total number of options from the MI_OPTIONS array length
    // We'll check this after acquiring the lock to avoid deadlocks
    // First, get mutable access to the global MI_OPTIONS array
    let mut options_guard = crate::MI_OPTIONS.lock().unwrap();
    let options = &mut *options_guard;
    
    // Check if index is within bounds (equivalent to option < _mi_option_last in C)
    if index >= options.len() {
        return;
    }
    
    // Get mutable reference to the option descriptor
    let desc = &mut options[index];
    
    // Verify that the descriptor's option field matches (assertion in C)
    // In safe Rust, we can use debug_assert! for development builds
    debug_assert_eq!(desc.option as i32, option as i32, "desc->option == option");
    
    // Set the value and mark as initialized
    desc.value = value as isize;  // Convert i64 to isize
    desc.init = crate::mi_option_init_t::mi_option_init_t::MI_OPTION_INITIALIZED;
    
    // Handle guarded min/max synchronization
    if desc.option as i32 == crate::mi_option_t::MiOption::GuardedMin as i32 {
        let current_max = crate::_mi_option_get_fast(crate::mi_option_t::MiOption::GuardedMax);
        if current_max < value {
            // Release the lock before recursive call to avoid deadlock
            drop(options_guard);
            mi_option_set(crate::mi_option_t::MiOption::GuardedMax, value);
            return;
        }
    } else if desc.option as i32 == crate::mi_option_t::MiOption::GuardedMax as i32 {
        let current_min = crate::_mi_option_get_fast(crate::mi_option_t::MiOption::GuardedMin);
        if current_min > value {
            // Release the lock before recursive call to avoid deadlock
            drop(options_guard);
            mi_option_set(crate::mi_option_t::MiOption::GuardedMin, value);
            return;
        }
    }
}
pub fn mi_vfprintf_thread(
    output_func: MiOutputFun,
    argument: Option<&mut dyn std::any::Any>,
    pre: Option<&CStr>,
    format: &CStr,
    va_args: *mut std::ffi::c_void,
) {
    // Check if we should add thread prefix
    if let Some(prefix_cstr) = pre {
        if let Ok(prefix_str) = prefix_cstr.to_str() {
            if _mi_strnlen(Some(prefix_str), 33) <= 32 && !_mi_is_main_thread() {
                let mut tprefix = [0u8; 64];
                let thread_id = _mi_thread_id();
                
                // Create the formatted prefix string
                let formatted_prefix = format!("{}thread 0x{:x}: ", prefix_str, thread_id);
                
                // Ensure null termination
                let bytes_to_copy = formatted_prefix.len().min(tprefix.len() - 1);
                tprefix[..bytes_to_copy].copy_from_slice(&formatted_prefix.as_bytes()[..bytes_to_copy]);
                tprefix[bytes_to_copy] = 0;
                
                // Convert to C string
                let tprefix_cstr = unsafe { CStr::from_ptr(tprefix.as_ptr() as *const std::ffi::c_char) };
                
                // Call mi_vfprintf with thread prefix
                mi_vfprintf(Some(output_func), argument, Some(tprefix_cstr), format, va_args);
                return;
            }
        }
    }
    
    // Call mi_vfprintf without thread prefix
    mi_vfprintf(Some(output_func), argument, pre, format, va_args);
}

// Declare vsnprintf from libc manually since it's not exposed by the libc crate
extern "C" {
    fn vsnprintf(
        buf: *mut std::os::raw::c_char,
        buflen: libc::size_t,
        fmt: *const std::os::raw::c_char,
        args: *mut libc::c_void,
    ) -> std::os::raw::c_int;
}

// mi_vfprintf - Format and print a message using variadic arguments
pub fn mi_vfprintf(
    output_func: Option<MiOutputFun>,
    argument: Option<&mut dyn std::any::Any>,
    pre: Option<&CStr>,
    format: &CStr,
    va_args: *mut std::ffi::c_void,
) {
    // Format the message using vsnprintf
    let mut buf = [0u8; 1024];
    let buf_ptr = buf.as_mut_ptr() as *mut std::os::raw::c_char;
    
    let written = unsafe {
        vsnprintf(buf_ptr, buf.len(), format.as_ptr(), va_args as _)
    };
    
    if written < 0 {
        return;
    }
    
    // Convert to string
    let message = unsafe {
        let len = std::cmp::min(written as usize, buf.len() - 1);
        std::str::from_utf8_unchecked(&buf[..len])
    };
    
    // Build the full message with prefix
    let full_message = if let Some(prefix) = pre {
        format!("{}{}", prefix.to_string_lossy(), message)
    } else {
        message.to_string()
    };
    
    // Call the output function or default to stderr
    if let Some(out_fn) = output_func {
        out_fn(&full_message, argument);
    } else {
        eprint!("{}", full_message);
    }
}
pub fn mi_show_error_message(fmt: &CStr, args: *mut c_void) {
    if !mi_option_is_enabled(convert_mi_option(MiOption::Verbose)) {
        if !mi_option_is_enabled(convert_mi_option(MiOption::ShowErrors)) {
            return;
        }

        let mi_max_error_count = MI_MAX_ERROR_COUNT.load(Ordering::Acquire);
        if mi_max_error_count >= 0 {
            let prev = ERROR_COUNT.fetch_add(1, Ordering::AcqRel) as i64;
            if prev > mi_max_error_count {
                return;
            }
        }
    }

    let pre =
        CStr::from_bytes_with_nul(b"mimalloc: error: \0").expect("NUL-terminated error prefix");

    let output_func: Option<MiOutputFun> = None;
    if let Some(func) = output_func {
        mi_vfprintf_thread(func, Option::None, Some(pre), fmt, args);
    }
}
pub static MI_ERROR_ARG: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());

pub type mi_error_fun = fn(err: i32, arg: Option<&mut ()>);

// MiOutputFun is already defined in dependencies, so we don't redefine it here

// mi_error_default is already defined in dependencies, so we don't redefine it here

pub fn _mi_error_message(err: i32, fmt: *const c_char) {
    unsafe {
        let fmt_cstr = CStr::from_ptr(fmt);
        let fmt_str = fmt_cstr.to_string_lossy();
        
        // Call mi_show_error_message with the formatted string
        // Note: The original C code uses va_list which isn't directly translatable
        // This is a simplified version that just passes the format string
        // Since we don't have va_list in Rust, we pass null pointer for args
        mi_show_error_message(&fmt_cstr, std::ptr::null_mut());
        
        // Check if error handler is set
        if let Some(handler) = MI_ERROR_HANDLER {
            let arg_ptr = MI_ERROR_ARG.load(Ordering::Acquire);
            let arg = if arg_ptr.is_null() {
                Option::None
            } else {
                Some(&mut *arg_ptr)
            };
            handler(err, arg);
        } else {
            mi_error_default(err);
        }
    }
}

// Global variables (these would typically be defined elsewhere in the crate)
static MI_ERROR_HANDLER: Option<mi_error_fun> = None;
pub fn mi_option_get_clamp(option: MiOption, min: i64, max: i64) -> i64 {
    let x = mi_option_get(option);
    if x < min {
        min
    } else if x > max {
        max
    } else {
        x
    }
}
pub fn mi_option_get_size(option: MiOption) -> usize {
    let x = mi_option_get(option);
    let mut size = if x < 0 { 0 } else { x as usize };
    
    if mi_option_has_size_in_kib(option) {
        size *= 1024;
    }
    
    size
}
pub fn _mi_verbose_message(fmt: &std::ffi::CStr, mut args: *mut std::ffi::c_void) {
    if !mi_option_is_enabled(MiOption::Verbose) {
        return;
    }
    
    let prefix = std::ffi::CStr::from_bytes_with_nul(b"mimalloc: \0").unwrap();
    mi_vfprintf(Option::None, Option::None, Some(prefix), fmt, args);
}

pub fn _mi_raw_message(fmt: &CStr) {
    // In C, this function uses variadic arguments. In Rust, we pass the format string
    // directly to mi_vfprintf, which will handle the variadic arguments internally.
    // The C code passes 0 for the first three arguments, which corresponds to None in Rust.
    mi_vfprintf(None, None, None, fmt, std::ptr::null_mut());
}
pub fn _mi_message(fmt: &std::ffi::CStr, args: *mut std::ffi::c_void) {
    let pre = std::ffi::CStr::from_bytes_with_nul(b"mimalloc: \0").expect("valid C string literal");
    mi_vfprintf(None, None, Some(pre), fmt, args);
}
pub fn mi_out_stderr(msg: Option<&str>, arg: Option<&mut ()>) {
    // arg is unused in the C code, so we ignore it
    let _ = arg;
    
    // Check if msg is not None and not an empty string
    if let Some(msg_str) = msg {
        if !msg_str.is_empty() {
            _mi_prim_out_stderr(msg_str);
        }
    }
}
pub fn mi_out_buf_flush(out: Option<MiOutputFun>, no_more_buf: bool, arg: Option<&mut dyn std::any::Any>) {
    if out.is_none() {
        return;
    }
    let out = out.unwrap();
    
    let increment = if no_more_buf { 16 * 1024 } else { 1 };
    let count = OUT_LEN.fetch_add(increment, std::sync::atomic::Ordering::AcqRel);
    
    let count = if count > 16 * 1024 { 16 * 1024 } else { count };
    
    {
        let mut buffer = MI_OUTPUT_BUFFER.lock().unwrap();
        buffer[count] = 0;
        
        let msg = std::str::from_utf8(&buffer[..count]).unwrap_or("");
        out(msg, arg);
        
        if !no_more_buf {
            buffer[count] = b'\n';
        }
    }
}
pub fn mi_out_buf_stderr(msg: Option<&str>, mut arg: Option<&mut dyn std::any::Any>) {
    // Call mi_out_stderr with None since we don't actually use the argument
    mi_out_stderr(msg, Option::None);
    
    // Call mi_out_buf with the original argument
    mi_out_buf(msg, arg);
}
lazy_static! {
    pub static ref MI_OUT_DEFAULT: AtomicPtr<MiOutputFun> = AtomicPtr::new(std::ptr::null_mut());
}

pub fn mi_add_stderr_output() {
    // Check if mi_out_default is NULL (0 in C)
    if MI_OUT_DEFAULT.load(Ordering::SeqCst).is_null() {
        // Flush the stderr buffer
        // Create a wrapper function that matches MiOutputFun signature
        fn out_fn_wrapper(msg: &str, arg: Option<&mut dyn std::any::Any>) {
            // Convert arg from Option<&mut dyn std::any::Any> to Option<&mut ()>
            let converted_arg = arg.map(|a| unsafe { &mut *(a as *mut dyn std::any::Any as *mut ()) });
            mi_out_stderr(Some(msg), converted_arg);
        }
        
        mi_out_buf_flush(Some(out_fn_wrapper), false, Option::None);
        
        // Set mi_out_default to point to mi_out_buf_stderr
        // Convert mi_out_buf_stderr to a function pointer
        let ptr = mi_out_buf_stderr as *const MiOutputFun as *mut MiOutputFun;
        MI_OUT_DEFAULT.store(ptr, Ordering::SeqCst);
    }
}

pub fn mi_options_print() {
    const VERMAJOR: i32 = 316 / 100;
    const VERMINOR: i32 = (316 % 100) / 10;
    const VERPATCH: i32 = 316 % 10;
    
    // Format the version string
    let version_msg = format!("v{}.{}.{}{}{} (built on {}, {})\n", 
                             VERMAJOR, VERMINOR, VERPATCH, "", "", "Dec 16 2025", "20:53:47");
    let c_version_msg = std::ffi::CString::new(version_msg).unwrap();
    _mi_message(&c_version_msg, std::ptr::null_mut());
    
    // Lock the global options array
    let mi_options_guard = MI_OPTIONS.lock().unwrap();
    let mi_options = &*mi_options_guard;
    
    // Iterate through all options
    for i in 0..MiOption::MiOptionLast as usize {
        let option = unsafe { std::mem::transmute::<u8, MiOption>(i as u8) };
        let l = mi_option_get(option);
        // Cast to void in C - no action needed in Rust
        let desc = &mi_options[i];
        
        // Get the unit suffix based on option type
        let unit_suffix = if mi_option_has_size_in_kib(option) { "KiB" } else { "" };
        
        // Format the option message
        let name = desc.name.unwrap_or("");
        let option_msg = format!("option '{}': {} {}\n", name, desc.value, unit_suffix);
        let c_option_msg = std::ffi::CString::new(option_msg).unwrap();
        _mi_message(&c_option_msg, std::ptr::null_mut());
    }
    
    // Drop the lock before other messages (not strictly necessary but good practice)
    drop(mi_options_guard);
    
    // Print additional system information
    let debug_msg = format!("debug level : {}\n", 2);
    let c_debug_msg = std::ffi::CString::new(debug_msg).unwrap();
    _mi_message(&c_debug_msg, std::ptr::null_mut());
    
    let secure_msg = format!("secure level: {}\n", 0);
    let c_secure_msg = std::ffi::CString::new(secure_msg).unwrap();
    _mi_message(&c_secure_msg, std::ptr::null_mut());
    
    let mem_msg = format!("mem tracking: {}\n", "none");
    let c_mem_msg = std::ffi::CString::new(mem_msg).unwrap();
    _mi_message(&c_mem_msg, std::ptr::null_mut());
}
pub fn _mi_options_init() {
    mi_add_stderr_output();
    
    for i in 0..MiOption::MiOptionLast as u8 {
        let option = unsafe { std::mem::transmute::<u8, MiOption>(i) };
        let l = mi_option_get(option);
        // l is intentionally unused
        let _ = l;
    }

    MI_MAX_ERROR_COUNT.store(
        mi_option_get(MiOption::MaxErrors),
        Ordering::Relaxed
    );
    
    MI_MAX_WARNING_COUNT.store(
        mi_option_get(MiOption::MaxWarnings),
        Ordering::Relaxed
    );
    
    if mi_option_is_enabled(MiOption::Verbose) {
        mi_options_print();
    }
}
pub fn mi_option_set_enabled(option: crate::mi_option_t::MiOption, enable: bool) {
    let value = if enable { 1 } else { 0 };
    crate::mi_option_set(option, value);
}
pub fn mi_option_enable(option: crate::mi_option_t::MiOption) {
    mi_option_set_enabled(option, true);
}
pub fn mi_option_disable(option: crate::mi_option_t::MiOption) {
    mi_option_set_enabled(option, false);
}
pub fn mi_option_set_default(option: crate::MiOption, value: isize) {
    // Check bounds assertion
    let index = option as isize;
    if !(index >= 0 && index < crate::MiOption::MiOptionLast as isize) {
        // In debug builds, this will panic with assertion message
        // In release builds, it will just return early
        #[cfg(debug_assertions)]
        {
            let assertion = std::ffi::CString::new("option >= 0 && option < _mi_option_last").unwrap();
            let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/options.c").unwrap();
            let func = std::ffi::CString::new("mi_option_set_default").unwrap();
            // Use fully qualified path to disambiguate
            super_function_unit5::_mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 299, func.as_ptr());
        }
        return;
    }
    
    // Additional bounds check (redundant but matches C code)
    if index < 0 || index >= crate::MiOption::MiOptionLast as isize {
        return;
    }
    
    // Lock the global options mutex
    if let Ok(mut options_guard) = crate::MI_OPTIONS.lock() {
        // Get mutable reference to the specific option descriptor
        let desc = &mut options_guard[index as usize];
        
        // Check if not already initialized
        if desc.init != crate::mi_option_init_t::mi_option_init_t::MI_OPTION_INITIALIZED {
            desc.value = value;
        }
    }
    // Mutex guard is automatically released here
}
pub fn mi_option_set_enabled_default(option: crate::MiOption, enable: bool) {
    let value = if enable { 1 } else { 0 };
    crate::mi_option_set_default(option, value);
}
pub fn mi_register_error(fun: Option<mi_error_fun>, arg: Option<&mut ()>) {
    // Store the error handler function
    // Since MI_ERROR_HANDLER doesn't exist in dependencies, we need to create/store it
    // Based on the original C code, we need a static to store the function pointer
    static mut MI_ERROR_HANDLER: Option<mi_error_fun> = None;
    
    unsafe {
        MI_ERROR_HANDLER = fun;
    }
    
    let raw_arg = match arg {
        Some(ptr) => ptr as *mut () as *mut (),
        None => std::ptr::null_mut(),
    };
    
    // Disambiguate MI_ERROR_ARG by using the fully qualified path
    crate::globals::MI_ERROR_ARG.store(raw_arg, std::sync::atomic::Ordering::Release);
}
pub fn mi_register_output(out: Option<MiOutputFun>, mut arg: Option<&mut dyn std::any::Any>) {
    // Use mi_out_stderr as the default if no output function is provided
    fn mi_out_stderr_wrapper(msg: &str, arg: Option<&mut dyn std::any::Any>) {
        // Convert arg to Option<&mut ()> for mi_out_stderr
        let arg_opt = arg.map(|a| unsafe { &mut *(a as *mut dyn std::any::Any as *mut ()) });
        unsafe {
            mi_out_stderr(Some(msg), arg_opt);
        }
    }
    
    let mi_out_default: MiOutputFun = match out {
        Some(func) => func,
        None => mi_out_stderr_wrapper,
    };
    
    // Store the function pointer in the atomic
    MI_OUT_DEFAULT.store(Box::into_raw(Box::new(mi_out_default)) as *mut MiOutputFun, Ordering::Release);
    
    // Store the argument pointer if provided
    if let Some(arg_ref) = arg.as_mut() {
        // Convert the mutable reference to a raw pointer
        let arg_ptr = *arg_ref as *mut dyn std::any::Any as *mut ();
        MI_OUT_ARG.store(arg_ptr, Ordering::Release);
    } else {
        MI_OUT_ARG.store(std::ptr::null_mut(), Ordering::Release);
    }
    
    // Flush if a custom output function was provided
    if out.is_some() {
        mi_out_buf_flush(out, true, arg);
    }
}
