use crate::*;

#[derive(Clone)]
pub struct Stat {
    pub st_dev: u64,
    pub st_ino: u64,
    pub st_nlink: u64,
    pub st_mode: u32,
    pub st_uid: u32,
    pub st_gid: u32,
    pub __pad0: i32,
    pub st_rdev: u64,
    pub st_size: i64,
    pub st_blksize: i64,
    pub st_blocks: i64,
    pub st_atime: i64,
    pub st_atimensec: usize,
    pub st_mtime: i64,
    pub st_mtimensec: usize,
    pub st_ctime: i64,
    pub st_ctimensec: usize,
    pub __glibc_reserved: [i64; 3],
}

