use crate::*;

#[repr(C)]
#[derive(Clone)]
pub struct RUsage {
    pub ru_utime: Timeval,
    pub ru_stime: Timeval,
    // union { long int ru_maxrss; __syscall_slong_t __ru_maxrss_word; };
    pub ru_maxrss: i64,      // 1 element: maximum resident set size
    // union { long int ru_ixrss; __syscall_slong_t __ru_ixrss_word; };
    pub ru_ixrss: i64,        // 1 element: integral shared memory size
    // union { long int ru_idrss; __syscall_slong_t __ru_idrss_word; };
    pub ru_idrss: i64,        // 1 element: integral unshared data size
    // union { long int ru_isrss; __syscall_slong_t __ru_isrss_word; };
    pub ru_isrss: i64,        // 1 element: integral unshared stack size
    // union { long int ru_minflt; __syscall_slong_t __ru_minflt_word; };
    pub ru_minflt: i64,      // 1 element: page reclaims (soft page faults)
    // union { long int ru_majflt; __syscall_slong_t __ru_majflt_word; };
    pub ru_majflt: i64,      // 1 element: page faults (hard page faults)
    // union { long int ru_nswap; __syscall_slong_t __ru_nswap_word; };
    pub ru_nswap: i64,        // 1 element: swaps
    // union { long int ru_inblock; __syscall_slong_t __ru_inblock_word; };
    pub ru_inblock: i64,    // 1 element: block input operations
    // union { long int ru_oublock; __syscall_slong_t __ru_oublock_word; };
    pub ru_oublock: i64,    // 1 element: block output operations
    // union { long int ru_msgsnd; __syscall_slong_t __ru_msgsnd_word; };
    pub ru_msgsnd: i64,      // 1 element: IPC messages sent
    // union { long int ru_msgrcv; __syscall_slong_t __ru_msgrcv_word; };
    pub ru_msgrcv: i64,      // 1 element: IPC messages received
    // union { long int ru_nsignals; __syscall_slong_t __ru_nsignals_word; };
    pub ru_nsignals: i64,  // 1 element: signals received
    // union { long int ru_nvcsw; __syscall_slong_t __ru_nvcsw_word; };
    pub ru_nvcsw: i64,        // 1 element: voluntary context switches
    // union { long int ru_nivcsw; __syscall_slong_t __ru_nivcsw_word; };
    pub ru_nivcsw: i64,      // 1 element: involuntary context switches
}

