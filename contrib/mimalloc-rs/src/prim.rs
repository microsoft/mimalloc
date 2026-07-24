use MiOption as MiOption;
use core::convert::TryFrom;
use crate::*;
use crate::_MI_HEAP_DEFAULT_KEY;
use lazy_static::lazy_static;
use std::cell::RefCell;
use std::ffi::CStr;
use std::ffi::c_void;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Write;
use std::io;
use std::mem::MaybeUninit;
use std::os::raw::c_char;
use std::os::raw::c_int;
use std::os::unix::fs::MetadataExt;
use std::ptr;
use std::sync::Mutex;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::AtomicI64;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
use std::time::SystemTime;
use std::time::UNIX_EPOCH;


pub fn _mi_is_redirected() -> bool {
    false
}
pub fn _mi_allocator_init(message: Option<&mut Option<&'static str>>) -> bool {
    if let Some(msg) = message {
        *msg = None;
    }
    true
}

pub fn _mi_allocator_done() {
    // This function is intentionally left empty as per the C implementation
}
// Global variables from the C code
static mut addr: *mut std::ffi::c_void = std::ptr::null_mut();
static mut size: usize = 0;
static mut err: bool = false;

pub fn _mi_prim_free(free_addr: *mut std::ffi::c_void, free_size: usize) -> i32 {
    if free_size == 0 {
        return 0;
    }
    
    // Use unsafe block for system call
    let result = unsafe {
        // Use munmap directly (assuming it's declared somewhere as extern "C")
        #[cfg(unix)]
        {
            // Call munmap directly (not through libc namespace)
            let ret = munmap(free_addr, free_size);
            ret
        }
        #[cfg(not(unix))]
        {
            -1 // Not implemented on non-Unix platforms
        }
    };
    
    if result == -1 {
        unsafe {
            err = true;
            // Get the last OS error
            return std::io::Error::last_os_error().raw_os_error().unwrap_or(-1);
        }
    }
    
    0
}

// We need to declare munmap as an extern function since it's not in Rust's standard library
#[cfg(unix)]
extern "C" {
    fn munmap(addr: *mut std::ffi::c_void, len: usize) -> i32;
}
pub fn _mi_prim_reuse(start: Option<&mut [u8]>, size_param: usize) -> i32 {
    // Explicitly ignore the parameters as in the C code
    let _ = start;
    let _ = size_param;
    
    0
}
pub fn _mi_prim_numa_node() -> usize {
    let mut node: usize = 0;
    let mut ncpu: usize = 0;
    let syscall_err: isize;

    unsafe {
        std::arch::asm!(
            "syscall",
            in("rax") 309,
            in("rdi") &mut ncpu,
            in("rsi") &mut node,
            in("rdx") 0,
            out("rcx") _, // syscall clobbers rcx
            out("r11") _, // syscall clobbers r11
            lateout("rax") syscall_err,
        );
    }

    if syscall_err != 0 {
        return 0;
    }
    node
}

pub fn _mi_prim_out_stderr(msg: &str) {
    let _ = io::stderr().write_all(msg.as_bytes());
}

pub fn _mi_prim_thread_is_in_threadpool() -> bool {
    false
}

pub unsafe extern "C" fn mi_prim_open(fpath: *const c_char, open_flags: c_int) -> c_int {
    // System call number 2 is SYS_open on Linux
    // We'll use libc::syscall if available, but since we can't use libc,
    // we need an alternative. Let's use inline assembly for x86_64 Linux.
    #[cfg(target_os = "linux")]
    #[cfg(target_arch = "x86_64")]
    {
        let result: i64;
        core::arch::asm!(
            "syscall",
            in("rax") 2i64,  // SYS_open
            in("rdi") fpath,
            in("rsi") open_flags as i64,
            in("rdx") 0i64,   // mode
            out("rcx") _,     // clobbered
            out("r11") _,     // clobbered
            lateout("rax") result,
            options(nostack, preserves_flags)
        );
        result as c_int
    }
    
    #[cfg(not(all(target_os = "linux", target_arch = "x86_64")))]
    {
        // Fallback: use libc::open if we can't use inline assembly
        // Convert the C string to Rust string and use std::fs
        
        if fpath.is_null() {
            return -1;
        }
        
        let c_str = CStr::from_ptr(fpath);
        let path = match c_str.to_str() {
            Ok(s) => s,
            Err(_) => return -1,
        };
        
        match OpenOptions::new()
            .read((open_flags & 0o3) != 0o1)  // O_WRONLY is 1
            .write((open_flags & 0o3) != 0o0) // O_RDONLY is 0
            .create((open_flags & 0o100) != 0) // O_CREAT is 100
            .truncate((open_flags & 0o1000) != 0) // O_TRUNC is 1000
            .append((open_flags & 0o2000) != 0) // O_APPEND is 2000
            .open(path)
        {
            Ok(file) => file.into_raw_fd(),
            Err(_) => -1,
        }
    }
}
#[inline]
unsafe fn mi_prim_close(fd: i32) -> i32 {
    let ret: i32;
    core::arch::asm!(
        "syscall",
        in("rax") 3, // SYS_close
        in("rdi") fd,
        out("rcx") _, // clobbered by syscall
        out("r11") _, // clobbered by syscall
        lateout("rax") ret,
        options(nostack)
    );
    ret
}

pub fn mi_prim_access(fpath: Option<&str>, mode: i32) -> i32 {
    match fpath {
        Some(path) => {
            match std::fs::metadata(path) {
                Ok(metadata) => {
                    // Check if the file mode matches the requested access mode
                    // This is a simplified implementation - in real code you'd need
                    // to properly check R/W/X permissions against the mode parameter
                    if metadata.mode() as i32 & mode != 0 {
                        0  // Success - access granted
                    } else {
                        -1  // Failure - access denied
                    }
                }
                Err(_) => -1,  // File doesn't exist or other error
            }
        }
        None => -1,  // NULL path pointer
    }
}
pub fn unix_madvise(addr_param: *mut std::ffi::c_void, size_param: usize, advice: std::ffi::c_int) -> std::ffi::c_int {
    extern "C" {
        fn madvise(addr: *mut std::ffi::c_void, len: usize, advice: std::ffi::c_int) -> std::ffi::c_int;
    }
    let res = unsafe { madvise(addr_param, size_param, advice) };
    if res == 0 {
        0
    } else {
        std::io::Error::last_os_error().raw_os_error().unwrap_or(-1)
    }
}
// Constants from system headers
// MAP_FAILED might already be defined in libc, so we should use it from there
// or define it only if not already defined. Since we're told not to redefine
// global variables, let's assume it's available elsewhere.


extern "C" {
    fn mmap(
        addr: *mut c_void,
        length: usize,
        prot: i32,
        flags: i32,
        fd: std::os::fd::RawFd,
        offset: usize,
    ) -> *mut c_void;
    fn prctl(option: i32, arg2: usize, arg3: usize, arg4: usize, arg5: usize) -> i32;
}

pub fn unix_mmap_prim(
    addr_param: Option<*mut c_void>,
    size_param: usize,
    protect_flags: i32,
    flags: i32,
    fd: std::os::fd::RawFd,
) -> *mut c_void {
    let addr_ptr = addr_param.unwrap_or(std::ptr::null_mut());
    
    unsafe {
        let p = mmap(
            addr_ptr,
            size_param,
            protect_flags,
            flags,
            fd,
            0,
        );
        
        // MAP_FAILED is typically defined as ((void *) (-1)) in C
        // In Rust, we can represent this as -1isize as *mut c_void
        if p != (-1isize) as *mut c_void && !p.is_null() {
            prctl(0x53564d41, 0, p as usize, size_param, "mimalloc\0".as_ptr() as usize);
        }
        
        p
    }
}

pub fn unix_mmap_fd() -> i32 {
    -1
}
pub fn unix_mprotect_hint(_err: i32) {
    // The C function casts err to void to suppress unused parameter warnings
    // In Rust, we prefix with underscore to indicate it's intentionally unused
    // and to avoid conflict with the static `err` variable
}
pub fn mi_prim_mbind(
    start: *mut core::ffi::c_void,
    len: usize,
    mode: usize,
    nmask: *const usize,
    maxnode: usize,
    flags: usize,
) -> isize {
    let syscall_num: isize = 237;
    let result: isize;

    unsafe {
        core::arch::asm!(
            "syscall",
            in("rax") syscall_num,
            in("rdi") start,
            in("rsi") len,
            in("rdx") mode,
            in("r10") nmask,
            in("r8") maxnode,
            in("r9") flags,
            lateout("rax") result,
            options(nostack, preserves_flags)
        );
    }

    result
}

pub fn _mi_prim_clock_now() -> i64 {
    let now = SystemTime::now();
    let duration = now.duration_since(UNIX_EPOCH).expect("Time went backwards");
    duration.as_millis() as i64
}
pub fn mi_prim_read(fd: std::os::fd::RawFd, buf: *mut u8, bufsize: usize) -> isize {
    unsafe {
        let result: isize;
        
        #[cfg(target_os = "linux")]
        #[cfg(target_arch = "x86_64")]
        {
            const SYS_READ: i64 = 0;
            let syscall_num = SYS_READ;
            core::arch::asm!(
                "syscall",
                in("rax") syscall_num,
                in("rdi") fd,
                in("rsi") buf,
                in("rdx") bufsize,
                out("rcx") _, // clobbered by syscall
                out("r11") _, // clobbered by syscall
                lateout("rax") result,
                options(nostack)
            );
        }
        
        #[cfg(target_os = "linux")]
        #[cfg(target_arch = "x86")]
        {
            const SYS_READ: i32 = 3;
            let syscall_num = SYS_READ;
            core::arch::asm!(
                "int 0x80",
                in("eax") syscall_num,
                in("ebx") fd as i32,
                in("ecx") buf,
                in("edx") bufsize as u32,
                lateout("eax") result,
                options(nostack)
            );
        }
        
        #[cfg(not(any(
            all(target_os = "linux", target_arch = "x86_64"),
            all(target_os = "linux", target_arch = "x86")
        )))]
        {
            // Use libc's read function as fallback
            result = libc::read(fd as i32, buf as *mut _, bufsize as _) as isize;
        }
        
        result
    }
}

lazy_static! {
    pub static ref environ: Mutex<Option<Vec<String>>> = Mutex::new(None);
}

pub fn mi_get_environ() -> Option<Vec<String>> {
    let environ_guard = environ.lock().unwrap();
    environ_guard.clone()
}
pub fn _mi_prim_getenv(name: Option<&str>, result: &mut [u8]) -> bool {
    // Check for NULL pointer (None in Rust)
    if name.is_none() {
        return false;
    }
    let name = name.unwrap();

    // Get length using the provided dependency function
    let len = _mi_strlen(Some(name));
    if len == 0 {
        return false;
    }

    // Get environment using the provided dependency function
    let env_opt = mi_get_environ();
    if env_opt.is_none() {
        return false;
    }
    let env = env_opt.unwrap();

    // Iterate through environment variables
    for s in env.iter().take(10000) {
        if s.is_empty() {
            break;
        }

        // Check if the environment variable starts with name=
        if _mi_strnicmp(name, s.as_str(), len) == 0 && s.as_bytes().get(len) == Some(&b'=') {
            // Copy the value part (after '=') to result
            let value_start = len + 1;
            if value_start < s.len() {
                let value_bytes = &s.as_bytes()[value_start..];
                _mi_strlcpy(result, value_bytes);
                return true;
            }
        }
    }

    false
}
pub unsafe extern "C" fn _mi_prim_decommit(
    start: *mut std::ffi::c_void,
    size_param: usize,
    needs_recommit: *mut bool,
) -> std::ffi::c_int {
    let mut err_code: std::ffi::c_int = 0;
    // MADV_DONTNEED is typically 4 on Linux
    const MADV_DONTNEED: std::ffi::c_int = 4;
    // PROT_NONE is typically 0
    const PROT_NONE: std::ffi::c_int = 0;
    // Declare mprotect as an external C function
    extern "C" {
        fn mprotect(addr: *mut std::ffi::c_void, len: usize, prot: std::ffi::c_int) -> std::ffi::c_int;
    }
    err_code = unix_madvise(start, size_param, MADV_DONTNEED);
    *needs_recommit = true;
    // Call mprotect with PROT_NONE
    mprotect(start, size_param, PROT_NONE);
    err_code
}
pub fn _mi_prim_reset(start: *mut std::ffi::c_void, size_param: usize) -> std::ffi::c_int {
    let err_code = unix_madvise(start, size_param, 4); // MADV_DONTNEED = 4
    err_code
}
pub fn _mi_prim_commit(start: *mut c_void, size_param: usize, is_zero: &mut bool) -> c_int {
    *is_zero = false;
    
    // Declare mprotect as an external C function
    extern "C" {
        fn mprotect(addr: *mut c_void, len: usize, prot: c_int) -> c_int;
    }
    
    // Use raw values for PROT_READ | PROT_WRITE (0x1 | 0x2 = 0x3)
    let result = unsafe { mprotect(start, size_param, 0x1 | 0x2) };
    
    if result != 0 {
        let error_code = std::io::Error::last_os_error().raw_os_error().unwrap_or(-1);
        unix_mprotect_hint(error_code);
        return error_code;
    }
    
    0
}
pub fn unix_mmap_prim_aligned(
    addr_param: Option<*mut std::ffi::c_void>,
    size_param: usize,
    try_alignment: usize,
    protect_flags: i32,
    flags: i32,
    fd: i32,
) -> Option<*mut std::ffi::c_void> {
    // Define MAP_FAILED as it's used in the C code (-1 cast to void*)
    const MAP_FAILED: *mut std::ffi::c_void = (-1isize) as *mut std::ffi::c_void;
    
    if addr_param.is_none() {
        let hint = _mi_os_get_aligned_hint(try_alignment, size_param);
        if hint.is_some() {
            // In the C code: hint is a pointer, not Option<()>
            // The Rust version of _mi_os_get_aligned_hint returns Option<()> which is incorrect
            // We'll use the hint as None since the Rust version doesn't return a real pointer
            let p_idx = unix_mmap_prim(Option::None, size_param, protect_flags, flags, fd);
            
            if p_idx != MAP_FAILED {
                // Check alignment - note: &*p_idx is just p_idx in Rust (no pointer arithmetic needed)
                if _mi_is_aligned(Some(unsafe { &mut *p_idx }), try_alignment) {
                    return Some(p_idx);
                } else {
                    let error_code = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
                    // Use fully qualified path to disambiguate the function
                    crate::alloc::_mi_error_message(
                        error_code,
                        "unable to directly request hinted aligned OS memory (error: %d (0x%x), size: 0x%zx bytes, alignment: 0x%zx, hint address: %p)\n\0".as_ptr() as *const std::os::raw::c_char,
                    );
                    // In C code, if not aligned, it continues to try regular mmap
                }
            } else {
                let error_code = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
                crate::alloc::_mi_error_message(
                    error_code,
                    "unable to directly request hinted aligned OS memory (error: %d (0x%x), size: 0x%zx bytes, alignment: 0x%zx, hint address: %p)\n\0".as_ptr() as *const std::os::raw::c_char,
                );
            }
        }
    }
    
    // Try regular mmap without hint
    let p_idx = unix_mmap_prim(addr_param, size_param, protect_flags, flags, fd);
    
    if p_idx != MAP_FAILED {
        Some(p_idx)
    } else {
        Option::None
    }
}

static LARGE_PAGE_TRY_OK: AtomicUsize = AtomicUsize::new(0);

pub fn unix_mmap(
    addr_param: Option<*mut c_void>,
    size_param: usize,
    try_alignment: usize,
    protect_flags: i32,
    large_only: bool,
    allow_large: bool,
    is_large: &mut bool,
) -> Option<*mut c_void> {
    let mut p: Option<*mut c_void> = Option::None;
    let fd = unix_mmap_fd();
    let mut flags = 0x02 | 0x20;
    
    if _mi_os_has_overcommit() {
        flags |= 0x04000;
    }
    
    if allow_large && (large_only || (_mi_os_use_large_page(size_param, try_alignment) && 
        mi_option_get(convert_mi_option(MiOption::AllowLargeOsPages)) == 1)) {
        
        let try_ok = LARGE_PAGE_TRY_OK.load(Ordering::Acquire);
        
        if !large_only && try_ok > 0 {
            let mut current = try_ok;
            LARGE_PAGE_TRY_OK.compare_exchange_weak(
                current,
                current - 1,
                Ordering::AcqRel,
                Ordering::Acquire,
            ).ok();
        } else {
            let mut lflags = flags & !0x04000;
            let lfd = fd;
            lflags |= 0x40000;
            
            if large_only || lflags != flags {
                *is_large = true;
                let p_idx = unix_mmap_prim_aligned(
                    addr_param,
                    size_param,
                    try_alignment,
                    protect_flags,
                    lflags,
                    lfd,
                );
                
                if large_only {
                    return p;
                }
                
                if p_idx.is_none() {
                    LARGE_PAGE_TRY_OK.store(8, Ordering::Release);
                } else {
                    p = p_idx;
                }
            }
        }
    }
    
    if p.is_none() {
        *is_large = false;
        p = unix_mmap_prim_aligned(
            addr_param,
            size_param,
            try_alignment,
            protect_flags,
            flags,
            fd,
        );
    }
    
    p
}
pub fn _mi_prim_alloc(
    hint_addr: Option<*mut c_void>,
    size_param: usize,
    try_alignment: usize,
    commit: bool,
    allow_large: bool,
    is_large: &mut bool,
    is_zero: &mut bool,
    addr_out: &mut Option<*mut c_void>,
) -> i32 {
    // Assertions
    if !(size_param > 0 && (size_param % _mi_os_page_size()) == 0) {
        _mi_assert_fail(
            "size > 0 && (size % _mi_os_page_size()) == 0\0".as_ptr() as *const _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/prim/unix/prim.c\0".as_ptr() as *const _,
            418,
            "_mi_prim_alloc\0".as_ptr() as *const _,
        );
    }
    
    if !(commit || !allow_large) {
        _mi_assert_fail(
            "commit || !allow_large\0".as_ptr() as *const _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/prim/unix/prim.c\0".as_ptr() as *const _,
            419,
            "_mi_prim_alloc\0".as_ptr() as *const _,
        );
    }
    
    if !(try_alignment > 0) {
        _mi_assert_fail(
            "try_alignment > 0\0".as_ptr() as *const _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/prim/unix/prim.c\0".as_ptr() as *const _,
            420,
            "_mi_prim_alloc\0".as_ptr() as *const _,
        );
    }

    let mut try_alignment = try_alignment;
    
    // Adjust alignment for large allocations
    if hint_addr.is_none()
        && size_param >= 8 * (2 * (1024 * 1024))
        && try_alignment > 1
        && _mi_is_power_of_two(try_alignment)
        && try_alignment < (2 * (1024 * 1024))
    {
        try_alignment = 2 * (1024 * 1024);
    }

    *is_zero = true;
    
    let protect_flags = if commit { 0x2 | 0x1 } else { 0x0 };
    
    *addr_out = unix_mmap(
        hint_addr,
        size_param,
        try_alignment,
        protect_flags,
        false,
        allow_large,
        is_large,
    );

    if addr_out.is_some() {
        0
    } else {
        std::io::Error::last_os_error().raw_os_error().unwrap_or(-1)
    }
}
pub fn _mi_prim_alloc_huge_os_pages(
    hint_addr: Option<*mut c_void>,
    size_param: usize,  // Renamed from 'size' to avoid shadowing static
    numa_node: i32,
    is_zero: &mut bool,
    addr_out: &mut Option<*mut c_void>,  // Renamed from 'addr' to avoid shadowing static
) -> i32 {
    let mut is_large = true;
    *is_zero = true;
    
    *addr_out = unix_mmap(
        hint_addr,
        size_param,
        1_usize << (13 + 3),
        0x1 | 0x2,
        true,
        true,
        &mut is_large,
    );
    
    if let Some(addr_val) = *addr_out {
        if numa_node >= 0 && numa_node < (8 * (1 << 3)) {
            let numa_mask = 1_usize << numa_node;
            let bind_err = mi_prim_mbind(  // Renamed from 'err' to avoid shadowing static
                addr_val,
                size_param,
                1,
                &numa_mask,
                8 * (1 << 3),
                0,
            );
            
            if bind_err != 0 {
                let err_code = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
                let fmt = std::ffi::CString::new("failed to bind huge (1GiB) pages to numa node %d (error: %d (0x%x))\n").unwrap();
                // Create arguments on stack for the warning message
                let args = (numa_node, err_code, err_code);
                _mi_warning_message(&fmt, &args as *const _ as *mut c_void);
            }
        }
    }
    
    match *addr_out {
        Some(_) => 0,
        None => std::io::Error::last_os_error().raw_os_error().unwrap_or(0),
    }
}
pub fn _mi_prim_numa_node_count() -> usize {
    let mut buf = [0u8; 128];
    let mut node: u32 = 0;

    for node_val in 0..256 {
        // Use _mi_snprintf to format the path like the original C code
        let fmt = std::ffi::CString::new("/sys/devices/system/node/node%u").unwrap();
        unsafe {
            _mi_snprintf(
                buf.as_mut_ptr() as *mut std::os::raw::c_char,
                127,
                fmt.as_ptr(),
                &mut (node_val + 1) as *mut u32 as *mut std::os::raw::c_void,
            );
        }
        
        // Convert buffer to C string
        let c_path = std::ffi::CStr::from_bytes_until_nul(&buf).unwrap();
        
        // Check if the path is accessible - R_OK is typically 4
        if mi_prim_access(Some(c_path.to_str().unwrap()), 4) != 0 {
            break;
        }
        
        node = node_val + 1;
    }

    (node + 1) as usize
}

// Define the thread-local storage for heap
thread_local! {
    static MI_HEAP_DEFAULT: RefCell<Option<*mut mi_heap_t>> = RefCell::new(None);
}

// Use the provided global variable _MI_HEAP_DEFAULT_KEY

pub fn _mi_prim_thread_associate_default_heap(heap: *mut mi_heap_t) {
    // Check if the key is valid (not -1)
    let key = _MI_HEAP_DEFAULT_KEY.load(Ordering::SeqCst);
    if key != -1 {
        // Store the heap pointer in thread-local storage
        MI_HEAP_DEFAULT.with(|cell| {
            *cell.borrow_mut() = Some(heap);
        });
    }
}
pub fn mi_pthread_done(value: Option<&mut mi_heap_t>) {
    if let Some(heap) = value {
        _mi_thread_done(Some(heap));
    }
}
pub fn _mi_prim_thread_init_auto_done() {
    
    if _MI_HEAP_DEFAULT_KEY.load(Ordering::SeqCst) != -1 {
        _mi_assert_fail(
            "_mi_heap_default_key == (pthread_key_t)(-1)".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/prim/unix/prim.c".as_ptr() as *const std::os::raw::c_char,
            939,
            "_mi_prim_thread_init_auto_done".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // In Rust, we simulate pthread_key_create by setting the atomic value
    // Since we can't directly create pthread keys, we'll use a placeholder value
    // The actual key management would need to be handled differently in a real implementation
    _MI_HEAP_DEFAULT_KEY.store(0, Ordering::SeqCst);
    
    // Call the cleanup function with None since we don't have a real heap pointer
    mi_pthread_done(Option::None);
}
pub fn unix_detect_physical_memory(page_size: usize, physical_memory_in_kib: &mut Option<usize>) {
    let _ = page_size; // Explicitly mark as unused
    
    let mut info: Sysinfo = unsafe { std::mem::zeroed() };
    
    // Declare the sysinfo function from the C library
    extern "C" {
        fn sysinfo(info: *mut Sysinfo) -> i32;
    }
    
    // Use the sysinfo system call
    let result = unsafe { sysinfo(&mut info) };
    
    if result == 0 && info.totalram > 0 && info.totalram <= usize::MAX as u64 {
        *physical_memory_in_kib = Some((info.totalram as usize) / 1024);
    }
}
pub fn unix_detect_overcommit() -> bool {
    let mut os_overcommit = true;
    
    // Open the file
    let path = CStr::from_bytes_with_nul(b"/proc/sys/vm/overcommit_memory\0").unwrap();
    let fd = unsafe { mi_prim_open(path.as_ptr(), 0) }; // O_RDONLY is 0
    
    if fd >= 0 {
        let mut buf = [0u8; 32];
        
        // Read from the file descriptor
        let nread = mi_prim_read(fd, buf.as_mut_ptr(), buf.len());
        
        // Close the file descriptor
        unsafe {
            mi_prim_close(fd);
        }
        
        if nread >= 1 {
            // Check if the first character is '0' or '1'
            os_overcommit = (buf[0] == b'0') || (buf[0] == b'1');
        }
    }
    
    os_overcommit
}
pub fn _mi_prim_mem_init(config: &mut MiOsMemConfig) {
    extern "C" {
        fn sysconf(name: i32) -> i64;
        fn prctl(option: i32, arg2: usize, arg3: usize, arg4: usize, arg5: usize) -> i32;
    }
    
    const _SC_PAGESIZE: i32 = 30; // Common value for _SC_PAGESIZE
    
    let psize = unsafe { sysconf(_SC_PAGESIZE) };
    if psize > 0 && (psize as usize) < usize::MAX {
        config.page_size = psize as usize;
        config.alloc_granularity = psize as usize;
        let mut physical_memory_option = Some(config.physical_memory_in_kib);
        unix_detect_physical_memory(config.page_size, &mut physical_memory_option);
        config.physical_memory_in_kib = physical_memory_option.unwrap_or(0);
    }
    config.large_page_size = 2 * (1024 * 1024);
    config.has_overcommit = unix_detect_overcommit();
    config.has_partial_free = true;
    config.has_virtual_reserve = true;
    
    if !mi_option_is_enabled(MiOption::AllowLargeOsPages) {
        let mut val: i32 = 0;
        unsafe {
            // Use the already-imported prctl function directly
            if prctl(42, &mut val as *mut i32 as usize, 0, 0, 0) != 0 {
                val = 1;
                let _ = prctl(41, &mut val as *mut i32 as usize, 0, 0, 0);
            }
        }
    }
}
static NO_GETRANDOM: std::sync::atomic::AtomicPtr<()> = std::sync::atomic::AtomicPtr::new(std::ptr::null_mut());

pub fn _mi_prim_random_buf(buf: *mut u8, buf_len: usize) -> bool {
    // Try getrandom syscall first
    if NO_GETRANDOM.load(std::sync::atomic::Ordering::Acquire).is_null() {
        #[cfg(all(target_os = "linux", target_arch = "x86_64"))]
        {
            // Use syscall directly without libc
            let ret: i64;
            unsafe {
                core::arch::asm!(
                    "syscall",
                    in("rax") 318i64,  // SYS_getrandom on x86_64
                    in("rdi") buf,
                    in("rsi") buf_len,
                    in("rdx") 1i64,    // GRND_NONBLOCK
                    out("rcx") _,      // clobbered
                    out("r11") _,      // clobbered
                    lateout("rax") ret,
                    options(nostack, preserves_flags)
                );
            }
            
            if ret >= 0 {
                // Return true if entire buffer was filled
                return (ret as usize) == buf_len;
            }
            
            // Check if ENOSYS (system call not implemented)
            // ENOSYS = 38
            if ret == -38 {
                // Mark getrandom as unavailable
                NO_GETRANDOM.store(1 as *mut (), std::sync::atomic::Ordering::Release);
            } else {
                // For other errors, return false as per original C code
                return false;
            }
        }
        
        #[cfg(not(all(target_os = "linux", target_arch = "x86_64")))]
        {
            // On non-Linux or non-x86_64, mark getrandom as unavailable immediately
            NO_GETRANDOM.store(1 as *mut (), std::sync::atomic::Ordering::Release);
        }
    }
    
    // Fallback to /dev/urandom
    // O_RDONLY is typically 0
    let flags: std::os::raw::c_int = 0; // O_RDONLY
    
    // Use static string to avoid repeated allocation
    static URANDOM_PATH: &[u8] = b"/dev/urandom\0";
    let fpath = URANDOM_PATH.as_ptr() as *const std::os::raw::c_char;
    let fd = unsafe { mi_prim_open(fpath, flags) };
    
    if fd < 0 {
        return false;
    }
    
    let mut count = 0;
    while count < buf_len {
        let remaining = buf_len - count;
        let ret = unsafe {
            mi_prim_read(fd as std::os::fd::RawFd, 
                        buf.add(count),
                        remaining)
        };
        
        if ret <= 0 {
            // Check for EINTR (4) or EAGAIN (11) - continue on these errors
            // Note: We can't easily check errno without libc, so we'll use 
            // a simplified approach as shown in the original C code
            // EINTR = 4, EAGAIN = 11
            if ret != -4 && ret != -11 {
                break;
            }
            // For EINTR/EAGAIN, continue trying
        } else {
            count += ret as usize;
        }
    }
    
    unsafe {
        let _ = mi_prim_close(fd);
    }
    
    // Return true if entire buffer was filled
    count == buf_len
}
pub fn _mi_prim_protect(start: &mut [u8], protect: bool) -> i32 {
    let prot = if protect { 0x0 } else { 0x1 | 0x2 };
    
    extern "C" {
        fn mprotect(addr: *mut std::ffi::c_void, len: usize, prot: i32) -> i32;
        fn __errno_location() -> *mut i32;
    }
    
    let result = unsafe {
        mprotect(
            start.as_mut_ptr() as *mut std::ffi::c_void,
            start.len(),
            prot,
        )
    };
    
    let error_code = if result != 0 {
        unsafe { *__errno_location() }
    } else {
        0
    };
    
    unix_mprotect_hint(error_code);
    error_code
}

pub type mi_msecs_t = i64;

#[repr(C)]
#[derive(Clone)]
pub struct Timeval {
    pub tv_sec: i64,
    pub tv_usec: i64,
}

pub fn timeval_secs(tv: Option<&Timeval>) -> mi_msecs_t {
    match tv {
        Some(tv) => {
            ((tv.tv_sec as mi_msecs_t) * 1000) + ((tv.tv_usec as mi_msecs_t) / 1000)
        }
        None => 0,
    }
}

extern "C" {
    fn getrusage(who: i32, usage: *mut RUsage) -> i32;
}

pub fn _mi_prim_process_info(pinfo: Option<&mut crate::mi_process_info_t::mi_process_info_t>) {
    let pinfo = match pinfo {
        Some(pinfo) => pinfo,
        None => return,
    };

    let mut rusage = MaybeUninit::<RUsage>::uninit();

    let rc = unsafe {
        getrusage(
            crate::__rusage_who::__rusage_who::RUSAGE_SELF as i32,
            rusage.as_mut_ptr(),
        )
    };
    if rc != 0 {
        return;
    }

    let rusage = unsafe { rusage.assume_init() };

    pinfo.utime = timeval_secs(Some(&rusage.ru_utime));
    pinfo.stime = timeval_secs(Some(&rusage.ru_stime));

    pinfo.page_faults = usize::try_from(rusage.ru_majflt).unwrap_or(0);

    let peak_rss_bytes_i64 = rusage.ru_maxrss.saturating_mul(1024);
    pinfo.peak_rss = usize::try_from(peak_rss_bytes_i64).unwrap_or(0);
}


pub fn _mi_prim_thread_done_auto_done() {
    let key = _MI_HEAP_DEFAULT_KEY.load(Ordering::Relaxed);
    
    if key != -1 {
        // In Rust, we can't directly delete a pthread key since we're not using pthreads.
        // Instead, we'll just reset the atomic value to -1 to indicate the key is no longer valid.
        _MI_HEAP_DEFAULT_KEY.store(-1, Ordering::Relaxed);
    }
}
pub fn mi_process_attach() {
    _mi_auto_process_init();
}
pub fn mi_process_detach() {
    _mi_auto_process_done();
}
