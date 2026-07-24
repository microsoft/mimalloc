use crate::*;
use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;
use std::os::raw::c_void;
use std::ptr::null_mut;
use std::ptr;


pub fn mi_wdupenv_s(
    buf: Option<&mut *mut u16>,
    size: Option<&mut usize>,
    name: Option<&u16>,
) -> i32 {
    // Check for null pointers (converted to None in Rust)
    if buf.is_none() || name.is_none() {
        return 22;
    }

    // Unwrap the mutable reference to buf
    let buf_ref = buf.unwrap();
    
    // Set size to 0 if provided
    if let Some(size_ref) = size {
        *size_ref = 0;
    }

    // Set buf to null pointer
    *buf_ref = ptr::null_mut();

    22
}

pub fn mi__expand(p: Option<&mut ()>, newsize: usize) -> Option<&mut ()> {
    let res = mi_expand(p, newsize);
    
    if res.is_none() {
        // In Rust, we don't directly set errno like in C.
        // The error handling is typically done through Result/Option types.
        // Since this function returns Option, the caller can check for None.
        // If errno access is needed elsewhere, consider using std::io::Error
        // or a custom error type instead of global errno.
    }
    
    res
}
pub fn mi_malloc_size(p: Option<&[u8]>) -> usize {
    mi_usable_size(p)
}
pub fn mi_malloc_good_size(size: usize) -> usize {
    mi_good_size(size)
}
pub fn mi_malloc_usable_size(p: Option<&[u8]>) -> usize {
    mi_usable_size(p)
}

pub fn mi_reallocarray(p: Option<*mut c_void>, count: usize, size: usize) -> Option<*mut c_void> {
    let newp = mi_reallocn(p, count, size);
    
    // Note: In the original C code, errno would be set to 12 (ENOMEM) here
    // if newp is null, but we can't set errno without libc
    // In Rust, we could use std::io::Error::last_os_error() on some platforms,
    // but we'll leave it as is for now.
    
    newp
}
pub fn mi_aligned_recalloc(
    p: Option<&mut [u8]>,
    newcount: usize,
    size: usize,
    alignment: usize,
) -> Option<&mut [u8]> {
    mi_recalloc_aligned(p, newcount, size, alignment)
}
pub fn mi_aligned_offset_recalloc<'a>(
    p: Option<&'a mut [u8]>,
    newcount: usize,
    size: usize,
    alignment: usize,
    offset: usize,
) -> Option<&'a mut [u8]> {
    mi_recalloc_aligned_at(p, newcount, size, alignment, offset)
}

pub fn mi_mbsdup(s: Option<&CStr>) -> Option<CString> {
    mi_strdup(s)
}

pub fn mi_cfree(p: Option<*mut c_void>) {
    if mi_is_in_heap_region(p.map(|ptr| ptr as *const ())) {
        mi_free(p);
    }
}

pub fn mi_memalign(alignment: usize, size: usize) -> Option<*mut u8> {
    let p = mi_malloc_aligned(size, alignment);
    
    if let Some(ptr) = p {
        let ptr_value = ptr as usize;
        if ptr_value % alignment != 0 {
            let assertion = CString::new("((uintptr_t)p % alignment) == 0").unwrap();
            let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-posix.c").unwrap();
            let func = CString::new("mi_memalign").unwrap();
            
            _mi_assert_fail(
                assertion.as_ptr(),
                fname.as_ptr(),
                71,
                func.as_ptr(),
            );
        }
    }
    
    p
}
pub fn mi_valloc(size: usize) -> Option<*mut u8> {
    mi_memalign(_mi_os_page_size(), size)
}

pub fn mi_aligned_alloc(alignment: usize, size: usize) -> Option<*mut u8> {
    let p = mi_malloc_aligned(size, alignment);
    
    if let Some(ptr) = p {
        let aligned = (ptr as usize) % alignment == 0;
        if !aligned {
            let assertion = CString::new("((uintptr_t)p % alignment) == 0").unwrap();
            let fname = CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-posix.c").unwrap();
            let func = CString::new("mi_aligned_alloc").unwrap();
            _mi_assert_fail(assertion.as_ptr(), fname.as_ptr(), 98, func.as_ptr());
        }
    }
    
    p
}

pub fn mi_reallocarr(p: Option<&mut *mut c_void>, count: usize, size: usize) -> i32 {
    // Check for NULL pointer (equivalent to C's assert)
    if p.is_none() {
        _mi_assert_fail(
            "p != NULL\0".as_ptr() as *const _,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-posix.c\0".as_ptr() as *const _,
            109,
            "mi_reallocarr\0".as_ptr() as *const _,
        );
        return 22; // EINVAL
    }

    let p = p.unwrap();
    
    // Get the current pointer value
    let current_ptr = *p;
    
    // Call mi_reallocarray with the current pointer
    let newp = mi_reallocarray(
        if current_ptr.is_null() {
            None
        } else {
            Some(current_ptr)
        },
        count,
        size,
    );

    // Check if allocation failed
    if newp.is_none() {
        return std::io::Error::last_os_error().raw_os_error().unwrap_or(22);
    }

    // Update the pointer with the new allocation
    *p = newp.unwrap();
    
    0 // Success
}

pub fn mi_wcsdup(s: Option<&[u16]>) -> Option<Box<[u16]>> {
    // Check for NULL pointer (None in Rust)
    let s = s?;

    // Calculate length of the wide string (excluding null terminator)
    let len = s.iter().position(|&c| c == 0).unwrap_or(s.len());

    // Allocate memory for the new string (including null terminator)
    let size = (len + 1) * std::mem::size_of::<u16>();
    let p_ptr = mi_malloc(size) as *mut u16;

    if p_ptr.is_null() {
        return None;
    }

    // Create a mutable slice from the allocated memory
    let p_slice = unsafe { std::slice::from_raw_parts_mut(p_ptr, len + 1) };

    // Copy the source string
    p_slice[..len].copy_from_slice(&s[..len]);
    // Add null terminator
    p_slice[len] = 0;

    // Convert to Box<[u16]> for safe ownership
    Some(unsafe { Box::from_raw(std::slice::from_raw_parts_mut(p_ptr, len + 1)) })
}
pub fn mi_dupenv_s(
    buf: Option<&mut Option<CString>>,
    mut size: Option<&mut usize>,
    name: Option<&CStr>,
) -> i32 {
    // Check for null pointers (represented as None in Rust)
    if buf.is_none() || name.is_none() {
        return 22;
    }

    // Unwrap the parameters safely
    let buf = buf.unwrap();
    let name = name.unwrap();

    // Initialize size to 0 if provided
    if let Some(size_ref) = size.as_mut() {
        **size_ref = 0;
    }

    // Convert CStr to Rust string for getenv
    let name_str = match name.to_str() {
        Ok(s) => s,
        Err(_) => {
            // If the name is not valid UTF-8, we can't look it up
            *buf = None;
            return 0;
        }
    };

    // Get environment variable using Rust's std::env
    match std::env::var_os(name_str) {
        Some(os_value) => {
            // Convert OsString to CString
            match CString::new(os_value.to_string_lossy().into_owned()) {
                Ok(c_string) => {
                    *buf = Some(c_string);
                    
                    // Update size if provided
                    if let Some(size_ref) = size.as_mut() {
                        // Get the length of the original environment variable value
                        let env_str = os_value.to_string_lossy();
                        **size_ref = _mi_strlen(Some(&env_str));
                    }
                    0
                }
                Err(_) => {
                    *buf = None;
                    12 // Return error 12 if conversion failed (e.g., contains null bytes)
                }
            }
        }
        None => {
            *buf = None;
            0
        }
    }
}

pub fn mi_posix_memalign(p: Option<&mut *mut u8>, alignment: usize, size: usize) -> i32 {
    // Check if p is None (equivalent to NULL in C)
    if p.is_none() {
        return 22;
    }
    
    // Unwrap p safely since we know it's Some
    let p = p.unwrap();
    
    // Check alignment requirements
    if (alignment % std::mem::size_of::<*mut u8>()) != 0 {
        return 22;
    }
    
    if alignment == 0 || !_mi_is_power_of_two(alignment) {
        return 22;
    }
    
    // Allocate aligned memory
    let q = mi_malloc_aligned(size, alignment);
    
    // Check for allocation failure
    if q.is_none() && size != 0 {
        return 12;
    }
    
    // Unwrap q safely
    let q = q.unwrap();
    
    // Verify alignment
    if (q as usize) % alignment != 0 {
        let assertion = std::ffi::CString::new("((uintptr_t)q % alignment) == 0").unwrap();
        let fname = std::ffi::CString::new("/workdir/C2RustTranslation-main/subjects/mimalloc/src/alloc-posix.c").unwrap();
        let func = std::ffi::CString::new("mi_posix_memalign").unwrap();
        
        _mi_assert_fail(
            assertion.as_ptr(),
            fname.as_ptr(),
            64,
            func.as_ptr(),
        );
    }
    
    // Assign the result
    *p = q;
    
    0
}
pub fn mi_pvalloc(size: usize) -> Option<*mut u8> {
    let psize = _mi_os_page_size();
    
    if size >= (usize::MAX - psize) {
        return None;
    }
    
    let asize = _mi_align_up(size, psize);
    mi_malloc_aligned(asize, psize)
}
