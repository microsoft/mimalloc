use crate::*;
use std::cmp::Ordering;
use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;
use std::os::raw::c_int;
use std::os::raw::c_void;

pub fn _mi_toupper(c: char) -> char {
    if c >= 'a' && c <= 'z' {
        ((c as u8) - b'a' + b'A') as char
    } else {
        c
    }
}
pub fn _mi_strlcpy(dest: &mut [u8], src: &[u8]) {
    let mut src_idx = 0;
    let mut dest_idx = 0;
    let mut remaining = dest.len();

    // Check for null pointers or zero destination size
    if dest.is_empty() || src.is_empty() {
        return;
    }

    while src_idx < src.len() && src[src_idx] != 0 && remaining > 1 {
        dest[dest_idx] = src[src_idx];
        src_idx += 1;
        dest_idx += 1;
        remaining -= 1;
    }

    // Null-terminate the destination
    if dest_idx < dest.len() {
        dest[dest_idx] = 0;
    }
}
pub fn _mi_strlen(s: Option<&str>) -> usize {
    // Check if the input is None (equivalent to NULL in C)
    if s.is_none() {
        return 0;
    }
    
    // Unwrap safely: s is guaranteed to be Some(&str) here
    let s = s.unwrap();
    
    // Rust strings already know their length via .len()
    s.len()
}
pub fn _mi_strnlen(s: Option<&str>, max_len: usize) -> usize {
    // Check if s is None (equivalent to checking for NULL in C)
    if s.is_none() {
        return 0;
    }
    
    // Unwrap safely: If s is Some, it will be a valid string reference
    let s = s.unwrap();
    
    // Use Rust's built-in chars() iterator to count characters
    // Take only up to max_len characters and stop at null terminator
    s.chars()
        .take(max_len)
        .take_while(|&c| c != '\0')
        .count()
}
pub fn mi_outc(c: char, out: &mut Option<&mut [u8]>, end: usize) {
    // Take the current slice out of the Option
    if let Some(slice) = out.take() {
        // Check if we're at or past the end boundary
        // Compare the slice's starting pointer address with the end address
        if slice.is_empty() || slice.as_ptr() as usize >= end {
            // Put it back since we're not modifying it
            *out = Some(slice);
            return;
        }
        
        // Write the character to the current position
        slice[0] = c as u8;
        
        // Advance the slice by one position
        // This mimics: *out = p + 1;
        *out = Some(&mut slice[1..]);
    }
}
pub fn mi_outs(s: Option<&str>, out: &mut Option<&mut [u8]>, end: *const u8) {
    let s = match s {
        Some(s) => s,
        None => return,
    };
    
    let out_slice = match out.take() {
        Some(slice) => slice,
        None => return,
    };
    
    let mut s_idx = 0;
    let mut p_idx = 0;
    
    // Get the starting pointer of the output slice
    let p_start = out_slice.as_ptr();
    
    while s_idx < s.len() && p_idx < out_slice.len() {
        // Check if we've reached the null terminator in the source string
        // and if the current output position is before the end pointer
        let current_out_ptr = p_start.wrapping_add(p_idx) as *const u8;
        
        if s.as_bytes()[s_idx] != 0 && current_out_ptr < end {
            out_slice[p_idx] = s.as_bytes()[s_idx];
            s_idx += 1;
            p_idx += 1;
        } else {
            break;
        }
    }
    
    *out = Some(&mut out_slice[p_idx..]);
}
pub fn mi_out_fill(fill: char, len: usize, out: &mut *mut u8, end: *const u8) {
    let mut p = *out;
    let mut p_idx = 0;
    
    for i in 0..len {
        // Check if current position is before end pointer
        // Convert p.add(p_idx) to *const u8 for comparison with end
        if (p as *const u8).wrapping_add(p_idx) >= end {
            break;
        }
        
        // Write fill character
        unsafe {
            *p.add(p_idx) = fill as u8;
        }
        p_idx += 1;
    }
    
    // Update output pointer
    *out = unsafe { p.add(p_idx) };
}

pub fn mi_out_alignright(fill: char, start: &mut [u8], len: usize, extra: usize, end: usize) {
    if len == 0 || extra == 0 {
        return;
    }
    
    if start.len() < end {
        return;
    }
    
    let slice_end = len + extra;
    if slice_end > end {
        return;
    }
    
    // Move existing content to the right
    for i in (0..len).rev() {
        let src_idx = len - 1 - i;
        let dst_idx = (len + extra) - 1 - i;
        start[dst_idx] = start[src_idx];
    }
    
    // Fill the beginning with the fill character
    for i in 0..extra {
        start[i] = fill as u8;
    }
}
pub fn _mi_strnicmp(s: &str, t: &str, n: usize) -> i32 {
    if n == 0 {
        return 0;
    }

    let mut s_chars = s.chars();
    let mut t_chars = t.chars();
    let mut remaining = n;

    loop {
        match (s_chars.next(), t_chars.next()) {
            (Some(s_char), Some(t_char)) if remaining > 0 => {
                if _mi_toupper(s_char) != _mi_toupper(t_char) {
                    break;
                }
                remaining -= 1;
            }
            _ => break,
        }
    }

    if remaining == 0 {
        0
    } else {
        let s_next = s_chars.next().unwrap_or('\0');
        let t_next = t_chars.next().unwrap_or('\0');
        s_next as i32 - t_next as i32
    }
}
pub fn _mi_getenv(name: Option<&str>, result: &mut [u8]) -> bool {
    if name.is_none() || result.len() < 64 {
        return false;
    }
    _mi_prim_getenv(name, result)
}

pub fn mi_out_num(
    mut x: u64,
    base: usize,
    prefix: Option<char>,
    out: &mut Option<&mut [u8]>,
    end: usize,
) {
    // Handle special cases: x == 0, base == 0, or base > 16
    if x == 0 || base == 0 || base > 16 {
        if let Some(p) = prefix {
            mi_outc(p, out, end);
        }
        mi_outc('0', out, end);
        return;
    }

    let start_index = match out {
        Some(slice) => slice.len(),
        None => 0,
    };

    // Write digits in reverse order
    while x > 0 {
        let digit = (x % base as u64) as u8;
        let c = match digit.cmp(&9) {
            Ordering::Less => (b'0' + digit) as char,
            _ => (b'A' + digit - 10) as char,
        };
        mi_outc(c, out, end);
        x /= base as u64;
    }

    // Write prefix after digits (since we'll reverse)
    if let Some(p) = prefix {
        mi_outc(p, out, end);
    }

    // Reverse the digits we just wrote
    if let Some(slice) = out {
        let written_slice = &mut slice[start_index..];
        let len = written_slice.len();
        for i in 0..len / 2 {
            written_slice.swap(i, len - i - 1);
        }
    }
}
pub fn _mi_strlcat(dest: &mut [u8], src: &[u8]) {
    if dest.is_empty() || src.is_empty() {
        return;
    }

    let mut dest_idx = 0;
    let mut remaining = dest.len();

    // Find the end of the existing string in dest
    while dest_idx < dest.len() && dest[dest_idx] != 0 && remaining > 1 {
        dest_idx += 1;
        remaining -= 1;
    }

    // Copy src to the end of dest
    _mi_strlcpy(&mut dest[dest_idx..], src);
}

pub unsafe extern "C" fn _mi_snprintf(
    buf: *mut c_char,
    buflen: usize,
    fmt: *const c_char,
    mut args: *mut c_void,
) -> c_int {
    // Basic validation (avoids UB on null pointers in the translated Rust codebase).
    if fmt.is_null() {
        return -1;
    }
    if buflen != 0 && buf.is_null() {
        return -1;
    }

    // Use _mi_vsnprintf to handle the variadic arguments
    // We assume args is a va_list pointer
    let written = _mi_vsnprintf(buf, buflen, fmt, args);
    
    // _mi_vsnprintf returns the number of characters that would have been written
    // (excluding null terminator) if buflen was large enough
    if written < 0 {
        -1
    } else {
        written
    }
}

// Declare vsnprintf from libc manually since it's not exposed by the libc crate
extern "C" {
    fn vsnprintf(
        buf: *mut c_char,
        buflen: libc::size_t,
        fmt: *const c_char,
        args: *mut libc::c_void,
    ) -> c_int;
}

// Implement _mi_vsnprintf using libc's vsnprintf
#[allow(improper_ctypes_definitions)]
pub unsafe fn _mi_vsnprintf(
    buf: *mut c_char,
    buflen: usize,
    fmt: *const c_char,
    args: *mut c_void,
) -> c_int {
    // If args is a va_list, we use libc's vsnprintf
    // Cast args to libc's va_list type
    if buf.is_null() && buflen > 0 {
        return -1;
    }
    if fmt.is_null() {
        return -1;
    }
    
    // Use vsnprintf with the va_list
    // Note: args is expected to be a pointer to va_list
    vsnprintf(buf, buflen, fmt, args as *mut libc::c_void)
}
pub fn mi_byte_sum64(x: u64) -> usize {
    let mut x = x;
    x += x << 8;
    x += x << 16;
    x += x << 32;
    (x >> 56) as usize
}
pub fn mi_popcount_generic64(x: u64) -> usize {
    let mut x = x;
    x = x - ((x >> 1) & 0x5555555555555555);
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0F;
    mi_byte_sum64(x)
}
pub fn _mi_popcount_generic(x: usize) -> usize {
    if x <= 1 {
        return x;
    }
    if !x == 0 {
        return (1 << 3) * 8;
    }
    mi_popcount_generic64(x as u64)
}
