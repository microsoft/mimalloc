use crate::*;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RlimitResource {
    RlimitCpu = 0,
    RlimitFsize = 1,
    RlimitData = 2,
    RlimitStack = 3,
    RlimitCore = 4,
    __RlimitRss = 5,
    RlimitNofile = 7,
    RlimitAs = 9,
    __RlimitNproc = 6,
    __RlimitMemlock = 8,
    __RlimitLocks = 10,
    __RlimitSigpending = 11,
    __RlimitMsgqueue = 12,
    __RlimitNice = 13,
    __RlimitRtprio = 14,
    __RlimitRttime = 15,
    __RlimitNlimits = 16,
}

pub struct RlimitConstants;

impl RlimitConstants {
    pub const __RLIMIT_OFILE: RlimitResource = RlimitResource::RlimitNofile;
    pub const __RLIM_NLIMITS: RlimitResource = RlimitResource::__RlimitNlimits;
}

