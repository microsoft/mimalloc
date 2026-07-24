use crate::*;


pub fn _mi_assert_fail(assertion: *const std::os::raw::c_char, fname: *const std::os::raw::c_char, line: u32, func: *const std::os::raw::c_char) {
    let assertion_str = unsafe { std::ffi::CStr::from_ptr(assertion) }.to_string_lossy();
    let fname_str = unsafe { std::ffi::CStr::from_ptr(fname) }.to_string_lossy();
    let func_str = if func.is_null() {
        "".to_string()
    } else {
        unsafe { std::ffi::CStr::from_ptr(func) }.to_string_lossy().to_string()
    };

    let message = format!(
        "mimalloc: assertion failed: at \"{}\":{}, {}\n  assertion: \"{}\"\n",
        fname_str, line, func_str, assertion_str
    );
    
    // Use mi_out_buf directly instead of _mi_fprintf
    mi_out_buf(Some(&message), None::<&mut dyn std::any::Any>);
    std::process::abort();
}


pub fn mi_out_buf(msg: Option<&str>, arg: Option<&mut dyn std::any::Any>) {
    // arg is unused in the C code
    let _ = arg;
    
    // Check for NULL pointer (None in Rust)
    let msg = match msg {
        Some(m) => m,
        None => return,
    };
    
    // Check if buffer is already full
    if OUT_LEN.load(std::sync::atomic::Ordering::Relaxed) >= (16 * 1024) {
        return;
    }
    
    let n = _mi_strlen(Some(msg));
    if n == 0 {
        return;
    }
    
    // Atomic fetch and add
    let start = OUT_LEN.fetch_add(n, std::sync::atomic::Ordering::AcqRel);
    
    // Check bounds after atomic operation
    if start >= (16 * 1024) {
        return;
    }
    
    let mut n = n;
    if (start + n) >= (16 * 1024) {
        n = ((16 * 1024) - start) - 1;
    }
    
    // Assertion check (in debug builds only)
    debug_assert!(start + n <= 16 * 1024, "start + n <= MI_MAX_DELAY_OUTPUT");
    
    // Get mutable access to the buffer
    let mut buffer = MI_OUTPUT_BUFFER.lock().unwrap();
    
    // Ensure we don't overflow the buffer
    let end = std::cmp::min(start + n, 16 * 1024);
    
    // Copy the message into the buffer
    let msg_bytes = msg.as_bytes();
    let copy_len = std::cmp::min(n, msg_bytes.len());
    buffer[start..start + copy_len].copy_from_slice(&msg_bytes[..copy_len]);
}


pub fn _mi_fputs(
    out: Option<MiOutputFun>,
    mut arg: Option<&mut dyn std::any::Any>,  // Changed to mutable
    prefix: *const std::os::raw::c_char,
    message: *const std::os::raw::c_char,
) {
    // Convert C strings to Rust strings safely
    let prefix_str = if !prefix.is_null() {
        unsafe { std::ffi::CStr::from_ptr(prefix).to_string_lossy().into_owned() }
    } else {
        String::new()
    };
    
    let message_str = if !message.is_null() {
        unsafe { std::ffi::CStr::from_ptr(message).to_string_lossy().into_owned() }
    } else {
        String::new()
    };

    // Check if out is None or points to stdout/stderr (simplified check)
    let use_default = out.is_none();
    
    if use_default {
        if !mi_recurse_enter() {
            return;
        }
        
        // Create a mutable pointer for MI_OUT_ARG
        let mut arg_ptr: *mut () = std::ptr::null_mut();
        let out_fn = mi_out_get_default(Some(&mut arg_ptr));
        
        if !prefix_str.is_empty() {
            // Convert arg_ptr to the expected type
            let arg_ref: Option<&mut dyn std::any::Any> = if arg_ptr.is_null() {
                None
            } else {
                // This is unsafe but matches the C code behavior
                Some(unsafe { &mut *(arg_ptr as *mut dyn std::any::Any) })
            };
            out_fn(&prefix_str, arg_ref);
        }
        
        let arg_ref: Option<&mut dyn std::any::Any> = if arg_ptr.is_null() {
            None
        } else {
            Some(unsafe { &mut *(arg_ptr as *mut dyn std::any::Any) })
        };
        out_fn(&message_str, arg_ref);
        
        mi_recurse_exit();
    } else {
        if let Some(out_fn) = out {
            if !prefix_str.is_empty() {
                // Pass arg by taking a mutable reference to its contents
                out_fn(&prefix_str, arg.as_deref_mut());
            }
            
            out_fn(&message_str, arg.as_deref_mut());
        }
    }
}


// Wrapper function to match MiOutputFun signature
fn mi_out_buf_wrapper(msg: &str, arg: Option<&mut dyn std::any::Any>) {
    mi_out_buf(Some(msg), arg);
}

pub fn mi_out_get_default(parg: Option<&mut *mut ()>) -> MiOutputFun {
    if let Some(parg_ref) = parg {
        *parg_ref = MI_OUT_ARG.load(std::sync::atomic::Ordering::Acquire);
    }
    
    let out = MI_OUT_DEFAULT.load(std::sync::atomic::Ordering::Relaxed);
    
    if out.is_null() {
        // Return the wrapper function that matches MiOutputFun signature
        mi_out_buf_wrapper
    } else {
        unsafe { std::mem::transmute(out) }
    }
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

// mi_vfprintf - Rust implementation that accepts Rust types
// The variadic arguments are passed as va_list pointer (va_args)
pub fn mi_vfprintf(
    out: Option<MiOutputFun>,
    arg: Option<&mut dyn std::any::Any>,
    prefix: Option<&std::ffi::CStr>,
    fmt: &std::ffi::CStr,
    va_args: *mut std::ffi::c_void,
) {
    // Format the message using vsnprintf
    let mut buf = [0u8; 1024];
    let buf_ptr = buf.as_mut_ptr() as *mut std::os::raw::c_char;
    
    let written = unsafe {
        vsnprintf(buf_ptr, buf.len(), fmt.as_ptr(), va_args as _)
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
    let full_message = if let Some(pre) = prefix {
        format!("{}{}", pre.to_string_lossy(), message)
    } else {
        message.to_string()
    };
    
    // Call the output function or default to stderr
    if let Some(out_fn) = out {
        out_fn(&full_message, arg);
    } else {
        eprint!("{}", full_message);
    }
}

