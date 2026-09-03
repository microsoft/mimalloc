use crate::*;
use crate::MiOutputFun;
use crate::int64_t;
use crate::mi_stat_count_t::mi_stat_count_t;
use crate::mi_stat_counter_t::mi_stat_counter_t;
use lazy_static::lazy_static;
use std::any::Any;
use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;
use std::os::raw::c_int;
use std::os::raw::c_void;
use std::sync::atomic::AtomicI64;
use std::sync::atomic::Ordering;
use std::time::SystemTime;
use std::time::UNIX_EPOCH;
pub fn mi_stat_update_mt(stat: &crate::mi_stat_count_t::mi_stat_count_t, amount: i64) {
    if amount == 0 {
        return;
    }
    
    let current = mi_atomic_addi64_relaxed(unsafe { &*(&stat.current as *const i64 as *const AtomicI64) }, amount);
    mi_atomic_maxi64_relaxed(unsafe { &*(&stat.peak as *const i64 as *const AtomicI64) }, current + amount);
    
    if amount > 0 {
        mi_atomic_addi64_relaxed(unsafe { &*(&stat.total as *const i64 as *const AtomicI64) }, amount);
    }
}

pub fn mi_atomic_addi64_relaxed(p: &AtomicI64, add: i64) -> i64 {
    p.fetch_add(add, Ordering::Relaxed)
}

pub fn mi_atomic_maxi64_relaxed(p: &AtomicI64, x: i64) {
    let mut current = p.load(Ordering::Relaxed);
    while current < x {
        match p.compare_exchange_weak(current, x, Ordering::Relaxed, Ordering::Relaxed) {
            Ok(_) => break,
            Err(new_current) => current = new_current,
        }
    }
}
pub fn mi_stat_increase_mt(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, amount: usize) {
    unsafe {
        mi_stat_update_mt(stat, amount as int64_t);
    }
}
pub fn __mi_stat_decrease_mt(stat: *mut crate::mi_stat_count_t::mi_stat_count_t, amount: usize) {
    // Call mi_stat_update_mt with the negative amount
    unsafe {
        if !stat.is_null() {
            crate::mi_stat_update_mt(&*stat, -((amount as i64)));
        }
    }
}
pub unsafe extern "C" fn __mi_stat_counter_increase_mt(stat: *mut crate::mi_stat_counter_t::mi_stat_counter_t, amount: usize) {
    let atomic_total = &(*stat).total as *const i64 as *const std::sync::atomic::AtomicI64;
    mi_atomic_addi64_relaxed(unsafe { &*atomic_total }, amount as i64);
}

pub type mi_msecs_t = i64;

pub fn _mi_clock_now() -> mi_msecs_t {
    _mi_prim_clock_now()
}

fn _mi_prim_clock_now() -> mi_msecs_t {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards")
        .as_millis() as mi_msecs_t
}
pub fn mi_stat_update(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, amount: i64) {
    if amount == 0 {
        return;
    }
    
    stat.current += amount;
    
    if stat.current > stat.peak {
        stat.peak = stat.current;
    }
    
    if amount > 0 {
        stat.total += amount;
    }
}
pub fn __mi_stat_decrease(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, amount: usize) {
    mi_stat_update(stat, -((amount as i64)));
}
pub fn __mi_stat_increase(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, amount: usize) {
    mi_stat_update(stat, amount as i64);
}

pub fn mi_stat_counter_add_mt(stat: &mut crate::mi_stat_counter_t::mi_stat_counter_t, src: &crate::mi_stat_counter_t::mi_stat_counter_t) {
    // Check if the pointers are equal (comparing references in Rust)
    if std::ptr::eq(stat, src) {
        return;
    }
    
    // Get atomic references to the i64 fields
    let stat_total = unsafe { &*((&stat.total as *const i64).cast::<AtomicI64>()) };
    let src_total = unsafe { &*((&src.total as *const i64).cast::<AtomicI64>()) };
    
    // Use the provided atomic function with atomic references
    mi_atomic_void_addi64_relaxed(stat_total, src_total);
}
pub fn mi_stat_count_add_mt(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, src: &crate::mi_stat_count_t::mi_stat_count_t) {
    if std::ptr::eq(stat, src) {
        return;
    }
    
    // Since fields are i64 not AtomicI64, use regular addition
    stat.total += src.total;
    stat.current += src.current;
    stat.peak += src.peak;
}
pub fn mi_stats_add(
    stats: &mut crate::mi_stats_t::mi_stats_t,
    src: &crate::mi_stats_t::mi_stats_t,
) {
    if std::ptr::eq(stats, src) {
        return;
    }

    #[inline]
    fn add_count(
        dst: &mut crate::mi_stat_count_t::mi_stat_count_t,
        src: &crate::mi_stat_count_t::mi_stat_count_t,
    ) {
        dst.total += src.total;
        dst.peak += src.peak;
        dst.current += src.current;
    }

    #[inline]
    fn add_counter(
        dst: &mut crate::mi_stat_counter_t::mi_stat_counter_t,
        src: &crate::mi_stat_counter_t::mi_stat_counter_t,
    ) {
        dst.total += src.total;
    }

    add_count(&mut stats.pages, &src.pages);
    add_count(&mut stats.reserved, &src.reserved);
    add_count(&mut stats.committed, &src.committed);
    add_count(&mut stats.reset, &src.reset);
    add_count(&mut stats.purged, &src.purged);
    add_count(&mut stats.page_committed, &src.page_committed);
    add_count(&mut stats.pages_abandoned, &src.pages_abandoned);
    add_count(&mut stats.threads, &src.threads);
    add_count(&mut stats.malloc_normal, &src.malloc_normal);
    add_count(&mut stats.malloc_huge, &src.malloc_huge);
    add_count(&mut stats.malloc_requested, &src.malloc_requested);

    add_counter(&mut stats.mmap_calls, &src.mmap_calls);
    add_counter(&mut stats.commit_calls, &src.commit_calls);
    add_counter(&mut stats.reset_calls, &src.reset_calls);
    add_counter(&mut stats.purge_calls, &src.purge_calls);
    add_counter(&mut stats.arena_count, &src.arena_count);
    add_counter(&mut stats.malloc_normal_count, &src.malloc_normal_count);
    add_counter(&mut stats.malloc_huge_count, &src.malloc_huge_count);
    add_counter(&mut stats.malloc_guarded_count, &src.malloc_guarded_count);
    add_counter(&mut stats.arena_rollback_count, &src.arena_rollback_count);
    add_counter(&mut stats.arena_purges, &src.arena_purges);
    add_counter(&mut stats.pages_extended, &src.pages_extended);
    add_counter(&mut stats.pages_retire, &src.pages_retire);
    add_counter(&mut stats.page_searches, &src.page_searches);

    add_count(&mut stats.segments, &src.segments);
    add_count(&mut stats.segments_abandoned, &src.segments_abandoned);
    add_count(&mut stats.segments_cache, &src.segments_cache);
    add_count(&mut stats._segments_reserved, &src._segments_reserved);

    add_counter(&mut stats.pages_reclaim_on_alloc, &src.pages_reclaim_on_alloc);
    add_counter(&mut stats.pages_reclaim_on_free, &src.pages_reclaim_on_free);
    add_counter(&mut stats.pages_reabandon_full, &src.pages_reabandon_full);
    add_counter(
        &mut stats.pages_unabandon_busy_wait,
        &src.pages_unabandon_busy_wait,
    );

    for i in 0..74 {
        add_count(&mut stats.malloc_bins[i], &src.malloc_bins[i]);
    }
    for i in 0..74 {
        add_count(&mut stats.page_bins[i], &src.page_bins[i]);
    }
}
pub fn _mi_stats_merge_from(to: Option<&mut crate::mi_stats_t::mi_stats_t>, from: Option<&mut crate::mi_stats_t::mi_stats_t>) {
    // Check for NULL pointers using Option
    if to.is_none() || from.is_none() {
        crate::super_function_unit5::_mi_assert_fail(
            "to != NULL && from != NULL".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/stats.c".as_ptr() as *const std::os::raw::c_char,
            410,
            "_mi_stats_merge_from".as_ptr() as *const std::os::raw::c_char,
        );
        return;
    }

    let to = to.unwrap();
    let from = from.unwrap();

    // Check if pointers are different
    if !std::ptr::eq(to as *const _, from as *const _) {
        crate::mi_stats_add(to, from);
        
        // Zero out the source struct
        let from_bytes = unsafe {
            std::slice::from_raw_parts_mut(from as *mut crate::mi_stats_t::mi_stats_t as *mut u8, std::mem::size_of::<crate::mi_stats_t::mi_stats_t>())
        };
        crate::_mi_memzero(from_bytes, std::mem::size_of::<crate::mi_stats_t::mi_stats_t>());
    }
}
pub fn _mi_stats_merge_thread(mut tld: Option<&mut mi_tld_t>) {
    // Check both conditions as in the original C code
    let tld_ref = match tld {
        Some(ref mut t) => t,
        Option::None => {
            crate::super_function_unit5::_mi_assert_fail(
                "tld != NULL && tld->subproc != NULL\0".as_ptr() as *const std::os::raw::c_char,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/stats.c\0".as_ptr() as *const std::os::raw::c_char,
                422,
                "_mi_stats_merge_thread\0".as_ptr() as *const std::os::raw::c_char,
            );
            return;
        }
    };
    
    if tld_ref.subproc.is_none() {
        crate::super_function_unit5::_mi_assert_fail(
            "tld != NULL && tld->subproc != NULL\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/stats.c\0".as_ptr() as *const std::os::raw::c_char,
            422,
            "_mi_stats_merge_thread\0".as_ptr() as *const std::os::raw::c_char,
        );
        return;
    }
    
    // Now we know both tld and subproc exist
    if let Some(subproc) = &mut tld_ref.subproc {
        _mi_stats_merge_from(
            Some(&mut subproc.stats),
            Some(&mut tld_ref.stats),
        );
    }
}
pub fn mi_stat_adjust_mt(stat: &mut mi_stat_count_t, amount: i64) {
    if amount == 0 {
        return;
    }
    
    // Since stat fields are regular i64, use regular addition
    stat.current += amount;
    stat.total += amount;
    
    // Update peak if current exceeds it
    if stat.current > stat.peak {
        stat.peak = stat.current;
    }
}
pub fn __mi_stat_adjust_increase_mt(stat: &mut mi_stat_count_t, amount: usize) {
    // Inline the logic of mi_stat_adjust_mt for increase
    stat.current += amount as i64;
    stat.total += amount as i64;
    if stat.current > stat.peak {
        stat.peak = stat.current;
    }
}
pub fn __mi_stat_adjust_decrease_mt(stat: &mut mi_stat_count_t, amount: usize) {
    mi_stat_adjust_mt(stat, -((amount as i64)));
}
pub fn __mi_stat_counter_increase(stat: &mut mi_stat_counter_t, amount: usize) {
    stat.total += amount as int64_t;
}
pub fn _mi_clock_start() -> mi_msecs_t {
    if MI_CLOCK_DIFF.load(Ordering::Relaxed) == 0 {
        let t0 = _mi_clock_now();
        let diff = _mi_clock_now() - t0;
        MI_CLOCK_DIFF.store(diff, Ordering::Relaxed);
    }
    _mi_clock_now()
}

lazy_static! {
    pub static ref mi_process_start: AtomicI64 = AtomicI64::new(0);
}

pub fn _mi_stats_init() {
    if mi_process_start.load(Ordering::Relaxed) == 0 {
        mi_process_start.store(_mi_clock_start(), Ordering::Relaxed);
    }
}

pub fn _mi_clock_end(start: mi_msecs_t) -> mi_msecs_t {
    let end = _mi_clock_now();
    (end - start) - MI_CLOCK_DIFF.load(Ordering::Relaxed)
}
pub fn _mi_stats_done(stats: Option<&mut crate::mi_stats_t::mi_stats_t>) {
    // Get the global subprocess stats mutex
    let subproc = _mi_subproc();
    
    // Lock the mutex to get mutable access to the subprocess stats
    let mut subproc_guard = subproc.lock().unwrap();
    
    // Call the merge function with mutable references to both stats
    _mi_stats_merge_from(
        Some(&mut subproc_guard.stats),
        stats
    );
}
pub fn mi_stat_adjust(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, amount: i64) {
    if amount == 0 {
        return;
    }
    stat.current += amount;
    stat.total += amount;
}
pub fn __mi_stat_adjust_decrease(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, amount: usize) {
    mi_stat_adjust(stat, -((amount as i64)));
}
pub fn mi_stats_merge() {
    let tld_ptr = _mi_thread_tld();
    let tld = unsafe { tld_ptr.as_mut() };
    _mi_stats_merge_thread(tld);
}
pub fn mi_print_header(out: Option<MiOutputFun>, arg: Option<&mut dyn std::any::Any>) {
    
    let heap_stats = CStr::from_bytes_with_nul(b"heap stats\0").unwrap();
    let peak = CStr::from_bytes_with_nul(b"peak   \0").unwrap();
    let total = CStr::from_bytes_with_nul(b"total   \0").unwrap();
    let current = CStr::from_bytes_with_nul(b"current   \0").unwrap();
    let block = CStr::from_bytes_with_nul(b"block   \0").unwrap();
    let total_num = CStr::from_bytes_with_nul(b"total#   \0").unwrap();
    
    let format = CStr::from_bytes_with_nul(b"%10s: %11s %11s %11s %11s %11s\n\0").unwrap();
    
    let out = match out {
        Some(out) => out,
        None => {
            let mut parg: *mut () = core::ptr::null_mut();
            mi_out_get_default(Some(&mut parg))
        }
    };
    
    // Format the string using Rust's formatting
    let formatted = format!(
        "{:>10}: {:>11} {:>11} {:>11} {:>11} {:>11}\n",
        heap_stats.to_str().unwrap(),
        peak.to_str().unwrap(),
        total.to_str().unwrap(),
        current.to_str().unwrap(),
        block.to_str().unwrap(),
        total_num.to_str().unwrap()
    );
    
    // Call the output function directly
    out(&formatted, arg);
}

// Remove duplicate type definitions since they're provided in dependencies
// pub type int64_t = i64;  // Already defined in dependency
// pub type MiOutputFun = fn(msg: &str, arg: Option<&mut dyn std::any::Any>);  // Already defined in dependency

pub fn _mi_snprintf(
    buf: *mut c_char,
    buflen: usize,
    fmt: *const c_char,
    mut args: *mut c_void,
) -> c_int {
    if fmt.is_null() {
        return -1;
    }
    if buflen != 0 && buf.is_null() {
        return -1;
    }

    let written = unsafe { _mi_vsnprintf(buf, buflen, fmt, args) };
    
    if written < 0 {
        -1
    } else {
        written
    }
}

// Use _mi_vsnprintf from libc_new module
use crate::libc_new::_mi_vsnprintf;

pub fn _mi_fprintf(
    out: Option<MiOutputFun>,
    arg: Option<&mut dyn std::any::Any>,
    fmt: *const c_char,
    buf: *const c_char,
) {
    if fmt.is_null() || buf.is_null() {
        return;
    }
    
    let fmt_cstr = unsafe { CStr::from_ptr(fmt) };
    let buf_cstr = unsafe { CStr::from_ptr(buf) };
    
    let fmt_str = fmt_cstr.to_string_lossy();
    let buf_str = buf_cstr.to_string_lossy();
    
    // Format according to the format string
    let formatted = if fmt_str.contains("%s") {
        format!("{}", buf_str)
    } else {
        buf_str.to_string()
    };
    
    if let Some(out_fn) = out {
        out_fn(&formatted, arg);
    }
}

pub fn mi_printf_amount(
    n: int64_t,
    unit: int64_t,
    out: Option<MiOutputFun>,
    arg: Option<&mut dyn std::any::Any>,
    fmt: *const c_char,
) {
    let mut buf: [c_char; 32] = [0; 32];
    let len = 32;
    
    // Clear buffer (equivalent to memset in original C)
    for i in 0..len {
        buf[i] = 0;
    }
    
    let suffix = if unit <= 0 { " " } else { "B" };
    let base = if unit == 0 { 1000 } else { 1024 };
    
    let mut n = n;
    if unit > 0 {
        n *= unit;
    }
    
    let pos = if n < 0 { -n } else { n };
    
    if pos < base {
        if n != 1 || suffix.chars().next().unwrap() != 'B' {
            let suffix_str = if n == 0 { "" } else { suffix };
            let fmt_str = CString::new("%lld   %-3s").unwrap();
            unsafe {
                _mi_snprintf(
                    buf.as_mut_ptr(),
                    len,
                    fmt_str.as_ptr(),
                    &mut n as *mut int64_t as *mut c_void,
                );
            }
        }
    } else {
        let mut divider = base;
        let mut magnitude = "K";
        
        if pos >= (divider * base) {
            divider *= base;
            magnitude = "M";
        }
        if pos >= (divider * base) {
            divider *= base;
            magnitude = "G";
        }
        
        let tens = n / (divider / 10);
        let whole = (tens / 10) as i64;
        let frac1 = (tens % 10) as i64;
        
        let mut unitdesc: [c_char; 8] = [0; 8];
        let i_str = if base == 1024 { "i" } else { "" };
        let unitdesc_fmt = CString::new("%s%s%s").unwrap();
        unsafe {
            _mi_snprintf(
                unitdesc.as_mut_ptr(),
                8,
                unitdesc_fmt.as_ptr(),
                &mut (magnitude, i_str, suffix) as *mut (&str, &str, &str) as *mut c_void,
            );
        }
        
        let frac1_abs = if frac1 < 0 { -frac1 } else { frac1 };
        let buf_fmt = CString::new("%ld.%ld %-3s").unwrap();
        unsafe {
            _mi_snprintf(
                buf.as_mut_ptr(),
                len,
                buf_fmt.as_ptr(),
                &mut (whole, frac1_abs, unitdesc.as_ptr()) as *mut (i64, i64, *const c_char) as *mut c_void,
            );
        }
    }
    
    let default_fmt = CString::new("%12s").unwrap();
    let fmt_to_use = if fmt.is_null() {
        default_fmt.as_ptr()
    } else {
        fmt
    };
    
    _mi_fprintf(out, arg, fmt_to_use, buf.as_ptr());
}
pub fn mi_print_amount(
    n: int64_t,
    unit: int64_t,
    out: Option<MiOutputFun>,
    arg: Option<&mut dyn std::any::Any>,
) {
    // Use null pointer for fmt parameter as per original C code
    let fmt = std::ptr::null() as *const c_char;
    mi_printf_amount(n, unit, out, arg, fmt);
}

pub fn mi_print_count(
    n: int64_t,
    unit: int64_t,
    out: Option<MiOutputFun>,
    arg: Option<&mut dyn Any>,
) {
    if unit == 1 {
        // C: _mi_fprintf(out, arg, "%12s", " "); -> 12 spaces total.
        let out = match out {
            Some(out) => out,
            None => mi_out_get_default(None),
        };
        out("            ", arg);
    } else {
        mi_print_amount(n, 0, out, arg);
    }
}
pub fn mi_stat_print_ex(
    stat: &mi_stat_count_t,
    msg: &str,
    unit: int64_t,
    out: Option<MiOutputFun>,
    mut arg: Option<&mut dyn Any>,  // Changed to mutable
    notok: Option<&str>,
) {
    // Line 3: Print the message label
    if let Some(out_fn) = out {
        out_fn(&format!("{:>10}:", msg), arg.as_deref_mut());
    }

    // Line 4: Check if unit is not zero
    if unit != 0 {
        // Lines 6-28: Handle positive and negative units
        if unit > 0 {
            // Lines 8-12: Positive unit case
            mi_print_amount(stat.peak, unit, out, arg.as_deref_mut());
            mi_print_amount(stat.total, unit, out, arg.as_deref_mut());
            mi_print_amount(stat.current, unit, out, arg.as_deref_mut());
            mi_print_amount(unit, 1, out, arg.as_deref_mut());
            mi_print_count(stat.total, unit, out, arg.as_deref_mut());
        } else {
            // Lines 16-27: Negative unit case
            mi_print_amount(stat.peak, -1, out, arg.as_deref_mut());
            mi_print_amount(stat.total, -1, out, arg.as_deref_mut());
            mi_print_amount(stat.current, -1, out, arg.as_deref_mut());
            
            if unit == -1 {
                // Lines 21-22: Special case for unit == -1
                if let Some(out_fn) = out {
                    out_fn(&format!("{:>24}", ""), arg.as_deref_mut());
                }
            } else {
                // Lines 25-26: General negative unit case
                mi_print_amount(-unit, 1, out, arg.as_deref_mut());
                mi_print_count(stat.total / (-unit), 0, out, arg.as_deref_mut());
            }
        }
        
        // Lines 29-38: Print status message
        if stat.current != 0 {
            if let Some(out_fn) = out {
                out_fn("  ", arg.as_deref_mut());
                let message = notok.unwrap_or("not all freed");
                out_fn(message, arg.as_deref_mut());
                out_fn("\n", arg.as_deref_mut());
            }
        } else {
            if let Some(out_fn) = out {
                out_fn("  ok\n", arg.as_deref_mut());
            }
        }
    } else {
        // Lines 42-46: Unit is zero case
        mi_print_amount(stat.peak, 1, out, arg.as_deref_mut());
        mi_print_amount(stat.total, 1, out, arg.as_deref_mut());
        
        if let Some(out_fn) = out {
            out_fn(&format!("{:>11}", " "), arg.as_deref_mut());
        }
        
        mi_print_amount(stat.current, 1, out, arg.as_deref_mut());
        
        if let Some(out_fn) = out {
            out_fn("\n", arg.as_deref_mut());
        }
    }
}
pub fn mi_stat_print(
    stat: &crate::mi_stat_count_t::mi_stat_count_t,
    msg: &str,
    unit: int64_t,
    out: Option<MiOutputFun>,
    mut arg: Option<&mut dyn std::any::Any>,
) {
    mi_stat_print_ex(stat, msg, unit, out, arg, Option::<&str>::None);
}
pub fn mi_stats_print_bins(
    bins: &[crate::mi_stat_count_t::mi_stat_count_t],
    max: usize,
    fmt: &CStr,
    out: Option<crate::MiOutputFun>,
    mut arg: Option<&mut dyn std::any::Any>,
) {
    let mut found = false;
    let mut buf = [0u8; 64];
    
    for i in 0..=max {
        if bins[i].total > 0 {
            found = true;
            let unit = _mi_bin_size(i as usize);
            
            // Format the string as in the original C code: "%s %3lu"
            let fmt_str = CStr::from_bytes_with_nul(b"%s %3lu\0").unwrap();
            unsafe {
                _mi_snprintf(
                    buf.as_mut_ptr() as *mut c_char,
                    buf.len(),
                    fmt_str.as_ptr(),
                    // Pass both arguments: fmt and i
                    // We need to create a va_list-like structure
                    // Since _mi_snprintf expects variadic arguments via *mut c_void,
                    // we pass a pointer to an array containing the arguments
                    {
                        let args: [*mut c_void; 2] = [
                            fmt.as_ptr() as *mut c_void,
                            i as *mut c_void,
                        ];
                        args.as_ptr() as *mut c_void
                    },
                );
            }
            
            // Convert buffer to CStr for mi_stat_print
            let msg = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
            
            // Pass arg directly (it's already mutable)
            mi_stat_print(&bins[i], msg.to_str().unwrap_or(""), unit as i64, out, arg.as_deref_mut());
        }
    }
    
    if found {
        // Use _mi_fprintf to output newline as in the original C code
        let newline = CStr::from_bytes_with_nul(b"\n").unwrap();
        unsafe {
            // _mi_fprintf expects 4 arguments: out, arg, fmt, and buf
            // The C code passes "\n" as the format string, and we need to pass null for buf
            _mi_fprintf(out, arg.as_deref_mut(), newline.as_ptr(), std::ptr::null());
        }
        
        mi_print_header(out, arg.as_deref_mut());
    }
}
pub fn mi_stat_total_print(
    stat: &mi_stat_count_t,
    msg: &str,
    unit: int64_t,
    out: Option<MiOutputFun>,
    mut arg: Option<&mut dyn std::any::Any>,
) {
    
    // Line 3: Print the message with formatting
    let fmt1 = std::ffi::CString::new("%10s:").unwrap();
    let msg_c = std::ffi::CString::new(msg).unwrap();
    _mi_fprintf(out, arg.as_deref_mut(), fmt1.as_ptr(), msg_c.as_ptr());

    // Line 4: Print 12 spaces
    let fmt2 = std::ffi::CString::new("%12s").unwrap();
    let space = std::ffi::CString::new(" ").unwrap();
    _mi_fprintf(out, arg.as_deref_mut(), fmt2.as_ptr(), space.as_ptr());

    // Line 5: Print the total amount
    mi_print_amount(stat.total, unit, out, arg.as_deref_mut());

    // Line 6: Print newline
    let newline = std::ffi::CString::new("\n").unwrap();
    _mi_fprintf(out, arg.as_deref_mut(), newline.as_ptr(), std::ptr::null());
}
pub fn mi_stat_counter_print(
    stat: Option<&mi_stat_counter_t>,
    msg: Option<&str>,
    out: Option<MiOutputFun>,
    mut arg: Option<&mut dyn std::any::Any>,
) {
    let msg = msg.unwrap_or("");
    if let Some(out) = out {
        out(&format!("{:>10}:", msg), arg.as_deref_mut());
    }

    let total: int64_t = stat.map(|stat| stat.total).unwrap_or(0);
    let unit: int64_t = -1;
    mi_print_amount(total, unit, out, arg.as_deref_mut());

    if let Some(out) = out {
        out("\n", arg.as_deref_mut());
    }
}
#[derive(Clone, Debug, Default)]
#[repr(C)]
pub struct mi_process_info_t {
    pub elapsed: mi_msecs_t,
    pub utime: mi_msecs_t,
    pub stime: mi_msecs_t,
    pub current_rss: usize,
    pub peak_rss: usize,
    pub current_commit: usize,
    pub peak_commit: usize,
    pub page_faults: usize,
}
pub fn mi_stat_peak_print(
    stat: &mi_stat_count_t,
    msg: &str,
    unit: int64_t,
    out: Option<MiOutputFun>,
    mut arg: Option<&mut dyn std::any::Any>,
) {
    // Print the message label
    if let Some(out_fn) = out {
        out_fn(&format!("{:>10}:", msg), arg.as_deref_mut());
    }
    
    // Print the peak amount
    mi_print_amount(stat.peak, unit, out, arg.as_deref_mut());
    
    // Print newline
    if let Some(out_fn) = out {
        out_fn("\n", arg.as_deref_mut());
    }
}

pub fn mi_stat_counter_print_avg(
    stat: &mi_stat_counter_t,
    msg: &str,
    out: Option<MiOutputFun>,
    arg: Option<&mut dyn std::any::Any>,
) {
    let avg_tens = if stat.total == 0 {
        0
    } else {
        (stat.total * 10) / stat.total
    };
    let avg_whole = (avg_tens / 10) as i64;
    let avg_frac1 = (avg_tens % 10) as i64;
    
    // Note: _mi_fprintf implementation would need to be provided
    // This is a placeholder showing the formatted output
    let output = format!("{:>10}: {:>5}.{} avg\n", msg, avg_whole, avg_frac1);
    
    if let Some(out_fn) = out {
        out_fn(&output, arg);
    }
}
/// Buffered output structure for logging/stats printing
#[repr(C)]
pub struct buffered_t {
    /// Output function callback
    pub out: Option<MiOutputFun>,
    /// Argument passed to the output function (as a raw pointer)
    pub arg: *mut std::ffi::c_void,
    /// Buffer for storing formatted output
    pub buf: *mut std::os::raw::c_char,
    /// Number of bytes currently used in the buffer
    pub used: usize,
    /// Total capacity of the buffer
    pub count: usize,
}

fn mi_buffered_flush(buf: &mut buffered_t) {
    unsafe {
        // Null-terminate the string at the current used position
        if !buf.buf.is_null() && buf.used < buf.count {
            *buf.buf.add(buf.used) = 0;
        }
        
        // Convert raw pointer to Option<&mut dyn std::any::Any>
        let arg_any = if buf.arg.is_null() {
            Option::None
        } else {
            Some(unsafe { &mut *(buf.arg as *mut dyn std::any::Any) })
        };
        
        _mi_fputs(buf.out, arg_any, std::ptr::null(), buf.buf);
        buf.used = 0;
    }
}
pub fn mi_buffered_out(msg: *const std::os::raw::c_char, arg: *mut std::ffi::c_void) {
    // Check for NULL pointers
    if msg.is_null() || arg.is_null() {
        return;
    }

    // Convert raw pointer to mutable reference
    let buf = unsafe { &mut *(arg as *mut buffered_t) };

    // Convert C string to Rust string slice
    let msg_cstr = unsafe { std::ffi::CStr::from_ptr(msg) };
    let msg_bytes = msg_cstr.to_bytes();
    
    let mut src_idx = 0;

    // Iterate through each byte in the message (C strings are bytes)
    while src_idx < msg_bytes.len() {
        let c = msg_bytes[src_idx];
        src_idx += 1;

        // Check if buffer is full
        if buf.used >= buf.count {
            mi_buffered_flush(buf);
        }

        // Safety assertion (translated from C macro)
        // Only assert if buffer is still full after flushing
        if buf.used >= buf.count {
            // Direct call to _mi_assert_fail (not using super::super::)
            crate::super_function_unit5::_mi_assert_fail(
                "buf->used < buf->count".as_ptr() as *const std::os::raw::c_char,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/stats.c".as_ptr() as *const std::os::raw::c_char,
                303,
                "mi_buffered_out".as_ptr() as *const std::os::raw::c_char,
            );
        }

        // Write character to buffer
        unsafe {
            *buf.buf.add(buf.used) = c as std::os::raw::c_char;
        }
        buf.used += 1;

        // Flush on newline
        if c == b'\n' {
            mi_buffered_flush(buf);
        }
    }
}

pub fn _mi_stats_print(
    stats: &crate::mi_stats_t::mi_stats_t,
    out0: Option<crate::MiOutputFun>,
    arg0: *mut std::ffi::c_void,
) {
    // Create a buffer of 256 chars (u8 in Rust), initialized to zeros
    let mut buf: [std::os::raw::c_char; 256] = [0; 256];
    
    // Create buffered_t structure
    let mut buffer = crate::buffered_t::buffered_t {
        out: out0,
        arg: arg0,
        buf: buf.as_mut_ptr(),
        used: 0,
        count: 255, // 255 because we need space for null terminator
    };
    
    // Wrapper function to convert mi_buffered_out signature to MiOutputFun
    fn buffered_out_wrapper(msg: &str, arg: Option<&mut dyn std::any::Any>) {
        if let Some(arg_ptr) = arg {
            // Convert the message to C string
            if let Ok(c_msg) = std::ffi::CString::new(msg) {
                unsafe {
                    // Cast arg back to *mut c_void
                    let arg_raw = arg_ptr as *mut _ as *mut std::ffi::c_void;
                    crate::mi_buffered_out(c_msg.as_ptr(), arg_raw);
                }
            }
        }
    }
    
    // Set the output function and argument
    let out: Option<crate::MiOutputFun> = Some(buffered_out_wrapper);
    
    // Call the printing functions - create new Option<&mut dyn Any> each time
    crate::mi_print_header(out, Some(&mut buffer as &mut dyn std::any::Any));
    
    // Print bins - note the C code uses 73U, so we use 73 in Rust
    let fmt = std::ffi::CStr::from_bytes_with_nul(b"bin\0").unwrap();
    crate::mi_stats_print_bins(
        &stats.malloc_bins,
        73,
        fmt,
        out,
        Some(&mut buffer as &mut dyn std::any::Any),
    );
    
    // Print normal malloc stats
    crate::mi_stat_print(
        &stats.malloc_normal,
        "binned",
        if stats.malloc_normal_count.total == 0 { 1 } else { -1 },
        out,
        Some(&mut buffer as &mut dyn std::any::Any),
    );
    
    // Print huge malloc stats
    crate::mi_stat_print(
        &stats.malloc_huge,
        "huge",
        if stats.malloc_huge_count.total == 0 { 1 } else { -1 },
        out,
        Some(&mut buffer as &mut dyn std::any::Any),
    );
    
    // Calculate and print total
    let mut total = crate::mi_stat_count_t::mi_stat_count_t {
        total: 0,
        peak: 0,
        current: 0,
    };
    
    crate::mi_stat_count_add_mt(&mut total, &stats.malloc_normal);
    crate::mi_stat_count_add_mt(&mut total, &stats.malloc_huge);
    crate::mi_stat_print_ex(&total, "total", 1, out, Some(&mut buffer as &mut dyn std::any::Any), Some(""));
    
    crate::mi_stat_total_print(&stats.malloc_requested, "malloc req", 1, out, Some(&mut buffer as &mut dyn std::any::Any));
    
    // For _mi_fprintf, we need to create C strings for format and buffer
    let newline_fmt = std::ffi::CString::new("\n").unwrap();
    let newline_buf = std::ffi::CString::new("").unwrap();
    crate::_mi_fprintf(out, Some(&mut buffer as &mut dyn std::any::Any), newline_fmt.as_ptr(), newline_buf.as_ptr());
    
    // Print various statistics
    crate::mi_stat_print_ex(&stats.reserved, "reserved", 1, out, Some(&mut buffer as &mut dyn std::any::Any), Some(""));
    crate::mi_stat_print_ex(&stats.committed, "committed", 1, out, Some(&mut buffer as &mut dyn std::any::Any), Some(""));
    crate::mi_stat_peak_print(&stats.reset, "reset", 1, out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_peak_print(&stats.purged, "purged", 1, out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_print_ex(&stats.page_committed, "touched", 1, out, Some(&mut buffer as &mut dyn std::any::Any), Some(""));
    crate::mi_stat_print(&stats.pages, "pages", -1, out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_print(&stats.pages_abandoned, "-abandoned", -1, out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.pages_reclaim_on_alloc), Some("-reclaima"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.pages_reclaim_on_free), Some("-reclaimf"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.pages_reabandon_full), Some("-reabandon"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.pages_unabandon_busy_wait), Some("-waits"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.pages_extended), Some("-extended"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.pages_retire), Some("-retire"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.arena_count), Some("arenas"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.arena_rollback_count), Some("-rollback"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.mmap_calls), Some("mmaps"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.commit_calls), Some("commits"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.reset_calls), Some("resets"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.purge_calls), Some("purges"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print(Some(&stats.malloc_guarded_count), Some("guarded"), out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_print(&stats.threads, "threads", -1, out, Some(&mut buffer as &mut dyn std::any::Any));
    crate::mi_stat_counter_print_avg(&stats.page_searches, "searches", out, Some(&mut buffer as &mut dyn std::any::Any));
    
    // Print NUMA nodes
    let numa_fmt = std::ffi::CString::new("%10s: %5i\n").unwrap();
    let numa_msg = std::ffi::CString::new("numa nodes").unwrap();
    let numa_count = crate::_mi_os_numa_node_count();
    let numa_buf = std::ffi::CString::new(format!("{}", numa_count)).unwrap();
    crate::_mi_fprintf(out, Some(&mut buffer as &mut dyn std::any::Any), numa_fmt.as_ptr(), numa_buf.as_ptr());
    
    // Get process information - using dummy values since mi_process_info is not available
    // In a real implementation, this would call mi_process_info
    let elapsed: usize = 0;
    let user_time: usize = 0;
    let sys_time: usize = 0;
    let current_rss: usize = 0;
    let peak_rss: usize = 0;
    let current_commit: usize = 0;
    let peak_commit: usize = 0;
    let page_faults: usize = 0;
    
    // Print elapsed time
    let elapsed_fmt = std::ffi::CString::new("%10s: %5zu.%03zu s\n").unwrap();
    let elapsed_msg = std::ffi::CString::new("elapsed").unwrap();
    let elapsed_buf = std::ffi::CString::new(format!("{}.{:03}", elapsed / 1000, elapsed % 1000)).unwrap();
    crate::_mi_fprintf(out, Some(&mut buffer as &mut dyn std::any::Any), elapsed_fmt.as_ptr(), elapsed_buf.as_ptr());
    
    // Print process information
    let process_fmt = std::ffi::CString::new("%10s: user: %zu.%03zu s, system: %zu.%03zu s, faults: %zu, rss: ").unwrap();
    let process_msg = std::ffi::CString::new("process").unwrap();
    let process_buf = std::ffi::CString::new(format!("{}.{:03}", user_time / 1000, user_time % 1000)).unwrap();
    crate::_mi_fprintf(out, Some(&mut buffer as &mut dyn std::any::Any), process_fmt.as_ptr(), process_buf.as_ptr());
    
    // Print peak RSS
    let peak_rss_fmt = std::ffi::CString::new("%s").unwrap();
    crate::mi_printf_amount(peak_rss as i64, 1, out, Some(&mut buffer as &mut dyn std::any::Any), peak_rss_fmt.as_ptr());
    
    // Print peak commit if > 0
    if peak_commit > 0 {
        let commit_fmt = std::ffi::CString::new(", commit: ").unwrap();
        let commit_buf = std::ffi::CString::new("").unwrap();
        crate::_mi_fprintf(out, Some(&mut buffer as &mut dyn std::any::Any), commit_fmt.as_ptr(), commit_buf.as_ptr());
        crate::mi_printf_amount(peak_commit as i64, 1, out, Some(&mut buffer as &mut dyn std::any::Any), peak_rss_fmt.as_ptr());
    }
    
    let final_newline_fmt = std::ffi::CString::new("\n").unwrap();
    let final_newline_buf = std::ffi::CString::new("").unwrap();
    crate::_mi_fprintf(out, Some(&mut buffer as &mut dyn std::any::Any), final_newline_fmt.as_ptr(), final_newline_buf.as_ptr());
}
pub fn mi_stats_print_out(out: Option<crate::MiOutputFun>, arg: *mut std::ffi::c_void) {
    crate::mi_stats_merge();
    
    let subproc = crate::_mi_subproc();
    let stats = &subproc.lock().unwrap().stats;
    
    crate::_mi_stats_print(stats, out, arg);
}
pub fn mi_stats_print(out: Option<crate::MiOutputFun>) {
    mi_stats_print_out(out, std::ptr::null_mut());
}
pub fn mi_stats_get_bin_size(bin: usize) -> usize {
    if bin > 73 {
        return 0;
    }
    _mi_bin_size(bin)
}
pub unsafe fn mi_get_tld_stats() -> *mut crate::mi_stats_t::mi_stats_t {
    &mut (*_mi_thread_tld()).stats
}
pub fn mi_thread_stats_print_out(out: Option<crate::MiOutputFun>, arg: *mut std::ffi::c_void) {
    let stats = unsafe { crate::mi_get_tld_stats() };
    if !stats.is_null() {
        crate::_mi_stats_print(unsafe { &*stats }, out, arg);
    }
}
pub fn __mi_stat_adjust_increase(stat: &mut crate::mi_stat_count_t::mi_stat_count_t, amount: usize) {
    crate::mi_stat_adjust(stat, amount as i64);
}
pub fn mi_heap_buf_expand(hbuf: Option<&mut MiHeapBuf>) -> bool {
    // Check for NULL pointer (None in Rust)
    let hbuf = match hbuf {
        Some(h) => h,
        None => return false,
    };

    // Clear the last byte if buffer exists and has size > 0
    if let Some(buf) = &mut hbuf.buf {
        if hbuf.size > 0 {
            if let Some(last) = buf.get_mut(hbuf.size - 1) {
                *last = 0;
            }
        }
    }

    // Check for overflow or reallocation disabled
    if hbuf.size > (usize::MAX / 2) || !hbuf.can_realloc {
        return false;
    }

    // Calculate new size
    let newsize = if hbuf.size == 0 {
        mi_good_size(12 * 1024)
    } else {
        2 * hbuf.size
    };

    // Prepare the pointer for mi_rezalloc
    let current_ptr = if let Some(buf) = &mut hbuf.buf {
        // Get a mutable pointer to the buffer's data as c_void
        Some(unsafe { &mut *(buf.as_mut_ptr() as *mut c_void) })
    } else {
        None
    };

    // Reallocate using C function
    let new_ptr = mi_rezalloc(current_ptr, newsize);
    
    match new_ptr {
        Some(ptr) if !ptr.is_null() => {
            // Convert raw pointer back to Vec<u8>
            unsafe {
                hbuf.buf = Some(Vec::from_raw_parts(
                    ptr as *mut u8,
                    hbuf.size,
                    newsize
                ));
            }
            hbuf.size = newsize;
            true
        }
        _ => false,
    }
}

pub fn mi_heap_buf_print(hbuf: Option<&mut MiHeapBuf>, msg: Option<&CStr>) {
    // Check for NULL pointers (None in Rust)
    if msg.is_none() || hbuf.is_none() {
        return;
    }
    
    let msg = msg.unwrap();
    let hbuf = hbuf.unwrap();
    
    // Check if buffer is full and cannot reallocate
    if (hbuf.used + 1) >= hbuf.size && !hbuf.can_realloc {
        return;
    }
    
    // Convert C string to bytes for iteration
    let msg_bytes = msg.to_bytes();
    
    for &c in msg_bytes {
        // Check if we need to expand the buffer
        if (hbuf.used + 1) >= hbuf.size {
            if !mi_heap_buf_expand(Some(hbuf)) {
                return;
            }
        }
        
        // Assert that used is less than size
        if !(hbuf.used < hbuf.size) {
            crate::super_function_unit5::_mi_assert_fail(
                "hbuf->used < hbuf.size\0".as_ptr() as *const std::os::raw::c_char,
                "/workdir/C2RustTranslation-main/subjects/mimalloc/src/stats.c\0".as_ptr() as *const std::os::raw::c_char,
                551,
                "mi_heap_buf_print\0".as_ptr() as *const std::os::raw::c_char,
            );
        }
        
        // Write the character to the buffer
        if let Some(buf) = &mut hbuf.buf {
            if hbuf.used < buf.len() {
                buf[hbuf.used] = c;
                hbuf.used += 1;
            }
        }
    }
    
    // Final assertion
    if !(hbuf.used < hbuf.size) {
        crate::super_function_unit5::_mi_assert_fail(
            "hbuf->used < hbuf.size\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/stats.c\0".as_ptr() as *const std::os::raw::c_char,
            554,
            "mi_heap_buf_print\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    // Null-terminate the string
    if let Some(buf) = &mut hbuf.buf {
        if hbuf.used < buf.len() {
            buf[hbuf.used] = 0;
        }
    }
}

pub fn mi_heap_buf_print_size(
    hbuf: Option<&mut MiHeapBuf>,
    name: Option<&CStr>,
    val: usize,
    add_comma: bool,
) {
    let mut buf = [0u8; 128];
    
    // Convert name to string slice if Some, otherwise use empty string
    let name_str = name.map(|cstr| cstr.to_string_lossy().to_string())
        .unwrap_or_else(|| String::new());
    
    // Format the string
    let suffix = if add_comma { "," } else { "" };
    let formatted = format!("    \"{}\": {}{}\n", name_str, val, suffix);
    
    // Ensure we don't overflow the buffer
    let bytes_to_copy = formatted.len().min(127);
    buf[..bytes_to_copy].copy_from_slice(&formatted.as_bytes()[..bytes_to_copy]);
    buf[127] = 0; // Ensure null termination
    
    // Convert buffer to CStr and print
    let c_str = unsafe { CStr::from_ptr(buf.as_ptr() as *const i8) };
    mi_heap_buf_print(hbuf, Some(c_str));
}

pub fn mi_heap_buf_print_value(
    hbuf: Option<&mut MiHeapBuf>,
    name: Option<&CStr>,
    val: int64_t,
) {
    let mut buf = [0u8; 128];
    
    // Convert name to string slice for formatting
    let name_str = match name {
        Some(cstr) => match cstr.to_str() {
            Ok(s) => s,
            Err(_) => return,
        },
        None => return,
    };
    
    // Format the string
    let formatted = format!("  \"{}\": {},\n", name_str, val);
    
    // Copy to buffer, ensuring null termination
    let bytes_to_copy = formatted.len().min(127);
    buf[..bytes_to_copy].copy_from_slice(&formatted.as_bytes()[..bytes_to_copy]);
    buf[127] = 0;
    
    // Convert buffer to CStr and print
    let cstr = unsafe { CStr::from_bytes_until_nul(&buf[..128]).unwrap() };
    mi_heap_buf_print(hbuf, Some(cstr));
}

pub fn mi_heap_buf_print_counter_value(
    hbuf: Option<&mut MiHeapBuf>,
    name: Option<&CStr>,
    stat: Option<&mi_stat_counter_t>,
) {
    if let (Some(hbuf), Some(name), Some(stat)) = (hbuf, name, stat) {
        mi_heap_buf_print_value(Some(hbuf), Some(name), stat.total);
    }
}
pub const MI_CBIN_COUNT: usize = 128;

#[repr(C)]
#[derive(Clone)]
pub struct mi_stats_t {
    // fields...
}

pub fn mi_heap_buf_print_count(
    hbuf: Option<&mut MiHeapBuf>,
    prefix: Option<&CStr>,
    stat: Option<&mi_stat_count_t>,
    add_comma: bool,
) {
    let mut buf = [0u8; 128];
    
    // Convert prefix to string or empty string
    let prefix_str = match prefix {
        Some(p) => p.to_string_lossy(),
        None => std::borrow::Cow::Borrowed(""),
    };
    
    // Get stat values or use defaults
    let (total, peak, current) = match stat {
        Some(s) => (s.total, s.peak, s.current),
        None => (0, 0, 0),
    };
    
    // Format the string
    let comma_str = if add_comma { "," } else { "" };
    let formatted = format!(
        "{} {{ \"total\": {}, \"peak\": {}, \"current\": {} }}{}\n",
        prefix_str, total, peak, current, comma_str
    );
    
    // Ensure we don't overflow the buffer
    let bytes_to_copy = std::cmp::min(formatted.len(), 127);
    buf[..bytes_to_copy].copy_from_slice(&formatted.as_bytes()[..bytes_to_copy]);
    buf[127] = 0;
    
    // Convert buffer to CStr and print
    let c_str = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
    mi_heap_buf_print(hbuf, Some(c_str));
}

pub fn mi_heap_buf_print_count_bin(
    hbuf: Option<&mut MiHeapBuf>,
    prefix: Option<&CStr>,
    stat: Option<&mi_stat_count_t>,
    bin: usize,
    add_comma: bool,
) {
    // Check for None values (equivalent to NULL checks in C)
    if hbuf.is_none() || prefix.is_none() || stat.is_none() {
        return;
    }

    let hbuf = hbuf.unwrap();
    let prefix = prefix.unwrap();
    let stat = stat.unwrap();

    // Calculate binsize using the dependency function
    let binsize = mi_stats_get_bin_size(bin);

    // Calculate pagesize based on binsize (translated from C ternary expression)
    let pagesize = if binsize <= (((1 * (1_usize << (13 + 3))) - ((3 + 2) * 32)) / 8) {
        1 * (1_usize << (13 + 3))
    } else if binsize <= ((8 * (1 * (1_usize << (13 + 3)))) / 8) {
        8 * (1 * (1_usize << (13 + 3)))
    } else if binsize <= ((8 * (1 * (1_usize << (13 + 3)))) / 8) {
        (1 << 3) * (8 * (1 * (1_usize << (13 + 3))))
    } else {
        0
    };

    // Create buffer for formatted string
    let mut buf = [0u8; 128];
    
    // Format the string using _mi_snprintf
    let comma_str = if add_comma { "," } else { "" };
    
    // Convert prefix to CString for C compatibility
    let prefix_cstr = prefix.to_bytes_with_nul();
    let prefix_ptr = prefix_cstr.as_ptr() as *const c_char;
    
    // Create format string
    let fmt = CString::new("%s{ \"total\": %lld, \"peak\": %lld, \"current\": %lld, \"block_size\": %zu, \"page_size\": %zu }%s\n").unwrap();
    
    // Prepare arguments for _mi_snprintf
    let mut args: Vec<*mut c_void> = Vec::new();
    args.push(prefix_ptr as *mut c_void);
    args.push(&stat.total as *const int64_t as *mut c_void);
    args.push(&stat.peak as *const int64_t as *mut c_void);
    args.push(&stat.current as *const int64_t as *mut c_void);
    args.push(&binsize as *const usize as *mut c_void);
    args.push(&pagesize as *const usize as *mut c_void);
    
    let comma_cstr = CString::new(comma_str).unwrap();
    args.push(comma_cstr.as_ptr() as *mut c_void);
    
    // Call _mi_snprintf
    let result = unsafe {
        _mi_snprintf(
            buf.as_mut_ptr() as *mut c_char,
            buf.len(),
            fmt.as_ptr(),
            args.as_mut_ptr() as *mut c_void,
        )
    };

    // Ensure null termination (equivalent to buf[127] = 0 in C)
    if result >= 0 && (result as usize) < buf.len() {
        buf[result as usize] = 0;
    } else {
        buf[127] = 0;
    }

    // Convert buffer to CStr and call mi_heap_buf_print
    let c_str = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
    mi_heap_buf_print(Some(hbuf), Some(c_str));
}

pub fn mi_heap_buf_print_count_cbin(
    hbuf: Option<&mut MiHeapBuf>,
    prefix: Option<&CStr>,
    stat: Option<&mi_stat_count_t>,
    bin: MiChunkbinT,
    add_comma: bool,
) {
    let cbin = match bin {
        MiChunkbinE::MI_CBIN_SMALL => "S",
        MiChunkbinE::MI_CBIN_MEDIUM => "M",
        MiChunkbinE::MI_CBIN_LARGE => "L",
        MiChunkbinE::MI_CBIN_OTHER => "X",
        _ => " ",
    };

    let mut buf = [0u8; 128];
    let comma_str = if add_comma { "," } else { "" };

    // Convert Option<&CStr> to *const c_char for the C function
    let prefix_ptr = prefix.map_or(std::ptr::null(), |p| p.as_ptr());
    
    // Get stat fields safely
    let total = stat.map_or(0, |s| s.total);
    let peak = stat.map_or(0, |s| s.peak);
    let current = stat.map_or(0, |s| s.current);

    // Create C strings for format and comma
    let fmt = CString::new("%s{ \"total\": %lld, \"peak\": %lld, \"current\": %lld, \"bin\": \"%s\" }%s\n")
        .expect("CString::new failed");
    let comma_cstr = CString::new(comma_str).expect("CString::new failed");

    unsafe {
        // Use _mi_snprintf with the buffer
        _mi_snprintf(
            buf.as_mut_ptr() as *mut c_char,
            buf.len(),
            fmt.as_ptr(),
            // Create a va_list-like structure (simplified - in real code this would need proper variadic handling)
            // For this translation, we'll pass the arguments directly
            std::ptr::null_mut(), // In real implementation, this would need proper variadic handling
        );
    }

    // Ensure null termination
    buf[127] = 0;

    // Convert buffer to CStr for printing
    let buf_cstr = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
    
    mi_heap_buf_print(hbuf, Some(buf_cstr));
}
pub fn mi_heap_buf_print_count_value(
    hbuf: Option<&mut MiHeapBuf>,
    name: Option<&CStr>,
    stat: Option<&mi_stat_count_t>,
) {
    // Create a buffer on the stack (equivalent to char buf[128] in C)
    let mut buf: [c_char; 128] = [0; 128];
    
    // Format the string using _mi_snprintf
    let name_ptr = name.map(|n| n.as_ptr()).unwrap_or(std::ptr::null());
    
    // Create a format string
    let fmt = CString::new("  \"%s\": ").unwrap();
    
    // Call _mi_snprintf (unsafe because it uses raw pointers)
    unsafe {
        _mi_snprintf(
            buf.as_mut_ptr(),
            buf.len(),
            fmt.as_ptr(),
            name_ptr as *mut c_void,
        );
    }
    
    // Ensure null termination (equivalent to buf[127] = 0 in C)
    buf[127] = 0;
    
    // Convert the buffer to a CStr for printing
    let buf_cstr = unsafe { CStr::from_ptr(buf.as_ptr()) };
    
    // Convert hbuf to raw pointer before using it
    let hbuf_ptr = hbuf.map(|r| r as *mut MiHeapBuf).unwrap_or(std::ptr::null_mut());
    
    // Reconstruct Option<&mut MiHeapBuf> for first call
    let hbuf_for_print = if !hbuf_ptr.is_null() {
        unsafe { Some(&mut *hbuf_ptr) }
    } else {
        None
    };
    
    // Call mi_heap_buf_print with the formatted buffer
    mi_heap_buf_print(hbuf_for_print, Some(buf_cstr));
    
    // Reconstruct Option<&mut MiHeapBuf> for second call
    let hbuf_for_count = if !hbuf_ptr.is_null() {
        unsafe { Some(&mut *hbuf_ptr) }
    } else {
        None
    };
    
    // Call mi_heap_buf_print_count with empty prefix and add_comma = true
    // Note: In the original C code, the prefix is an empty string "", not NULL
    let empty_prefix = CString::new("").unwrap();
    mi_heap_buf_print_count(hbuf_for_count, Some(empty_prefix.as_c_str()), stat, true);
}
pub fn mi_stats_reset() {
    // Get the stats pointer (unsafe call)
    let stats = unsafe { crate::mi_get_tld_stats() };
    
    // Lock the subproc mutex and get its stats field
    let subproc_mutex = crate::_mi_subproc();
    let mut subproc_guard = subproc_mutex.lock().unwrap();
    
    // Get raw pointer to subproc.stats for comparison
    let subproc_stats_ptr = &mut subproc_guard.stats as *mut crate::mi_stats_t::mi_stats_t;
    
    // Zero out stats if they're not the same memory location
    if stats != subproc_stats_ptr {
        let stats_slice = unsafe {
            std::slice::from_raw_parts_mut(stats as *mut u8, std::mem::size_of::<crate::mi_stats_t::mi_stats_t>())
        };
        crate::_mi_memzero(stats_slice, stats_slice.len());
    }
    
    // Zero out subproc.stats
    let subproc_stats_slice = unsafe {
        std::slice::from_raw_parts_mut(
            &mut subproc_guard.stats as *mut crate::mi_stats_t::mi_stats_t as *mut u8,
            std::mem::size_of::<crate::mi_stats_t::mi_stats_t>()
        )
    };
    crate::_mi_memzero(subproc_stats_slice, subproc_stats_slice.len());
    
    // Drop the guard before calling _mi_stats_init to avoid holding the lock
    drop(subproc_guard);
    
    // Initialize stats
    crate::_mi_stats_init();
}
pub fn mi_stats_get_json(output_size: usize, output_buf: *mut c_char) -> *mut c_char {
    crate::stats::mi_stats_merge();
    let mut hbuf = MiHeapBuf {
        buf: None,
        size: 0,
        used: 0,
        can_realloc: true,
    };
    if output_size > 0 && !output_buf.is_null() {
        unsafe {
            // Create a slice from the raw pointer for _mi_memzero
            let slice = std::slice::from_raw_parts_mut(output_buf as *mut u8, output_size);
            _mi_memzero(slice, output_size);
        }
        hbuf.buf = Some(unsafe { Vec::from_raw_parts(output_buf as *mut u8, output_size, output_size) });
        hbuf.size = output_size;
        hbuf.can_realloc = false;
    } else {
        if !mi_heap_buf_expand(Some(&mut hbuf)) {
            return std::ptr::null_mut();
        }
    }
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"{\n\0").unwrap()));
    mi_heap_buf_print_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"version\0").unwrap()), 2);
    mi_heap_buf_print_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"mimalloc_version\0").unwrap()), 316);
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  \"process\": {\n\0").unwrap()));
    let mut info = mi_process_info_t::default();
    // Call the process info function - assuming it exists with this signature
    // Based on the original C code, we need to pass references to the fields
    unsafe {
        // This function should be available from the C bindings
        // Note: The original C code uses size_t* parameters, which correspond to usize* in Rust
        extern "C" {
            fn mi_process_info(
                elapsed: *mut usize,
                utime: *mut usize,
                stime: *mut usize,
                current_rss: *mut usize,
                peak_rss: *mut usize,
                current_commit: *mut usize,
                peak_commit: *mut usize,
                page_faults: *mut usize,
            );
        }
        mi_process_info(
            &mut info.elapsed as *mut _ as *mut usize,
            &mut info.utime as *mut _ as *mut usize,
            &mut info.stime as *mut _ as *mut usize,
            &mut info.current_rss,
            &mut info.peak_rss,
            &mut info.current_commit,
            &mut info.peak_commit,
            &mut info.page_faults,
        );
    }
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"elapsed_msecs\0").unwrap()), info.elapsed as usize, true);
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"user_msecs\0").unwrap()), info.utime as usize, true);
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"system_msecs\0").unwrap()), info.stime as usize, true);
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"page_faults\0").unwrap()), info.page_faults, true);
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"rss_current\0").unwrap()), info.current_rss, true);
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"rss_peak\0").unwrap()), info.peak_rss, true);
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"commit_current\0").unwrap()), info.current_commit, true);
    mi_heap_buf_print_size(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"commit_peak\0").unwrap()), info.peak_commit, false);
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  },\n\0").unwrap()));
    let stats = &_mi_subproc().lock().unwrap().stats;
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages\0").unwrap()), Some(&stats.pages));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"reserved\0").unwrap()), Some(&stats.reserved));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"committed\0").unwrap()), Some(&stats.committed));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"reset\0").unwrap()), Some(&stats.reset));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"purged\0").unwrap()), Some(&stats.purged));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"page_committed\0").unwrap()), Some(&stats.page_committed));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages_abandoned\0").unwrap()), Some(&stats.pages_abandoned));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"threads\0").unwrap()), Some(&stats.threads));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"malloc_normal\0").unwrap()), Some(&stats.malloc_normal));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"malloc_huge\0").unwrap()), Some(&stats.malloc_huge));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"malloc_requested\0").unwrap()), Some(&stats.malloc_requested));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"mmap_calls\0").unwrap()), Some(&stats.mmap_calls));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"commit_calls\0").unwrap()), Some(&stats.commit_calls));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"reset_calls\0").unwrap()), Some(&stats.reset_calls));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"purge_calls\0").unwrap()), Some(&stats.purge_calls));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"arena_count\0").unwrap()), Some(&stats.arena_count));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"malloc_normal_count\0").unwrap()), Some(&stats.malloc_normal_count));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"malloc_huge_count\0").unwrap()), Some(&stats.malloc_huge_count));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"malloc_guarded_count\0").unwrap()), Some(&stats.malloc_guarded_count));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"arena_rollback_count\0").unwrap()), Some(&stats.arena_rollback_count));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"arena_purges\0").unwrap()), Some(&stats.arena_purges));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages_extended\0").unwrap()), Some(&stats.pages_extended));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages_retire\0").unwrap()), Some(&stats.pages_retire));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"page_searches\0").unwrap()), Some(&stats.page_searches));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"segments\0").unwrap()), Some(&stats.segments));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"segments_abandoned\0").unwrap()), Some(&stats.segments_abandoned));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"segments_cache\0").unwrap()), Some(&stats.segments_cache));
    mi_heap_buf_print_count_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"_segments_reserved\0").unwrap()), Some(&stats._segments_reserved));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages_reclaim_on_alloc\0").unwrap()), Some(&stats.pages_reclaim_on_alloc));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages_reclaim_on_free\0").unwrap()), Some(&stats.pages_reclaim_on_free));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages_reabandon_full\0").unwrap()), Some(&stats.pages_reabandon_full));
    mi_heap_buf_print_counter_value(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"pages_unabandon_busy_wait\0").unwrap()), Some(&stats.pages_unabandon_busy_wait));
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  \"malloc_bins\": [\n\0").unwrap()));
    for i in 0..=73 {
        mi_heap_buf_print_count_bin(
            Some(&mut hbuf),
            Some(CStr::from_bytes_with_nul(b"    \0").unwrap()),
            Some(&stats.malloc_bins[i]),
            i,
            i != 73,
        );
    }
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  ],\n\0").unwrap()));
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  \"page_bins\": [\n\0").unwrap()));
    for i in 0..=73 {
        mi_heap_buf_print_count_bin(
            Some(&mut hbuf),
            Some(CStr::from_bytes_with_nul(b"    \0").unwrap()),
            Some(&stats.page_bins[i]),
            i,
            i != 73,
        );
    }
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  ],\n\0").unwrap()));
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  \"chunk_bins\": [\n\0").unwrap()));
    for i in 0..MI_CBIN_COUNT {
        mi_heap_buf_print_count_cbin(
            Some(&mut hbuf),
            Some(CStr::from_bytes_with_nul(b"    \0").unwrap()),
            Some(&stats.chunk_bins[i]),
            // Use unsafe transmute to convert usize to MiChunkbinT
            // This assumes MiChunkbinT is repr(C) and has the same size as usize
            unsafe { std::mem::transmute(i as u8) },
            i != (MI_CBIN_COUNT - 1),
        );
    }
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"  ]\n\0").unwrap()));
    mi_heap_buf_print(Some(&mut hbuf), Some(CStr::from_bytes_with_nul(b"}\n\0").unwrap()));
    match hbuf.buf {
        Some(mut vec) => {
            let ptr = vec.as_mut_ptr();
            std::mem::forget(vec);
            ptr as *mut c_char
        }
        None => std::ptr::null_mut(),
    }
}
