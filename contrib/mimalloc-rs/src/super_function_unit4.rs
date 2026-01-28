use crate::*;
use crate::MI_MAX_WARNING_COUNT;
use crate::MI_OPTIONS;
use crate::MiOutputFun;
use crate::WARNING_COUNT;
use crate::mi_vfprintf_thread;
use std::ffi::CStr;
use std::ffi::CString;
use std::ffi::c_void;
use std::os::raw::c_char;
use std::sync::atomic::Ordering;


// Helper function to convert between the two MiOption types
pub fn convert_mi_option(opt: MiOption) -> MiOption {
    // Both enums are #[repr(i32)], so we can safely convert through the integer value
    match opt as i32 {
        0 => MiOption::ShowErrors,
        1 => MiOption::ShowStats,
        2 => MiOption::Verbose,
        3 => MiOption::EagerCommit,
        4 => MiOption::ArenaEagerCommit,
        5 => MiOption::PurgeDecommits,
        6 => MiOption::AllowLargeOsPages,
        7 => MiOption::ReserveHugeOsPages,
        8 => MiOption::ReserveHugeOsPagesAt,
        9 => MiOption::ReserveOsMemory,
        10 => MiOption::DeprecatedSegmentCache,
        11 => MiOption::DeprecatedPageReset,
        12 => MiOption::AbandonedPagePurge,
        13 => MiOption::DeprecatedSegmentReset,
        14 => MiOption::EagerCommitDelay,
        15 => MiOption::PurgeDelay,
        16 => MiOption::UseNumaNodes,
        17 => MiOption::DisallowOsAlloc,
        18 => MiOption::OsTag,
        19 => MiOption::MaxErrors,
        20 => MiOption::MaxWarnings,
        21 => MiOption::DeprecatedMaxSegmentReclaim,
        22 => MiOption::DestroyOnExit,
        23 => MiOption::ArenaReserve,
        24 => MiOption::ArenaPurgeMult,
        25 => MiOption::DeprecatedPurgeExtendDelay,
        26 => MiOption::DisallowArenaAlloc,
        27 => MiOption::RetryOnOom,
        28 => MiOption::VisitAbandoned,
        29 => MiOption::GuardedMin,
        30 => MiOption::GuardedMax,
        31 => MiOption::GuardedPrecise,
        32 => MiOption::GuardedSampleRate,
        33 => MiOption::GuardedSampleSeed,
        34 => MiOption::GenericCollect,
        35 => MiOption::PageReclaimOnFree,
        36 => MiOption::PageFullRetain,
        37 => MiOption::PageMaxCandidates,
        38 => MiOption::MaxVabits,
        39 => MiOption::PagemapCommit,
        40 => MiOption::PageCommitOnDemand,
        41 => MiOption::PageMaxReclaim,
        42 => MiOption::PageCrossThreadMaxReclaim,
        _ => MiOption::ShowErrors, // fallback
    }
}
pub fn _mi_warning_message(fmt: &CStr, args: *mut c_void) {
    if !mi_option_is_enabled(convert_mi_option(MiOption::Verbose)) {
        if !mi_option_is_enabled(convert_mi_option(MiOption::ShowErrors)) {
            return;
        }

        let mi_max_warning_count = MI_MAX_WARNING_COUNT.load(Ordering::Acquire);
        if mi_max_warning_count >= 0 {
            let prev = WARNING_COUNT.fetch_add(1, Ordering::AcqRel) as i64;
            if prev > mi_max_warning_count {
                return;
            }
        }
    }

    let pre = CStr::from_bytes_with_nul(b"mimalloc: warning: \0")
        .expect("NUL-terminated warning prefix");

    let output_func: Option<MiOutputFun> = None;
    if let Some(func) = output_func {
        mi_vfprintf_thread(func, Option::None, Some(pre), fmt, args);
    }
}

pub fn mi_option_is_enabled(option: MiOption) -> bool {
    mi_option_get(option) != 0
}


pub fn mi_option_get(option: MiOption) -> i64 {
    // Mirror the C defensive checks (Rust callers can still pass the sentinel variant).
    let option_usize = option as usize;
    let _mi_option_last = MiOption::MiOptionLast as usize;

    if !(option_usize < _mi_option_last) {
        let assertion = b"option >= 0 && option < _mi_option_last\0";
        let fname = b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/options.c\0";
        let func = b"mi_option_get\0";
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr() as *const c_char,
            fname.as_ptr() as *const c_char,
            258,
            func.as_ptr() as *const c_char,
        );
        return 0;
    }

    // Convert to MiOption for comparison with struct field
    let globals_option = match option as i32 {
        0 => MiOption::ShowErrors,
        1 => MiOption::ShowStats,
        2 => MiOption::Verbose,
        3 => MiOption::EagerCommit,
        4 => MiOption::ArenaEagerCommit,
        5 => MiOption::PurgeDecommits,
        6 => MiOption::AllowLargeOsPages,
        7 => MiOption::ReserveHugeOsPages,
        8 => MiOption::ReserveHugeOsPagesAt,
        9 => MiOption::ReserveOsMemory,
        10 => MiOption::DeprecatedSegmentCache,
        11 => MiOption::DeprecatedPageReset,
        12 => MiOption::AbandonedPagePurge,
        13 => MiOption::DeprecatedSegmentReset,
        14 => MiOption::EagerCommitDelay,
        15 => MiOption::PurgeDelay,
        16 => MiOption::UseNumaNodes,
        17 => MiOption::DisallowOsAlloc,
        18 => MiOption::OsTag,
        19 => MiOption::MaxErrors,
        20 => MiOption::MaxWarnings,
        21 => MiOption::DeprecatedMaxSegmentReclaim,
        22 => MiOption::DestroyOnExit,
        23 => MiOption::ArenaReserve,
        24 => MiOption::ArenaPurgeMult,
        25 => MiOption::DeprecatedPurgeExtendDelay,
        26 => MiOption::DisallowArenaAlloc,
        27 => MiOption::RetryOnOom,
        28 => MiOption::VisitAbandoned,
        29 => MiOption::GuardedMin,
        30 => MiOption::GuardedMax,
        31 => MiOption::GuardedPrecise,
        32 => MiOption::GuardedSampleRate,
        33 => MiOption::GuardedSampleSeed,
        34 => MiOption::GenericCollect,
        35 => MiOption::PageReclaimOnFree,
        36 => MiOption::PageFullRetain,
        37 => MiOption::PageMaxCandidates,
        38 => MiOption::MaxVabits,
        39 => MiOption::PagemapCommit,
        40 => MiOption::PageCommitOnDemand,
        41 => MiOption::PageMaxReclaim,
        42 => MiOption::PageCrossThreadMaxReclaim,
        _ => MiOption::ShowErrors,
    };

    let mut guard = MI_OPTIONS.lock().unwrap();
    let desc: &mut crate::mi_option_desc_t::mi_option_desc_t = &mut guard[option_usize];

    if desc.option != globals_option {
        let assertion = b"desc->option == option\0";
        let fname = b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/options.c\0";
        let func = b"mi_option_get\0";
        crate::super_function_unit5::_mi_assert_fail(
            assertion.as_ptr() as *const c_char,
            fname.as_ptr() as *const c_char,
            261,
            func.as_ptr() as *const c_char,
        );
    }

    if desc.init == crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT {
        mi_option_init(desc);
    }

    desc.value as i64
}


pub fn mi_option_init(desc: &mut crate::mi_option_desc_t::mi_option_desc_t) {
    let mut s: [u8; 64 + 1] = [0; 64 + 1];
    let mut buf: [u8; 64 + 1] = [0; 64 + 1];

    crate::libc_new::_mi_strlcpy(&mut buf, b"mimalloc_\0");
    let name_str = desc.name.unwrap_or("");
    crate::libc_new::_mi_strlcat(&mut buf, name_str.as_bytes());

    let name_end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    let name_str = std::str::from_utf8(&buf[..name_end]).unwrap_or("");
    let mut found = crate::libc_new::_mi_getenv(Some(name_str), &mut s);

    if !found {
        let legacy_name_opt = desc
            .legacy_name
            .and_then(|a| if a.is_empty() { Option::None } else { Some(a) });

        if let Some(legacy_name) = legacy_name_opt {
            crate::libc_new::_mi_strlcpy(&mut buf, b"mimalloc_\0");
            crate::libc_new::_mi_strlcat(&mut buf, legacy_name.as_bytes());

            let legacy_end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
            let legacy_env = std::str::from_utf8(&buf[..legacy_end]).unwrap_or("");
            found = crate::libc_new::_mi_getenv(Some(legacy_env), &mut s);

            if found {
                let msg = format!(
                    "environment option \"mimalloc_{}\" is deprecated -- use \"mimalloc_{}\" instead.\n",
                    legacy_name, desc.name.unwrap_or("")
                );
                if let Ok(cmsg) = CString::new(msg) {
                    let fmt = cmsg.as_c_str();
                    _mi_warning_message(fmt, std::ptr::null_mut());
                }
            }
        }
    }

    if found {
        let s_end = s.iter().position(|&b| b == 0).unwrap_or(s.len());
        let s_str = std::str::from_utf8(&s[..s_end]).unwrap_or("");
        let len = crate::libc_new::_mi_strnlen(Some(s_str), buf.len() - 1);

        for i in 0..len {
            let ch = s_str.as_bytes().get(i).copied().unwrap_or(0) as char;
            buf[i] = crate::libc_new::_mi_toupper(ch) as u8;
        }
        buf[len] = 0;

        let upper_end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
        let upper = std::str::from_utf8(&buf[..upper_end]).unwrap_or("");

        if upper.is_empty() || "1;TRUE;YES;ON".contains(upper) {
            desc.value = 1;
            desc.init = crate::mi_option_init_t::mi_option_init_t::MI_OPTION_INITIALIZED;
        } else if "0;FALSE;NO;OFF".contains(upper) {
            desc.value = 0;
            desc.init = crate::mi_option_init_t::mi_option_init_t::MI_OPTION_INITIALIZED;
        } else {
            // strtol-like parse with end pointer
            let bytes = upper.as_bytes();
            let mut end_idx: usize = 0;

            let mut sign: i64 = 1;
            if bytes.get(0) == Some(&b'-') {
                sign = -1;
                end_idx = 1;
            } else if bytes.get(0) == Some(&b'+') {
                end_idx = 1;
            }

            let digit_start = end_idx;
            while end_idx < bytes.len() && bytes[end_idx].is_ascii_digit() {
                end_idx += 1;
            }

            let mut value: i64 = 0;
            if end_idx > digit_start {
                if let Ok(v) = upper[digit_start..end_idx].parse::<i64>() {
                    value = v.saturating_mul(sign);
                }
            }

            if crate::options::mi_option_has_size_in_kib(convert_mi_option(desc.option)) {
                let mut size: usize = if value < 0 { 0 } else { value as usize };
                let mut overflow = false;

                if bytes.get(end_idx) == Some(&b'K') {
                    end_idx += 1;
                } else if bytes.get(end_idx) == Some(&b'M') {
                    overflow = crate::alloc::mi_mul_overflow(size, 1024, &mut size);
                    end_idx += 1;
                } else if bytes.get(end_idx) == Some(&b'G') {
                    overflow = crate::alloc::mi_mul_overflow(size, 1024 * 1024, &mut size);
                    end_idx += 1;
                } else if bytes.get(end_idx) == Some(&b'T') {
                    overflow = crate::alloc::mi_mul_overflow(size, 1024 * 1024 * 1024, &mut size);
                    end_idx += 1;
                } else {
                    size = ((size + 1024) - 1) / 1024;
                }

                if bytes.get(end_idx) == Some(&b'I') && bytes.get(end_idx + 1) == Some(&b'B') {
                    end_idx += 2;
                } else if bytes.get(end_idx) == Some(&b'B') {
                    end_idx += 1;
                }

                let ptrdiff_max: usize = isize::MAX as usize;
                if overflow || size > ptrdiff_max {
                    size = ptrdiff_max / 1024;
                }

                value = if size as u128 > i64::MAX as u128 {
                    i64::MAX
                } else {
                    size as i64
                };
            }

            if end_idx == bytes.len() {
                crate::options::mi_option_set(convert_mi_option(desc.option), value);
                desc.value = value as isize;
                desc.init = crate::mi_option_init_t::mi_option_init_t::MI_OPTION_INITIALIZED;
            } else {
                desc.init = crate::mi_option_init_t::mi_option_init_t::MI_OPTION_DEFAULTED;

                if desc.option == MiOption::Verbose && desc.value == 0 {
                    desc.value = 1;
                    let msg = format!(
                        "environment option mimalloc_{} has an invalid value.\n",
                        desc.name.unwrap_or("")
                    );
                    if let Ok(cmsg) = CString::new(msg) {
                        let fmt = cmsg.as_c_str();
                        _mi_warning_message(fmt, std::ptr::null_mut());
                    }
                    desc.value = 0;
                } else {
                    let msg = format!(
                        "environment option mimalloc_{} has an invalid value.\n",
                        desc.name.unwrap_or("")
                    );
                    if let Ok(cmsg) = CString::new(msg) {
                        let fmt = cmsg.as_c_str();
                        _mi_warning_message(fmt, std::ptr::null_mut());
                    }
                }
            }
        }

        if desc.init == crate::mi_option_init_t::mi_option_init_t::MI_OPTION_UNINIT {
            let assertion = b"desc->init != MI_OPTION_UNINIT\0";
            let fname = b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/options.c\0";
            let func = b"mi_option_init\0";
            crate::super_function_unit5::_mi_assert_fail(
                assertion.as_ptr() as *const c_char,
                fname.as_ptr() as *const c_char,
                679,
                func.as_ptr() as *const c_char,
            );
        }
    } else if !crate::init::_mi_preloading() {
        desc.init = crate::mi_option_init_t::mi_option_init_t::MI_OPTION_DEFAULTED;
    }
}


