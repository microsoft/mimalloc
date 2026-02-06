use crate::*;
use std::os::raw::c_ulong;
use std::sync::atomic::AtomicI16;
use std::sync::atomic::AtomicI64;
use std::sync::atomic::AtomicI8;
use std::sync::atomic::AtomicU64;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
use std::time::SystemTime;

pub type mi_arena_id_t = *mut std::ffi::c_void;

pub type mi_subproc_id_t = *mut std::ffi::c_void;


pub type mi_ssize_t = i64;

// For thread-safe global variables of this type, use:
// lazy_static::lazy_static! {
//     pub static ref VARIABLE_NAME: std::sync::Mutex<mi_ssize_t> = 
//         std::sync::Mutex::new(0);
// }

// For atomic operations on global variables of this type, use:
// pub static VARIABLE_NAME: AtomicI64 = AtomicI64::new(0);


pub type mi_atomic_once_t = AtomicUsize;


pub type mi_atomic_guard_t = AtomicUsize;


pub type mi_encoded_t = u64;

// For thread-safe global variables of this type, use:
// lazy_static::lazy_static! {
//     pub static ref MI_ENCODED_VAR: AtomicU64 = AtomicU64::new(0);
// }


pub type mi_threadid_t = usize;

pub static MI_THREAD_ID: AtomicUsize = AtomicUsize::new(0);


pub type mi_page_flags_t = usize;

pub static MI_PAGE_FLAGS: AtomicUsize = AtomicUsize::new(0);


pub type mi_thread_free_t = AtomicUsize;

pub type mi_heaptag_t = u8;


pub type mi_msecs_t = i64;

// For thread-safe global variables of this type, use:
// lazy_static::lazy_static! {
//     pub static ref VARIABLE_NAME: std::sync::Mutex<mi_msecs_t> = 
//         std::sync::Mutex::new(0);
// }

// For atomic operations on global variables of this type, use:
// pub static ATOMIC_VARIABLE: AtomicI64 = AtomicI64::new(0);


pub type mi_bfield_t = usize;

pub static MI_BFIELD_T: AtomicUsize = AtomicUsize::new(0);

pub type mi_xset_t = bool;

pub type __u_char = u8;

pub type __u_short = u16;

pub type __u_int = u32;

pub type __u_long = usize;

pub type __int8_t = i8;

pub type __uint8_t = u8;

pub type __int16_t = i16;

pub type __uint16_t = u16;

pub type __int32_t = i32;

pub type __uint32_t = u32;

pub type __int64_t = i64;

pub type __uint64_t = u64;

pub type __int_least8_t = i8;

pub type __uint_least8_t = u8;

pub type __int_least16_t = i16;

pub type __uint_least16_t = u16;

pub type __int_least32_t = i32;

pub type __uint_least32_t = u32;

pub type __int_least64_t = i64;

pub type __uint_least64_t = u64;

pub type __quad_t = i64;

pub type __u_quad_t = u64;

pub type __intmax_t = i64;

pub type __uintmax_t = u64;

pub type __dev_t = u64;

pub type __uid_t = u32;

pub type __gid_t = u32;

pub type __ino_t = u64;

pub type __ino64_t = u64;

pub type __mode_t = u32;

pub type __nlink_t = u64;

pub type __off_t = i64;

pub type __off64_t = i64;

pub type __pid_t = i32;

pub type __clock_t = i64;

pub type __rlim_t = usize;

pub type __rlim64_t = u64;

pub type __id_t = u32;

pub type __time_t = i64;

pub type __useconds_t = u32;

pub type __suseconds_t = i64;

pub type __suseconds64_t = i64;

pub type __daddr_t = i32;

pub type __key_t = i32;

pub type __clockid_t = i32;

pub type __timer_t = *mut std::ffi::c_void;

pub type __blksize_t = i64;

pub type __blkcnt_t = i64;

pub type __blkcnt64_t = i64;

pub type __fsblkcnt_t = u64;

pub type __fsblkcnt64_t = u64;

pub type __fsfilcnt_t = u64;

pub type __fsfilcnt64_t = u64;

pub type __fsword_t = i64;

pub type __ssize_t = i64;

pub type __syscall_slong_t = i64;

pub type __syscall_ulong_t = usize;

pub type __loff_t = i64;

pub type __caddr_t = *mut std::ffi::c_char;

pub type __intptr_t = isize;

pub type __socklen_t = u32;

pub type __sig_atomic_t = i32;

pub type off_t = i64;

pub type mode_t = u32;

pub type __s8 = i8;

pub type __u8 = u8;

pub type __s16 = i16;

pub type __u16 = u16;

pub type __s32 = i32;

pub type __u32 = u32;

pub type __s64 = i64;

pub type __u64 = u64;

pub type __kernel_key_t = i32;

pub type __kernel_mqd_t = i32;

pub type __kernel_old_uid_t = u16;

pub type __kernel_old_gid_t = u16;

pub type __kernel_old_dev_t = u64;

pub type __kernel_long_t = i64;

pub type __kernel_ulong_t = u64;

// Remove the duplicate definition of __kernel_ulong_t
// pub type __kernel_ulong_t = u64; // This line is already defined elsewhere
pub type __kernel_ino_t = __kernel_ulong_t;

pub type __kernel_mode_t = u32;

pub type __kernel_pid_t = i32;

pub type __kernel_ipc_pid_t = i32;

pub type __kernel_uid_t = u32;

pub type __kernel_gid_t = u32;

pub type __kernel_suseconds_t = i64;

pub type __kernel_daddr_t = i32;

pub type __kernel_uid32_t = u32;

pub type __kernel_gid32_t = u32;

pub type __kernel_size_t = usize;

pub type __kernel_ssize_t = isize;

pub type __kernel_ptrdiff_t = isize;

pub type __kernel_off_t = i64;

pub type __kernel_loff_t = i64;

pub type __kernel_old_time_t = __kernel_long_t;

pub type __kernel_time_t = i64;

pub type __kernel_time64_t = i64;

pub type __kernel_clock_t = i64;

pub type __kernel_timer_t = i32;

pub type __kernel_clockid_t = i32;

pub type __kernel_caddr_t = *mut std::ffi::c_char;

pub type __kernel_uid16_t = u16;

pub type __kernel_gid16_t = u16;

pub type __s128 = i128;

pub type __u128 = u128;

pub type __le16 = u16;

pub type __be16 = u16;

pub type __le32 = u32;

pub type __be32 = u32;

pub type __le64 = u64;

pub type __be64 = u64;

pub type __sum16 = u16;

pub type __wsum = u32;

pub type __poll_t = u32;

pub type rlim_t = u64;

pub type id_t = i32;

pub type __rlimit_resource_t = i32;

pub type __rusage_who_t = i32;

pub type __priority_which_t = i32;

pub type ino_t = u64;

pub type dev_t = u64;

pub type gid_t = u32;

pub type nlink_t = u64;

pub type uid_t = u32;

pub type pid_t = i32;

pub type ssize_t = isize;

pub type clockid_t = std::os::raw::c_int;


pub type time_t = SystemTime;

pub type timer_t = std::os::raw::c_long;


pub type int8_t = i8;

pub static mut INT8_T: AtomicI8 = AtomicI8::new(0);


pub type int16_t = i16;

pub static mut INT16_T: AtomicI16 = AtomicI16::new(0);

pub type int32_t = i32;

pub type int64_t = i64;

pub type u_int8_t = u8;

pub type u_int16_t = u16;

pub type u_int32_t = u32;

pub type u_int64_t = u64;

pub type register_t = i32;

pub type blkcnt_t = i64;


pub type fsblkcnt_t = c_ulong;


pub type fsfilcnt_t = c_ulong;


pub type random_t = AtomicUsize;

