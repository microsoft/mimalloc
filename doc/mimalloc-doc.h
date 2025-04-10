/* ----------------------------------------------------------------------------
Copyright (c) 2018-2025, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#error "documentation file only!"


/*! \mainpage

This is the API documentation of the
[mimalloc](https://github.com/microsoft/mimalloc) allocator
(pronounced "me-malloc") -- a
general purpose allocator with excellent [performance](bench.html)
characteristics. Initially
developed by Daan Leijen for the run-time systems of the
[Koka](https://github.com/koka-lang/koka) and [Lean](https://github.com/leanprover/lean) languages.

It is a drop-in replacement for `malloc` and can be used in other programs
without code changes, for example, on Unix you can use it as:
```
> LD_PRELOAD=/usr/bin/libmimalloc.so  myprogram
```

Notable aspects of the design include:
- __small and consistent__: the library is about 8k LOC using simple and
  consistent data structures. This makes it very suitable
  to integrate and adapt in other projects. For runtime systems it
  provides hooks for a monotonic _heartbeat_ and deferred freeing (for
  bounded worst-case times with reference counting).
  Partly due to its simplicity, mimalloc has been ported to many systems (Windows, macOS,
  Linux, WASM, various BSD's, Haiku, MUSL, etc) and has excellent support for dynamic overriding.
  At the same time, it is an industrial strength allocator that runs (very) large scale
  distributed services on thousands of machines with excellent worst case latencies.
- __free list sharding__: instead of one big free list (per size class) we have
  many smaller lists per "mimalloc page" which reduces fragmentation and
  increases locality --
  things that are allocated close in time get allocated close in memory.
  (A mimalloc page contains blocks of one size class and is usually 64KiB on a 64-bit system).
- __free list multi-sharding__: the big idea! Not only do we shard the free list
  per mimalloc page, but for each page we have multiple free lists. In particular, there
  is one list for thread-local `free` operations, and another one for concurrent `free`
  operations. Free-ing from another thread can now be a single CAS without needing
  sophisticated coordination between threads. Since there will be
  thousands of separate free lists, contention is naturally distributed over the heap,
  and the chance of contending on a single location will be low -- this is quite
  similar to randomized algorithms like skip lists where adding
  a random oracle removes the need for a more complex algorithm.
- __eager page purging__: when a "page" becomes empty (with increased chance
  due to free list sharding) the memory is marked to the OS as unused (reset or decommitted)
  reducing (real) memory pressure and fragmentation, especially in long running
  programs.
- __secure__: _mimalloc_ can be built in secure mode, adding guard pages,
  randomized allocation, encrypted free lists, etc. to protect against various
  heap vulnerabilities. The performance penalty is usually around 10% on average
  over our benchmarks.
- __first-class heaps__: efficiently create and use multiple heaps to allocate across different regions.
  A heap can be destroyed at once instead of deallocating each object separately.
- __bounded__: it does not suffer from _blowup_ \[1\], has bounded worst-case allocation
  times (_wcat_) (upto OS primitives), bounded space overhead (~0.2% meta-data, with low
  internal fragmentation), and has no internal points of contention using only atomic operations.
- __fast__: In our benchmarks (see [below](#bench)),
  _mimalloc_ outperforms other leading allocators (_jemalloc_, _tcmalloc_, _Hoard_, etc),
  and often uses less memory. A nice property is that it does consistently well over a wide range
  of benchmarks. There is also good huge OS page support for larger server programs.

You can read more on the design of _mimalloc_ in the
[technical report](https://www.microsoft.com/en-us/research/publication/mimalloc-free-list-sharding-in-action)
which also has detailed benchmark results.


Further information:

- \ref build
- \ref using
- \ref environment
- \ref overrides
- \ref bench
- \ref malloc
- \ref extended
- \ref aligned
- \ref heap
- \ref typed
- \ref analysis
- \ref options
- \ref posix
- \ref cpp

*/


/// \defgroup malloc Basic Allocation
/// The basic allocation interface.
/// \{


/// Free previously allocated memory.
/// The pointer `p` must have been allocated before (or be \a NULL).
/// @param p  pointer to free, or \a NULL.
void  mi_free(void* p);

/// Allocate \a size bytes.
/// @param size  number of bytes to allocate.
/// @returns pointer to the allocated memory or \a NULL if out of memory.
/// Returns a unique pointer if called with \a size 0.
void* mi_malloc(size_t size);

/// Allocate zero-initialized `size` bytes.
/// @param size The size in bytes.
/// @returns Pointer to newly allocated zero initialized memory,
/// or \a NULL if out of memory.
void* mi_zalloc(size_t size);

/// Allocate zero-initialized \a count elements of \a size bytes.
/// @param count number of elements.
/// @param size  size of each element.
/// @returns pointer to the allocated memory
/// of \a size*\a count bytes, or \a NULL if either out of memory
/// or when `count*size` overflows.
///
/// Returns a unique pointer if called with either \a size or \a count of 0.
/// @see mi_zalloc()
void* mi_calloc(size_t count, size_t size);

/// Re-allocate memory to \a newsize bytes.
/// @param p  pointer to previously allocated memory (or \a NULL).
/// @param newsize  the new required size in bytes.
/// @returns pointer to the re-allocated memory
/// of \a newsize bytes, or \a NULL if out of memory.
/// If \a NULL is returned, the pointer \a p is not freed.
/// Otherwise the original pointer is either freed or returned
/// as the reallocated result (in case it fits in-place with the
/// new size). If the pointer \a p is \a NULL, it behaves as
/// \a mi_malloc(\a newsize). If \a newsize is larger than the
/// original \a size allocated for \a p, the bytes after \a size
/// are uninitialized.
void* mi_realloc(void* p, size_t newsize);

/// Re-allocate memory to \a count elements of \a size bytes, with extra memory initialized to zero.
/// @param p Pointer to a previously allocated block (or \a NULL).
/// @param count The number of elements.
/// @param size The size of each element.
/// @returns A pointer to a re-allocated block of \a count * \a size bytes, or \a NULL
/// if out of memory or if \a count * \a size overflows.
///
/// If there is no overflow, it behaves exactly like `mi_rezalloc(p,count*size)`.
/// @see mi_reallocn()
/// @see [recallocarray()](http://man.openbsd.org/reallocarray) (on BSD).
void* mi_recalloc(void* p, size_t count, size_t size);

/// Try to re-allocate memory to \a newsize bytes _in place_.
/// @param p  pointer to previously allocated memory (or \a NULL).
/// @param newsize  the new required size in bytes.
/// @returns pointer to the re-allocated memory
/// of \a newsize bytes (always equal to \a p),
/// or \a NULL if either out of memory or if
/// the memory could not be expanded in place.
/// If \a NULL is returned, the pointer \a p is not freed.
/// Otherwise the original pointer is returned
/// as the reallocated result since it fits in-place with the
/// new size. If \a newsize is larger than the
/// original \a size allocated for \a p, the bytes after \a size
/// are uninitialized.
void* mi_expand(void* p, size_t newsize);

/// Allocate \a count elements of \a size bytes.
/// @param count The number of elements.
/// @param size The size of each element.
/// @returns A pointer to a block of \a count * \a size bytes, or \a NULL
/// if out of memory or if \a count * \a size overflows.
///
/// If there is no overflow, it behaves exactly like `mi_malloc(count*size)`.
/// @see mi_calloc()
/// @see mi_zallocn()
void* mi_mallocn(size_t count, size_t size);

/// Re-allocate memory to \a count elements of \a size bytes.
/// @param p Pointer to a previously allocated block (or \a NULL).
/// @param count The number of elements.
/// @param size The size of each element.
/// @returns A pointer to a re-allocated block of \a count * \a size bytes, or \a NULL
/// if out of memory or if \a count * \a size overflows.
///
/// If there is no overflow, it behaves exactly like `mi_realloc(p,count*size)`.
/// @see [reallocarray()](<http://man.openbsd.org/reallocarray>) (on BSD)
void* mi_reallocn(void* p, size_t count, size_t size);

/// Re-allocate memory to \a newsize bytes,
/// @param p  pointer to previously allocated memory (or \a NULL).
/// @param newsize  the new required size in bytes.
/// @returns pointer to the re-allocated memory
/// of \a newsize bytes, or \a NULL if out of memory.
///
/// In contrast to mi_realloc(), if \a NULL is returned, the original pointer
/// \a p is freed (if it was not \a NULL itself).
/// Otherwise the original pointer is either freed or returned
/// as the reallocated result (in case it fits in-place with the
/// new size). If the pointer \a p is \a NULL, it behaves as
/// \a mi_malloc(\a newsize). If \a newsize is larger than the
/// original \a size allocated for \a p, the bytes after \a size
/// are uninitialized.
///
/// @see [reallocf](https://www.freebsd.org/cgi/man.cgi?query=reallocf) (on BSD)
void* mi_reallocf(void* p, size_t newsize);


/// Allocate and duplicate a string.
/// @param s string to duplicate (or \a NULL).
/// @returns a pointer to newly allocated memory initialized
/// to string \a s, or \a NULL if either out of memory or if
/// \a s is \a NULL.
///
/// Replacement for the standard [strdup()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/strdup.html)
/// such that mi_free() can be used on the returned result.
char* mi_strdup(const char* s);

/// Allocate and duplicate a string up to \a n bytes.
/// @param s string to duplicate (or \a NULL).
/// @param n maximum number of bytes to copy (excluding the terminating zero).
/// @returns a pointer to newly allocated memory initialized
/// to string \a s up to the first \a n bytes (and always zero terminated),
/// or \a NULL if either out of memory or if \a s is \a NULL.
///
/// Replacement for the standard [strndup()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/strndup.html)
/// such that mi_free() can be used on the returned result.
char* mi_strndup(const char* s, size_t n);

/// Resolve a file path name.
/// @param fname File name.
/// @param resolved_name Should be \a NULL (but can also point to a buffer
///                      of at least \a PATH_MAX bytes).
/// @returns If successful a pointer to the resolved absolute file name, or
/// \a NULL on failure (with \a errno set to the error code).
///
/// If \a resolved_name was \a NULL, the returned result should be freed with
/// mi_free().
///
/// Replacement for the standard [realpath()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/realpath.html)
/// such that mi_free() can be used on the returned result (if \a resolved_name was \a NULL).
char* mi_realpath(const char* fname, char* resolved_name);

/// \}

// ------------------------------------------------------
// Extended functionality
// ------------------------------------------------------

/// \defgroup extended Extended Functions
/// Extended functionality.
/// \{

/// Maximum size allowed for small allocations in
/// #mi_malloc_small and #mi_zalloc_small (usually `128*sizeof(void*)` (= 1KB on 64-bit systems))
#define MI_SMALL_SIZE_MAX   (128*sizeof(void*))

/// Allocate a small object.
/// @param size The size in bytes, can be at most #MI_SMALL_SIZE_MAX.
/// @returns a pointer to newly allocated memory of at least \a size
/// bytes, or \a NULL if out of memory.
/// This function is meant for use in run-time systems for best
/// performance and does not check if \a size was indeed small -- use
/// with care!
void* mi_malloc_small(size_t size);

/// Allocate a zero initialized small object.
/// @param size The size in bytes, can be at most #MI_SMALL_SIZE_MAX.
/// @returns a pointer to newly allocated zero-initialized memory of at
/// least \a size bytes, or \a NULL if out of memory.
/// This function is meant for use in run-time systems for best
/// performance and does not check if \a size was indeed small -- use
/// with care!
void* mi_zalloc_small(size_t size);

/// Return the available bytes in a memory block.
/// @param p Pointer to previously allocated memory (or \a NULL)
/// @returns Returns the available bytes in the memory block, or
/// 0 if \a p was \a NULL.
///
/// The returned size can be
/// used to call \a mi_expand successfully.
/// The returned size is always at least equal to the
/// allocated size of \a p.
///
/// @see [_msize](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/msize?view=vs-2017) (Windows)
/// @see [malloc_usable_size](http://man7.org/linux/man-pages/man3/malloc_usable_size.3.html) (Linux)
/// @see mi_good_size()
size_t mi_usable_size(void* p);

/// Return the used allocation size.
/// @param size The minimal required size in bytes.
/// @returns the size `n` that will be allocated, where `n >= size`.
///
/// Generally, `mi_usable_size(mi_malloc(size)) == mi_good_size(size)`.
/// This can be used to reduce internal wasted space when
/// allocating buffers for example.
///
/// @see mi_usable_size()
size_t mi_good_size(size_t size);

/// Eagerly free memory.
/// @param force If \a true, aggressively return memory to the OS (can be expensive!)
///
/// Regular code should not have to call this function. It can be beneficial
/// in very narrow circumstances; in particular, when a long running thread
/// allocates a lot of blocks that are freed by other threads it may improve
/// resource usage by calling this every once in a while.
void mi_collect(bool force);

/// Deprecated
/// @param out Ignored, outputs to the registered output function or stderr by default.
///
/// Most detailed when using a debug build.
void mi_stats_print(void* out);

/// Print the main statistics.
/// @param out An output function or \a NULL for the default.
/// @param arg Optional argument passed to \a out (if not \a NULL)
///
/// Most detailed when using a debug build.
void mi_stats_print_out(mi_output_fun* out, void* arg);

/// Reset statistics.
void mi_stats_reset(void);

/// Merge thread local statistics with the main statistics and reset.
void mi_stats_merge(void);

/// Initialize mimalloc on a thread.
/// Should not be used as on most systems (pthreads, windows) this is done
/// automatically.
void mi_thread_init(void);

/// Uninitialize mimalloc on a thread.
/// Should not be used as on most systems (pthreads, windows) this is done
/// automatically. Ensures that any memory that is not freed yet (but will
/// be freed by other threads in the future) is properly handled.
void mi_thread_done(void);

/// Print out heap statistics for this thread.
/// @param out An output function or \a NULL for the default.
/// @param arg Optional argument passed to \a out (if not \a NULL)
///
/// Most detailed when using a debug build.
void mi_thread_stats_print_out(mi_output_fun* out, void* arg);

/// Type of deferred free functions.
/// @param force If \a true all outstanding items should be freed.
/// @param heartbeat A monotonically increasing count.
/// @param arg Argument that was passed at registration to hold extra state.
///
/// @see mi_register_deferred_free
typedef void (mi_deferred_free_fun)(bool force, unsigned long long heartbeat, void* arg);

/// Register a deferred free function.
/// @param deferred_free Address of a deferred free-ing function or \a NULL to unregister.
/// @param arg Argument that will be passed on to the deferred free function.
///
/// Some runtime systems use deferred free-ing, for example when using
/// reference counting to limit the worst case free time.
/// Such systems can register (re-entrant) deferred free function
/// to free more memory on demand. When the \a force parameter is
/// \a true all possible memory should be freed.
/// The per-thread \a heartbeat parameter is monotonically increasing
/// and guaranteed to be deterministic if the program allocates
/// deterministically. The \a deferred_free function is guaranteed
/// to be called deterministically after some number of allocations
/// (regardless of freeing or available free memory).
/// At most one \a deferred_free function can be active.
void   mi_register_deferred_free(mi_deferred_free_fun* deferred_free, void* arg);

/// Type of output functions.
/// @param msg Message to output.
/// @param arg Argument that was passed at registration to hold extra state.
///
/// @see mi_register_output()
typedef void (mi_output_fun)(const char* msg, void* arg);

/// Register an output function.
/// @param out The output function, use `NULL` to output to stderr.
/// @param arg Argument that will be passed on to the output function.
///
/// The `out` function is called to output any information from mimalloc,
/// like verbose or warning messages.
void mi_register_output(mi_output_fun* out, void* arg);

/// Type of error callback functions.
/// @param err Error code (see mi_register_error() for a complete list).
/// @param arg Argument that was passed at registration to hold extra state.
///
/// @see mi_register_error()
typedef void (mi_error_fun)(int err, void* arg);

/// Register an error callback function.
/// @param errfun The error function that is called on an error (use \a NULL for default)
/// @param arg Extra argument that will be passed on to the error function.
///
/// The \a errfun function is called on an error in mimalloc after emitting
/// an error message (through the output function). It as always legal to just
/// return from the \a errfun function in which case allocation functions generally
/// return \a NULL or ignore the condition. The default function only calls abort()
/// when compiled in secure mode with an \a EFAULT error. The possible error
/// codes are:
/// * \a EAGAIN: Double free was detected (only in debug and secure mode).
/// * \a EFAULT: Corrupted free list or meta-data was detected (only in debug and secure mode).
/// * \a ENOMEM: Not enough memory available to satisfy the request.
/// * \a EOVERFLOW: Too large a request, for example in mi_calloc(), the \a count and \a size parameters are too large.
/// * \a EINVAL: Trying to free or re-allocate an invalid pointer.
void mi_register_error(mi_error_fun* errfun, void* arg);

/// Is a pointer part of our heap?
/// @param p The pointer to check.
/// @returns \a true if this is a pointer into our heap.
/// This function is relatively fast.
bool mi_is_in_heap_region(const void* p);

/// Reserve OS memory for use by mimalloc. Reserved areas are used
/// before allocating from the OS again. By reserving a large area upfront,
/// allocation can be more efficient, and can be better managed on systems
/// without `mmap`/`VirtualAlloc` (like WASM for example).
/// @param size        The size to reserve.
/// @param commit      Commit the memory upfront.
/// @param allow_large Allow large OS pages (2MiB) to be used?
/// @return \a 0 if successful, and an error code otherwise (e.g. `ENOMEM`).
int  mi_reserve_os_memory(size_t size, bool commit, bool allow_large);

/// Manage a particular memory area for use by mimalloc.
/// This is just like `mi_reserve_os_memory` except that the area should already be
/// allocated in some manner and available for use my mimalloc.
/// @param start       Start of the memory area
/// @param size        The size of the memory area.
/// @param is_committed Is the area already committed?
/// @param is_large    Does it consist of large OS pages? Set this to \a true as well for memory
///                    that should not be decommitted or protected (like rdma etc.)
/// @param is_zero     Does the area consists of zero's?
/// @param numa_node   Possible associated numa node or `-1`.
/// @return \a true if successful, and \a false on error.
bool mi_manage_os_memory(void* start, size_t size, bool is_committed, bool is_large, bool is_zero, int numa_node);

/// Reserve \a pages of huge OS pages (1GiB) evenly divided over \a numa_nodes nodes,
/// but stops after at most `timeout_msecs` seconds.
/// @param pages The number of 1GiB pages to reserve.
/// @param numa_nodes The number of nodes do evenly divide the pages over, or 0 for using the actual number of NUMA nodes.
/// @param timeout_msecs Maximum number of milli-seconds to try reserving, or 0 for no timeout.
/// @returns 0 if successful, \a ENOMEM if running out of memory, or \a ETIMEDOUT if timed out.
///
/// The reserved memory is used by mimalloc to satisfy allocations.
/// May quit before \a timeout_msecs are expired if it estimates it will take more than
/// 1.5 times \a timeout_msecs. The time limit is needed because on some operating systems
/// it can take a long time to reserve contiguous memory if the physical memory is
/// fragmented.
int mi_reserve_huge_os_pages_interleave(size_t pages, size_t numa_nodes, size_t timeout_msecs);

/// Reserve \a pages of huge OS pages (1GiB) at a specific \a numa_node,
/// but stops after at most `timeout_msecs` seconds.
/// @param pages The number of 1GiB pages to reserve.
/// @param numa_node The NUMA node where the memory is reserved (start at 0). Use -1 for no affinity.
/// @param timeout_msecs Maximum number of milli-seconds to try reserving, or 0 for no timeout.
/// @returns 0 if successful, \a ENOMEM if running out of memory, or \a ETIMEDOUT if timed out.
///
/// The reserved memory is used by mimalloc to satisfy allocations.
/// May quit before \a timeout_msecs are expired if it estimates it will take more than
/// 1.5 times \a timeout_msecs. The time limit is needed because on some operating systems
/// it can take a long time to reserve contiguous memory if the physical memory is
/// fragmented.
int mi_reserve_huge_os_pages_at(size_t pages, int numa_node, size_t timeout_msecs);


/// Is the C runtime \a malloc API redirected?
/// @returns \a true if all malloc API calls are redirected to mimalloc.
///
/// Currently only used on Windows.
bool mi_is_redirected();

/// Return process information (time and memory usage).
/// @param elapsed_msecs   Optional. Elapsed wall-clock time of the process in milli-seconds.
/// @param user_msecs      Optional. User time in milli-seconds (as the sum over all threads).
/// @param system_msecs    Optional. System time in milli-seconds.
/// @param current_rss     Optional. Current working set size (touched pages).
/// @param peak_rss        Optional. Peak working set size (touched pages).
/// @param current_commit  Optional. Current committed memory (backed by the page file).
/// @param peak_commit     Optional. Peak committed memory (backed by the page file).
/// @param page_faults     Optional. Count of hard page faults.
///
/// The \a current_rss is precise on Windows and MacOSX; other systems estimate
/// this using \a current_commit. The \a commit is precise on Windows but estimated
/// on other systems as the amount of read/write accessible memory reserved by mimalloc.
void mi_process_info(size_t* elapsed_msecs, size_t* user_msecs, size_t* system_msecs, size_t* current_rss, size_t* peak_rss, size_t* current_commit, size_t* peak_commit, size_t* page_faults);

/// @brief Show all current arena's.
/// @param show_inuse       Show the arena blocks that are in use.
/// @param show_abandoned   Show the abandoned arena blocks.
/// @param show_purge       Show arena blocks scheduled for purging.
void mi_debug_show_arenas(bool show_inuse, bool show_abandoned, bool show_purge);

/// Mimalloc uses large (virtual) memory areas, called "arena"s, from the OS to manage its memory.
/// Each arena has an associated identifier.
typedef int mi_arena_id_t;

/// @brief  Return the size of an arena.
/// @param arena_id  The arena identifier.
/// @param size      Returned size in bytes of the (virtual) arena area.
/// @return base address of the arena.
void* mi_arena_area(mi_arena_id_t arena_id, size_t* size);

/// @brief Reserve huge OS pages (1GiB) into a single arena.
/// @param pages             Number of 1GiB pages to reserve.
/// @param numa_node         The associated NUMA node, or -1 for no NUMA preference.
/// @param timeout_msecs     Max amount of milli-seconds this operation is allowed to take. (0 is infinite)
/// @param exclusive         If exclusive, only a heap associated with this arena can allocate in it.
/// @param arena_id          The arena identifier.
/// @return 0 if successful, \a ENOMEM if running out of memory, or \a ETIMEDOUT if timed out.
int   mi_reserve_huge_os_pages_at_ex(size_t pages, int numa_node, size_t timeout_msecs, bool exclusive, mi_arena_id_t* arena_id);

/// @brief Reserve OS memory to be managed in an arena.
/// @param size Size the reserve.
/// @param commit Should the memory be initially committed?
/// @param allow_large Allow the use of large OS pages?
/// @param exclusive  Is the returned arena exclusive?
/// @param arena_id The new arena identifier.
/// @return Zero on success, an error code otherwise.
int   mi_reserve_os_memory_ex(size_t size, bool commit, bool allow_large, bool exclusive, mi_arena_id_t* arena_id);

/// @brief Manage externally allocated memory as a mimalloc arena. This memory will not be freed by mimalloc.
/// @param start Start address of the area.
/// @param size  Size in bytes of the area.
/// @param is_committed  Is the memory already committed?
/// @param is_large      Does it consist of (pinned) large OS pages?
/// @param is_zero       Is the memory zero-initialized?
/// @param numa_node     Associated NUMA node, or -1 to have no NUMA preference.
/// @param exclusive     Is the arena exclusive (where only heaps associated with the arena can allocate in it)
/// @param arena_id      The new arena identifier.
/// @return `true` if successful.
bool  mi_manage_os_memory_ex(void* start, size_t size, bool is_committed, bool is_large, bool is_zero, int numa_node, bool exclusive, mi_arena_id_t* arena_id);

/// @brief Create a new heap that only allocates in the specified arena.
/// @param arena_id The arena identifier.
/// @return The new heap or `NULL`.
mi_heap_t* mi_heap_new_in_arena(mi_arena_id_t arena_id);

/// @brief Create a new heap
/// @param heap_tag       The heap tag associated with this heap; heaps only reclaim memory between heaps with the same tag.
/// @param allow_destroy  Is \a mi_heap_destroy allowed?  Not allowing this allows the heap to reclaim memory from terminated threads.
/// @param arena_id       If not 0, the heap will only allocate from the specified arena.
/// @return A new heap or `NULL` on failure.
///
/// The \a arena_id can be used by runtimes to allocate only in a specified pre-reserved arena.
/// This is used for example for a compressed pointer heap in Koka.
/// The \a heap_tag enables heaps to keep objects of a certain type isolated to heaps with that tag.
/// This is used for example in the CPython integration.
mi_heap_t* mi_heap_new_ex(int heap_tag, bool allow_destroy, mi_arena_id_t arena_id);

/// A process can associate threads with sub-processes.
/// A sub-process will not reclaim memory from (abandoned heaps/threads)
/// other subprocesses.
typedef void* mi_subproc_id_t;

/// @brief  Get the main sub-process identifier.
mi_subproc_id_t mi_subproc_main(void);

/// @brief Create a fresh sub-process (with no associated threads yet).
/// @return The new sub-process identifier.
mi_subproc_id_t mi_subproc_new(void);

/// @brief Delete a previously created sub-process.
/// @param subproc The sub-process identifier.
/// Only delete sub-processes if all associated threads have terminated.
void mi_subproc_delete(mi_subproc_id_t subproc);

/// Add the current thread to the given sub-process.
/// This should be called right after a thread is created (and no allocation has taken place yet)
void mi_subproc_add_current_thread(mi_subproc_id_t subproc);


/// \}

// ------------------------------------------------------
// Aligned allocation
// ------------------------------------------------------

/// \defgroup aligned Aligned Allocation
///
/// Allocating aligned memory blocks.
/// Note that `alignment` always follows `size` for consistency with the unaligned
/// allocation API, but unfortunately this differs from `posix_memalign` and `aligned_alloc` in the C library.
///
/// \{

/// Allocate \a size bytes aligned by \a alignment.
/// @param size  number of bytes to allocate.
/// @param alignment  the minimal alignment of the allocated memory.
/// @returns pointer to the allocated memory or \a NULL if out of memory,
/// or if the alignment is not a power of 2 (including 0). The \a size is unrestricted
/// (and does not have to be an integral multiple of the \a alignment).
/// The returned pointer is aligned by \a alignment, i.e. `(uintptr_t)p % alignment == 0`.
/// Returns a unique pointer if called with \a size 0.
///
/// Note that `alignment` always follows `size` for consistency with the unaligned
/// allocation API, but unfortunately this differs from `posix_memalign` and `aligned_alloc` in the C library.
///
/// @see [aligned_alloc](https://en.cppreference.com/w/c/memory/aligned_alloc) (in the standard C11 library, with switched arguments!)
/// @see [_aligned_malloc](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc?view=vs-2017) (on Windows)
/// @see [aligned_alloc](http://man.openbsd.org/reallocarray) (on BSD, with switched arguments!)
/// @see [posix_memalign](https://linux.die.net/man/3/posix_memalign) (on Posix, with switched arguments!)
/// @see [memalign](https://linux.die.net/man/3/posix_memalign) (on Linux, with switched arguments!)
void* mi_malloc_aligned(size_t size, size_t alignment);
void* mi_zalloc_aligned(size_t size, size_t alignment);
void* mi_calloc_aligned(size_t count, size_t size, size_t alignment);
void* mi_realloc_aligned(void* p, size_t newsize, size_t alignment);

/// Allocate \a size bytes aligned by \a alignment at a specified \a offset.
/// @param size  number of bytes to allocate.
/// @param alignment  the minimal alignment of the allocated memory at \a offset.
/// @param offset     the offset that should be aligned.
/// @returns pointer to the allocated memory or \a NULL if out of memory,
/// or if the alignment is not a power of 2 (including 0). The \a size is unrestricted
/// (and does not have to be an integral multiple of the \a alignment).
/// The returned pointer is aligned by \a alignment, i.e. `(uintptr_t)p % alignment == 0`.
/// Returns a unique pointer if called with \a size 0.
///
/// @see [_aligned_offset_malloc](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-offset-malloc?view=vs-2017) (on Windows)
void* mi_malloc_aligned_at(size_t size, size_t alignment, size_t offset);
void* mi_zalloc_aligned_at(size_t size, size_t alignment, size_t offset);
void* mi_calloc_aligned_at(size_t count, size_t size, size_t alignment, size_t offset);
void* mi_realloc_aligned_at(void* p, size_t newsize, size_t alignment, size_t offset);

/// \}

/// \defgroup heap Heap Allocation
///
/// First-class heaps that can be destroyed in one go.
///
/// \{

/// Type of first-class heaps.
/// A heap can only be used for allocation in
/// the thread that created this heap! Any allocated
/// blocks can be freed or reallocated by any other thread though.
struct mi_heap_s;

/// Type of first-class heaps.
/// A heap can only be used for (re)allocation in
/// the thread that created this heap! Any allocated
/// blocks can be freed by any other thread though.
typedef struct mi_heap_s mi_heap_t;

/// Create a new heap that can be used for allocation.
mi_heap_t* mi_heap_new();

/// Delete a previously allocated heap.
/// This will release resources and migrate any
/// still allocated blocks in this heap (efficiently)
/// to the default heap.
///
/// If \a heap is the default heap, the default
/// heap is set to the backing heap.
void mi_heap_delete(mi_heap_t* heap);

/// Destroy a heap, freeing all its still allocated blocks.
/// Use with care as this will free all blocks still
/// allocated in the heap. However, this can be a very
/// efficient way to free all heap memory in one go.
///
/// If \a heap is the default heap, the default
/// heap is set to the backing heap.
void mi_heap_destroy(mi_heap_t* heap);

/// Set the default heap to use in the current thread for mi_malloc() et al.
/// @param heap  The new default heap.
/// @returns The previous default heap.
mi_heap_t* mi_heap_set_default(mi_heap_t* heap);

/// Get the default heap that is used for mi_malloc() et al. (for the current thread).
/// @returns The current default heap.
mi_heap_t* mi_heap_get_default();

/// Get the backing heap.
/// The _backing_ heap is the initial default heap for
/// a thread and always available for allocations.
/// It cannot be destroyed or deleted
/// except by exiting the thread.
mi_heap_t* mi_heap_get_backing();

/// Release outstanding resources in a specific heap.
void mi_heap_collect(mi_heap_t* heap, bool force);

/// Allocate in a specific heap.
/// @see mi_malloc()
void* mi_heap_malloc(mi_heap_t* heap, size_t size);

/// Allocate a small object in a specific heap.
/// \a size must be smaller or equal to MI_SMALL_SIZE_MAX().
/// @see mi_malloc()
void* mi_heap_malloc_small(mi_heap_t* heap, size_t size);

/// Allocate zero-initialized in a specific heap.
/// @see mi_zalloc()
void* mi_heap_zalloc(mi_heap_t* heap, size_t size);

/// Allocate \a count zero-initialized elements in a specific heap.
/// @see mi_calloc()
void* mi_heap_calloc(mi_heap_t* heap, size_t count, size_t size);

/// Allocate \a count elements in a specific heap.
/// @see mi_mallocn()
void* mi_heap_mallocn(mi_heap_t* heap, size_t count, size_t size);

/// Duplicate a string in a specific heap.
/// @see mi_strdup()
char* mi_heap_strdup(mi_heap_t* heap, const char* s);

/// Duplicate a string of at most length \a n in a specific heap.
/// @see mi_strndup()
char* mi_heap_strndup(mi_heap_t* heap, const char* s, size_t n);

/// Resolve a file path name using a specific \a heap to allocate the result.
/// @see mi_realpath()
char* mi_heap_realpath(mi_heap_t* heap, const char* fname, char* resolved_name);

void* mi_heap_realloc(mi_heap_t* heap, void* p, size_t newsize);
void* mi_heap_reallocn(mi_heap_t* heap, void* p, size_t count, size_t size);
void* mi_heap_reallocf(mi_heap_t* heap, void* p, size_t newsize);

void* mi_heap_malloc_aligned(mi_heap_t* heap, size_t size, size_t alignment);
void* mi_heap_malloc_aligned_at(mi_heap_t* heap, size_t size, size_t alignment, size_t offset);
void* mi_heap_zalloc_aligned(mi_heap_t* heap, size_t size, size_t alignment);
void* mi_heap_zalloc_aligned_at(mi_heap_t* heap, size_t size, size_t alignment, size_t offset);
void* mi_heap_calloc_aligned(mi_heap_t* heap, size_t count, size_t size, size_t alignment);
void* mi_heap_calloc_aligned_at(mi_heap_t* heap, size_t count, size_t size, size_t alignment, size_t offset);
void* mi_heap_realloc_aligned(mi_heap_t* heap, void* p, size_t newsize, size_t alignment);
void* mi_heap_realloc_aligned_at(mi_heap_t* heap, void* p, size_t newsize, size_t alignment, size_t offset);

/// \}


/// \defgroup zeroinit Zero initialized re-allocation
///
/// The zero-initialized re-allocations are only valid on memory that was
/// originally allocated with zero initialization too.
/// e.g. `mi_calloc`, `mi_zalloc`, `mi_zalloc_aligned` etc.
/// see <https://github.com/microsoft/mimalloc/issues/63#issuecomment-508272992>
///
/// \{

void* mi_rezalloc(void* p, size_t newsize);
void* mi_recalloc(void* p, size_t newcount, size_t size) ;

void* mi_rezalloc_aligned(void* p, size_t newsize, size_t alignment);
void* mi_rezalloc_aligned_at(void* p, size_t newsize, size_t alignment, size_t offset);
void* mi_recalloc_aligned(void* p, size_t newcount, size_t size, size_t alignment);
void* mi_recalloc_aligned_at(void* p, size_t newcount, size_t size, size_t alignment, size_t offset);

void* mi_heap_rezalloc(mi_heap_t* heap, void* p, size_t newsize);
void* mi_heap_recalloc(mi_heap_t* heap, void* p, size_t newcount, size_t size);

void* mi_heap_rezalloc_aligned(mi_heap_t* heap, void* p, size_t newsize, size_t alignment);
void* mi_heap_rezalloc_aligned_at(mi_heap_t* heap, void* p, size_t newsize, size_t alignment, size_t offset);
void* mi_heap_recalloc_aligned(mi_heap_t* heap, void* p, size_t newcount, size_t size, size_t alignment);
void* mi_heap_recalloc_aligned_at(mi_heap_t* heap, void* p, size_t newcount, size_t size, size_t alignment, size_t offset);

/// \}

/// \defgroup typed Typed Macros
///
/// Typed allocation macros. For example:
/// ```
/// int* p = mi_malloc_tp(int)
/// ```
///
/// \{

/// Allocate a block of type \a tp.
/// @param tp The type of the block to allocate.
/// @returns A pointer to an object of type \a tp, or
/// \a NULL if out of memory.
///
/// **Example:**
/// ```
/// int* p = mi_malloc_tp(int)
/// ```
///
/// @see mi_malloc()
#define mi_malloc_tp(tp)        ((tp*)mi_malloc(sizeof(tp)))

/// Allocate a zero-initialized block of type \a tp.
#define mi_zalloc_tp(tp)        ((tp*)mi_zalloc(sizeof(tp)))

/// Allocate \a count zero-initialized blocks of type \a tp.
#define mi_calloc_tp(tp,count)      ((tp*)mi_calloc(count,sizeof(tp)))

/// Allocate \a count blocks of type \a tp.
#define mi_mallocn_tp(tp,count)     ((tp*)mi_mallocn(count,sizeof(tp)))

/// Re-allocate to \a count blocks of type \a tp.
#define mi_reallocn_tp(p,tp,count)  ((tp*)mi_reallocn(p,count,sizeof(tp)))

/// Allocate a block of type \a tp in a heap \a hp.
#define mi_heap_malloc_tp(hp,tp)        ((tp*)mi_heap_malloc(hp,sizeof(tp)))

/// Allocate a zero-initialized block of type \a tp in a heap \a hp.
#define mi_heap_zalloc_tp(hp,tp)        ((tp*)mi_heap_zalloc(hp,sizeof(tp)))

/// Allocate \a count zero-initialized blocks of type \a tp in a heap \a hp.
#define mi_heap_calloc_tp(hp,tp,count)      ((tp*)mi_heap_calloc(hp,count,sizeof(tp)))

/// Allocate \a count blocks of type \a tp in a heap \a hp.
#define mi_heap_mallocn_tp(hp,tp,count)     ((tp*)mi_heap_mallocn(hp,count,sizeof(tp)))

/// Re-allocate to \a count blocks of type \a tp in a heap \a hp.
#define mi_heap_reallocn_tp(hp,p,tp,count)  ((tp*)mi_heap_reallocn(p,count,sizeof(tp)))

/// Re-allocate to \a count zero initialized blocks of type \a tp in a heap \a hp.
#define mi_heap_recalloc_tp(hp,p,tp,count)  ((tp*)mi_heap_recalloc(p,count,sizeof(tp)))

/// \}

/// \defgroup analysis Heap Introspection
///
/// Inspect the heap at runtime.
///
/// \{

/// Does a heap contain a pointer to a previously allocated block?
/// @param heap The heap.
/// @param p Pointer to a previously allocated block (in any heap)-- cannot be some
///          random pointer!
/// @returns \a true if the block pointed to by \a p is in the \a heap.
/// @see mi_heap_check_owned()
bool mi_heap_contains_block(mi_heap_t* heap, const void* p);

/// Check safely if any pointer is part of a heap.
/// @param heap The heap.
/// @param p   Any pointer -- not required to be previously allocated by us.
/// @returns \a true if \a p points to a block in \a heap.
///
/// Note: expensive function, linear in the pages in the heap.
/// @see mi_heap_contains_block()
/// @see mi_heap_get_default()
bool mi_heap_check_owned(mi_heap_t* heap, const void* p);

/// Check safely if any pointer is part of the default heap of this thread.
/// @param p   Any pointer -- not required to be previously allocated by us.
/// @returns \a true if \a p points to a block in default heap of this thread.
///
/// Note: expensive function, linear in the pages in the heap.
/// @see mi_heap_contains_block()
/// @see mi_heap_get_default()
bool mi_check_owned(const void* p);

/// An area of heap space contains blocks of a single size.
/// The bytes in freed blocks are `committed - used`.
typedef struct mi_heap_area_s {
  void*  blocks;      ///< start of the area containing heap blocks
  size_t reserved;    ///< bytes reserved for this area
  size_t committed;   ///< current committed bytes of this area
  size_t used;        ///< bytes in use by allocated blocks
  size_t block_size;  ///< size in bytes of one block
  size_t full_block_size; ///< size in bytes of a full block including padding and metadata.
  int    heap_tag;    ///< heap tag associated with this area (see \a mi_heap_new_ex)
} mi_heap_area_t;

/// Visitor function passed to mi_heap_visit_blocks()
/// @returns \a true if ok, \a false to stop visiting (i.e. break)
///
/// This function is always first called for every \a area
/// with \a block as a \a NULL pointer. If \a visit_all_blocks
/// was \a true, the function is then called for every allocated
/// block in that area.
typedef bool (mi_block_visit_fun)(const mi_heap_t* heap, const mi_heap_area_t* area, void* block, size_t block_size, void* arg);

/// Visit all areas and blocks in a heap.
/// @param heap The heap to visit.
/// @param visit_all_blocks If \a true visits all allocated blocks, otherwise
///                         \a visitor is only called for every heap area.
/// @param visitor This function is called for every area in the heap
///                 (with \a block as \a NULL). If \a visit_all_blocks is
///                 \a true, \a visitor is also called for every allocated
///                 block in every area (with `block!=NULL`).
///                 return \a false from this function to stop visiting early.
/// @param arg Extra argument passed to \a visitor.
/// @returns \a true if all areas and blocks were visited.
bool mi_heap_visit_blocks(const mi_heap_t* heap, bool visit_all_blocks, mi_block_visit_fun* visitor, void* arg);

/// @brief Visit all areas and blocks in abandoned heaps.
/// @param subproc_id The sub-process id associated with the abandoned heaps.
/// @param heap_tag Visit only abandoned memory with the specified heap tag, use -1 to visit all abandoned memory.
/// @param visit_blocks If \a true visits all allocated blocks, otherwise
///                         \a visitor is only called for every heap area.
/// @param visitor This function is called for every area in the heap
///                 (with \a block as \a NULL). If \a visit_all_blocks is
///                 \a true, \a visitor is also called for every allocated
///                 block in every area (with `block!=NULL`).
///                 return \a false from this function to stop visiting early.
/// @param arg extra argument passed to the \a visitor.
/// @return \a true if all areas and blocks were visited.
///
/// Note: requires the option `mi_option_visit_abandoned` to be set
/// at the start of the program.
bool mi_abandoned_visit_blocks(mi_subproc_id_t subproc_id, int heap_tag, bool visit_blocks, mi_block_visit_fun* visitor, void* arg);

/// \}

/// \defgroup options Runtime Options
///
/// Set runtime behavior.
///
/// \{

/// Runtime options.
typedef enum mi_option_e {
  // stable options
  mi_option_show_errors,  ///< Print error messages.
  mi_option_show_stats,   ///< Print statistics on termination.
  mi_option_verbose,      ///< Print verbose messages.
  mi_option_max_errors,                 ///< issue at most N error messages
  mi_option_max_warnings,               ///< issue at most N warning messages

  // advanced options
  mi_option_reserve_huge_os_pages,    ///< reserve N huge OS pages (1GiB pages) at startup
  mi_option_reserve_huge_os_pages_at, ///< Reserve N huge OS pages at a specific NUMA node N.
  mi_option_reserve_os_memory,        ///< reserve specified amount of OS memory in an arena at startup (internally, this value is in KiB; use `mi_option_get_size`)
  mi_option_allow_large_os_pages,     ///< allow large (2 or 4 MiB) OS pages, implies eager commit. If false, also disables THP for the process.
  mi_option_purge_decommits,          ///< should a memory purge decommit? (=1). Set to 0 to use memory reset on a purge (instead of decommit)
  mi_option_arena_reserve,            ///< initial memory size for arena reservation (= 1 GiB on 64-bit) (internally, this value is in KiB; use `mi_option_get_size`)
  mi_option_os_tag,                   ///< tag used for OS logging (macOS only for now) (=100)
  mi_option_retry_on_oom,             ///< retry on out-of-memory for N milli seconds (=400), set to 0 to disable retries. (only on windows)

  // experimental options
  mi_option_eager_commit,             ///< eager commit segments? (after `eager_commit_delay` segments) (enabled by default).
  mi_option_eager_commit_delay,       ///< the first N segments per thread are not eagerly committed (but per page in the segment on demand)
  mi_option_arena_eager_commit,       ///< eager commit arenas? Use 2 to enable just on overcommit systems (=2)
  mi_option_abandoned_page_purge,     ///< immediately purge delayed purges on thread termination
  mi_option_purge_delay,              ///< memory purging is delayed by N milli seconds; use 0 for immediate purging or -1 for no purging at all. (=10)
  mi_option_use_numa_nodes,           ///< 0 = use all available numa nodes, otherwise use at most N nodes.
  mi_option_disallow_os_alloc,        ///< 1 = do not use OS memory for allocation (but only programmatically reserved arenas)
  mi_option_limit_os_alloc,           ///< If set to 1, do not use OS memory for allocation (but only pre-reserved arenas)
  mi_option_max_segment_reclaim,        ///< max. percentage of the abandoned segments can be reclaimed per try (=10%)
  mi_option_destroy_on_exit,            ///< if set, release all memory on exit; sometimes used for dynamic unloading but can be unsafe
  mi_option_arena_purge_mult,           ///< multiplier for `purge_delay` for the purging delay for arenas (=10)
  mi_option_abandoned_reclaim_on_free,  ///< allow to reclaim an abandoned segment on a free (=1)
  mi_option_purge_extend_delay,         ///< extend purge delay on each subsequent delay (=1)
  mi_option_disallow_arena_alloc,       ///< 1 = do not use arena's for allocation (except if using specific arena id's)
  mi_option_visit_abandoned,            ///< allow visiting heap blocks from abandoned threads (=0)

  _mi_option_last
} mi_option_t;


bool  mi_option_is_enabled(mi_option_t option);
void  mi_option_enable(mi_option_t option);
void  mi_option_disable(mi_option_t option);
void  mi_option_set_enabled(mi_option_t option, bool enable);
void  mi_option_set_enabled_default(mi_option_t option, bool enable);

long   mi_option_get(mi_option_t option);
long   mi_option_get_clamp(mi_option_t option, long min, long max);
size_t mi_option_get_size(mi_option_t option);

void  mi_option_set(mi_option_t option, long value);
void  mi_option_set_default(mi_option_t option, long value);


/// \}

/// \defgroup posix Posix
///
///  `mi_` prefixed implementations of various Posix, Unix, and C++ allocation functions.
///  Defined for convenience as all redirect to the regular mimalloc API.
///
/// \{

/// Just as `free` but also checks if the pointer `p` belongs to our heap.
void   mi_cfree(void* p);
void* mi__expand(void* p, size_t newsize);

void*  mi_recalloc(void* p, size_t count, size_t size);
size_t mi_malloc_size(const void* p);
size_t mi_malloc_good_size(size_t size);
size_t mi_malloc_usable_size(const void *p);

int mi_posix_memalign(void** p, size_t alignment, size_t size);
int mi__posix_memalign(void** p, size_t alignment, size_t size);
void* mi_memalign(size_t alignment, size_t size);
void* mi_valloc(size_t size);
void* mi_pvalloc(size_t size);
void* mi_aligned_alloc(size_t alignment, size_t size);

unsigned short* mi_wcsdup(const unsigned short* s);
unsigned char*  mi_mbsdup(const unsigned char* s);
int mi_dupenv_s(char** buf, size_t* size, const char* name);
int mi_wdupenv_s(unsigned short** buf, size_t* size, const unsigned short* name);

/// Correspond s to [reallocarray](https://www.freebsd.org/cgi/man.cgi?query=reallocarray&sektion=3&manpath=freebsd-release-ports)
/// in FreeBSD.
void* mi_reallocarray(void* p, size_t count, size_t size);

/// Corresponds to [reallocarr](https://man.netbsd.org/reallocarr.3) in NetBSD.
int   mi_reallocarr(void* p, size_t count, size_t size);

void* mi_aligned_recalloc(void* p, size_t newcount, size_t size, size_t alignment);
void* mi_aligned_offset_recalloc(void* p, size_t newcount, size_t size, size_t alignment, size_t offset);

void mi_free_size(void* p, size_t size);
void mi_free_size_aligned(void* p, size_t size, size_t alignment);
void mi_free_aligned(void* p, size_t alignment);

/// \}

/// \defgroup cpp C++ wrappers
///
///  `mi_` prefixed implementations of various allocation functions
///  that use C++ semantics on out-of-memory, generally calling
///  `std::get_new_handler` and raising a `std::bad_alloc` exception on failure.
///
///  Note: use the `mimalloc-new-delete.h` header to override the \a new
///        and \a delete operators globally. The wrappers here are mostly
///        for convenience for library writers that need to interface with
///        mimalloc from C++.
///
/// \{

/// like mi_malloc(), but when out of memory, use `std::get_new_handler` and raise `std::bad_alloc` exception on failure.
void* mi_new(std::size_t n) noexcept(false);

/// like mi_mallocn(), but when out of memory, use `std::get_new_handler` and raise `std::bad_alloc` exception on failure.
void* mi_new_n(size_t count, size_t size) noexcept(false);

/// like mi_malloc_aligned(), but when out of memory, use `std::get_new_handler` and raise `std::bad_alloc` exception on failure.
void* mi_new_aligned(std::size_t n, std::align_val_t alignment) noexcept(false);

/// like `mi_malloc`, but when out of memory, use `std::get_new_handler` but return \a NULL on failure.
void* mi_new_nothrow(size_t n);

/// like `mi_malloc_aligned`, but when out of memory, use `std::get_new_handler` but return \a NULL on failure.
void* mi_new_aligned_nothrow(size_t n, size_t alignment);

/// like mi_realloc(), but when out of memory, use `std::get_new_handler` and raise `std::bad_alloc` exception on failure.
void* mi_new_realloc(void* p, size_t newsize);

/// like mi_reallocn(), but when out of memory, use `std::get_new_handler` and raise `std::bad_alloc` exception on failure.
void* mi_new_reallocn(void* p, size_t newcount, size_t size);

/// \a std::allocator implementation for mimalloc for use in STL containers.
/// For example:
/// ```
/// std::vector<int, mi_stl_allocator<int> > vec;
/// vec.push_back(1);
/// vec.pop_back();
/// ```
template<class T> struct mi_stl_allocator { }

/// \}

/*! \page build Building

Checkout the sources from GitHub:
```
git clone https://github.com/microsoft/mimalloc
```

## Windows

Open `ide/vs2019/mimalloc.sln` in Visual Studio 2019 and build (or `ide/vs2017/mimalloc.sln`).
The `mimalloc` project builds a static library (in `out/msvc-x64`), while the
`mimalloc-override` project builds a DLL for overriding malloc
in the entire program.

## macOS, Linux, BSD, etc.

We use [`cmake`](https://cmake.org)<sup>1</sup> as the build system:

```
> mkdir -p out/release
> cd out/release
> cmake ../..
> make
```
This builds the library as a shared (dynamic)
library (`.so` or `.dylib`), a static library (`.a`), and
as a single object file (`.o`).

`> sudo make install` (install the library and header files in `/usr/local/lib`  and `/usr/local/include`)

You can build the debug version which does many internal checks and
maintains detailed statistics as:

```
> mkdir -p out/debug
> cd out/debug
> cmake -DCMAKE_BUILD_TYPE=Debug ../..
> make
```
This will name the shared library as `libmimalloc-debug.so`.

Finally, you can build a _secure_ version that uses guard pages, encrypted
free lists, etc, as:
```
> mkdir -p out/secure
> cd out/secure
> cmake -DMI_SECURE=ON ../..
> make
```
This will name the shared library as `libmimalloc-secure.so`.
Use `ccmake`<sup>2</sup> instead of `cmake`
to see and customize all the available build options.

Notes:
1. Install CMake: `sudo apt-get install cmake`
2. Install CCMake: `sudo apt-get install cmake-curses-gui`

*/

/*! \page using Using the library

### Build

The preferred usage is including `<mimalloc.h>`, linking with
the shared- or static library, and using the `mi_malloc` API exclusively for allocation. For example,
```
gcc -o myprogram -lmimalloc myfile.c
```

mimalloc uses only safe OS calls (`mmap` and `VirtualAlloc`) and can co-exist
with other allocators linked to the same program.
If you use `cmake`, you can simply use:
```
find_package(mimalloc 2.1 REQUIRED)
```
in your `CMakeLists.txt` to find a locally installed mimalloc. Then use either:
```
target_link_libraries(myapp PUBLIC mimalloc)
```
to link with the shared (dynamic) library, or:
```
target_link_libraries(myapp PUBLIC mimalloc-static)
```
to link with the static library. See `test\CMakeLists.txt` for an example.

### C++
For best performance in C++ programs, it is also recommended to override the
global `new` and `delete` operators. For convenience, mimalloc provides
[`mimalloc-new-delete.h`](https://github.com/microsoft/mimalloc/blob/master/include/mimalloc-new-delete.h) which does this for you -- just include it in a single(!) source file in your project.

In C++, mimalloc also provides the `mi_stl_allocator` struct which implements the `std::allocator`
interface. For example:
```
std::vector<some_struct, mi_stl_allocator<some_struct>> vec;
vec.push_back(some_struct());
```

### Statistics

You can pass environment variables to print verbose messages (`MIMALLOC_VERBOSE=1`)
and statistics (`MIMALLOC_SHOW_STATS=1`) (in the debug version):
```
> env MIMALLOC_SHOW_STATS=1 ./cfrac 175451865205073170563711388363

175451865205073170563711388363 = 374456281610909315237213 * 468551

heap stats:     peak      total      freed       unit
normal   2:    16.4 kb    17.5 mb    17.5 mb      16 b   ok
normal   3:    16.3 kb    15.2 mb    15.2 mb      24 b   ok
normal   4:      64 b      4.6 kb     4.6 kb      32 b   ok
normal   5:      80 b    118.4 kb   118.4 kb      40 b   ok
normal   6:      48 b       48 b       48 b       48 b   ok
normal  17:     960 b      960 b      960 b      320 b   ok

heap stats:     peak      total      freed       unit
    normal:    33.9 kb    32.8 mb    32.8 mb       1 b   ok
      huge:       0 b        0 b        0 b        1 b   ok
     total:    33.9 kb    32.8 mb    32.8 mb       1 b   ok
malloc requested:         32.8 mb

 committed:    58.2 kb    58.2 kb    58.2 kb       1 b   ok
  reserved:     2.0 mb     2.0 mb     2.0 mb       1 b   ok
     reset:       0 b        0 b        0 b        1 b   ok
  segments:       1          1          1
-abandoned:       0
     pages:       6          6          6
-abandoned:       0
     mmaps:       3
 mmap fast:       0
 mmap slow:       1
   threads:       0
   elapsed:     2.022s
   process: user: 1.781s, system: 0.016s, faults: 756, reclaims: 0, rss: 2.7 mb
```

The above model of using the `mi_` prefixed API is not always possible
though in existing programs that already use the standard malloc interface,
and another option is to override the standard malloc interface
completely and redirect all calls to the _mimalloc_ library instead.

See \ref overrides for more info.

*/

/*! \page environment Environment Options

You can set further options either programmatically (using [`mi_option_set`](https://microsoft.github.io/mimalloc/group__options.html)), or via environment variables:

- `MIMALLOC_SHOW_STATS=1`: show statistics when the program terminates.
- `MIMALLOC_VERBOSE=1`: show verbose messages.
- `MIMALLOC_SHOW_ERRORS=1`: show error and warning messages.

Advanced options:

- `MIMALLOC_ARENA_EAGER_COMMIT=2`: turns on eager commit for the large arenas (usually 1GiB) from which mimalloc
   allocates segments and pages. Set this to 2 (default) to
   only enable this on overcommit systems (e.g. Linux). Set this to 1 to enable explicitly on other systems
   as well (like Windows or macOS) which may improve performance (as the whole arena is committed at once).
   Note that eager commit only increases the commit but not the actual the peak resident set
   (rss) so it is generally ok to enable this.
- `MIMALLOC_PURGE_DELAY=N`: the delay in `N` milli-seconds (by default `10`) after which mimalloc will purge
   OS pages that are not in use. This signals to the OS that the underlying physical memory can be reused which
   can reduce memory fragmentation especially in long running (server) programs. Setting `N` to `0` purges immediately when
   a page becomes unused which can improve memory usage but also decreases performance. Setting `N` to a higher
   value like `100` can improve performance (sometimes by a lot) at the cost of potentially using more memory at times.
   Setting it to `-1` disables purging completely.
- `MIMALLOC_PURGE_DECOMMITS=1`: By default "purging" memory means unused memory is decommitted (`MEM_DECOMMIT` on Windows,
   `MADV_DONTNEED` (which decresease rss immediately) on `mmap` systems). Set this to 0 to instead "reset" unused
   memory on a purge (`MEM_RESET` on Windows, generally `MADV_FREE` (which does not decrease rss immediately) on `mmap` systems).
   Mimalloc generally does not "free" OS memory but only "purges" OS memory, in other words, it tries to keep virtual
   address ranges and decommits within those ranges (to make the underlying physical memory available to other processes).

Further options for large workloads and services:

- `MIMALLOC_USE_NUMA_NODES=N`: pretend there are at most `N` NUMA nodes. If not set, the actual NUMA nodes are detected
   at runtime. Setting `N` to 1 may avoid problems in some virtual environments. Also, setting it to a lower number than
   the actual NUMA nodes is fine and will only cause threads to potentially allocate more memory across actual NUMA
   nodes (but this can happen in any case as NUMA local allocation is always a best effort but not guaranteed).
- `MIMALLOC_ALLOW_LARGE_OS_PAGES=1`: use large OS pages (2 or 4MiB) when available; for some workloads this can significantly
   improve performance. When this option is disabled (default), it also disables transparent huge pages (THP) for the process
   (on Linux and Android). On Linux the default setting is 2 -- this enables the use of large pages through THP only.
   Use `MIMALLOC_VERBOSE` to check if the large OS pages are enabled -- usually one needs
   to explicitly give permissions for large OS pages (as on [Windows][windows-huge] and [Linux][linux-huge]). However, sometimes
   the OS is very slow to reserve contiguous physical memory for large OS pages so use with care on systems that
   can have fragmented memory (for that reason, we generally recommend to use `MIMALLOC_RESERVE_HUGE_OS_PAGES` instead whenever possible).
- `MIMALLOC_RESERVE_HUGE_OS_PAGES=N`: where `N` is the number of 1GiB _huge_ OS pages. This reserves the huge pages at
   startup and sometimes this can give a large (latency) performance improvement on big workloads.
   Usually it is better to not use `MIMALLOC_ALLOW_LARGE_OS_PAGES=1` in combination with this setting. Just like large
   OS pages, use with care as reserving
   contiguous physical memory can take a long time when memory is fragmented (but reserving the huge pages is done at
   startup only once).
   Note that we usually need to explicitly give permission for huge OS pages (as on [Windows][windows-huge] and [Linux][linux-huge])).
   With huge OS pages, it may be beneficial to set the setting
   `MIMALLOC_EAGER_COMMIT_DELAY=N` (`N` is 1 by default) to delay the initial `N` segments (of 4MiB)
   of a thread to not allocate in the huge OS pages; this prevents threads that are short lived
   and allocate just a little to take up space in the huge OS page area (which cannot be purged as huge OS pages are pinned
   to physical memory).
   The huge pages are usually allocated evenly among NUMA nodes.
   We can use `MIMALLOC_RESERVE_HUGE_OS_PAGES_AT=N` where `N` is the numa node (starting at 0) to allocate all
   the huge pages at a specific numa node instead.

Use caution when using `fork` in combination with either large or huge OS pages: on a fork, the OS uses copy-on-write
for all pages in the original process including the huge OS pages. When any memory is now written in that area, the
OS will copy the entire 1GiB huge page (or 2MiB large page) which can cause the memory usage to grow in large increments.

[linux-huge]: https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/5/html/tuning_and_optimizing_red_hat_enterprise_linux_for_oracle_9i_and_10g_databases/sect-oracle_9i_and_10g_tuning_guide-large_memory_optimization_big_pages_and_huge_pages-configuring_huge_pages_in_red_hat_enterprise_linux_4_or_5
[windows-huge]: https://docs.microsoft.com/en-us/sql/database-engine/configure-windows/enable-the-lock-pages-in-memory-option-windows?view=sql-server-2017

*/

/*! \page overrides Overriding Malloc

Overriding the standard `malloc` (and `new`) can be done either _dynamically_ or _statically_.

## Dynamic override

This is the recommended way to override the standard malloc interface.

### Dynamic Override on Linux, BSD

On these ELF-based systems we preload the mimalloc shared
library so all calls to the standard `malloc` interface are
resolved to the _mimalloc_ library.
```
> env LD_PRELOAD=/usr/lib/libmimalloc.so myprogram
```

You can set extra environment variables to check that mimalloc is running,
like:
```
> env MIMALLOC_VERBOSE=1 LD_PRELOAD=/usr/lib/libmimalloc.so myprogram
```
or run with the debug version to get detailed statistics:
```
> env MIMALLOC_SHOW_STATS=1 LD_PRELOAD=/usr/lib/libmimalloc-debug.so myprogram
```

### Dynamic Override on MacOS

On macOS we can also preload the mimalloc shared
library so all calls to the standard `malloc` interface are
resolved to the _mimalloc_ library.
```
> env DYLD_INSERT_LIBRARIES=/usr/lib/libmimalloc.dylib myprogram
```

Note that certain security restrictions may apply when doing this from
the [shell](https://stackoverflow.com/questions/43941322/dyld-insert-libraries-ignored-when-calling-application-through-bash).


### Dynamic Override on Windows

<span id="override_on_windows">Dynamically overriding on mimalloc on Windows</span> 
is robust and has the particular advantage to be able to redirect all malloc/free calls 
that go through the (dynamic) C runtime allocator, including those from other DLL's or 
libraries. As it intercepts all allocation calls on a low level, it can be used reliably 
on large programs that include other 3rd party components.
There are four requirements to make the overriding work well:

1. Use the C-runtime library as a DLL (using the `/MD` or `/MDd` switch).

2. Link your program explicitly with the `mimalloc.lib` export library for the `mimalloc.dll`.
   (which must be compiled with `-DMI_OVERRIDE=ON`, which is the default though).
   To ensure the `mimalloc.dll` is actually loaded at run-time it is easiest 
   to insert some call to the mimalloc API in the `main` function, like `mi_version()`
   (or use the `/include:mi_version` switch on the linker command, or
   similarly, `#pragma comment(linker, "/include:mi_version")` in some source file). 
   See the `mimalloc-test-override` project for an example on how to use this. 

3. The `mimalloc-redirect.dll` must be put in the same directory as the main 
   `mimalloc.dll` at runtime (as it is a dependency of that DLL).
   The redirection DLL ensures that all calls to the C runtime malloc API get 
   redirected to mimalloc functions (which reside in `mimalloc.dll`).

4. Ensure the `mimalloc.dll` comes as early as possible in the import
   list of the final executable (so it can intercept all potential allocations).
   You can use `minject -l <exe>` to check this if needed.

For best performance on Windows with C++, it
is also recommended to also override the `new`/`delete` operations (by including
[`mimalloc-new-delete.h`](include/mimalloc-new-delete.h)
a single(!) source file in your project).

The environment variable `MIMALLOC_DISABLE_REDIRECT=1` can be used to disable dynamic
overriding at run-time. Use `MIMALLOC_VERBOSE=1` to check if mimalloc was successfully 
redirected.

For different platforms than x64, you may need a specific [redirection dll](bin).
Furthermore, we cannot always re-link an executable or ensure `mimalloc.dll` comes
first in the import table. In such cases the [`minject`](bin) tool can be used
to patch the executable's import tables.


## Static override

On Unix-like systems, you can also statically link with _mimalloc_ to override the standard
malloc interface. The recommended way is to link the final program with the
_mimalloc_ single object file (`mimalloc.o`). We use
an object file instead of a library file as linkers give preference to
that over archives to resolve symbols. To ensure that the standard
malloc interface resolves to the _mimalloc_ library, link it as the first
object file. For example:
```
> gcc -o myprogram mimalloc.o  myfile1.c ...
```

Another way to override statically that works on all platforms, is to
link statically to mimalloc (as shown in the introduction) and include a
header file in each source file that re-defines `malloc` etc. to `mi_malloc`.
This is provided by [`mimalloc-override.h`](https://github.com/microsoft/mimalloc/blob/master/include/mimalloc-override.h). This only works reliably though if all sources are
under your control or otherwise mixing of pointers from different heaps may occur!

## List of Overrides:

The specific functions that get redirected to the _mimalloc_ library are:

```
// C
void*  malloc(size_t size);
void*  calloc(size_t size, size_t n);
void*  realloc(void* p, size_t newsize);
void   free(void* p);

void*  aligned_alloc(size_t alignment, size_t size);
char*  strdup(const char* s);
char*  strndup(const char* s, size_t n);
char*  realpath(const char* fname, char* resolved_name);


// C++
void   operator delete(void* p);
void   operator delete[](void* p);

void*  operator new(std::size_t n) noexcept(false);
void*  operator new[](std::size_t n) noexcept(false);
void*  operator new( std::size_t n, std::align_val_t align) noexcept(false);
void*  operator new[]( std::size_t n, std::align_val_t align) noexcept(false);

void*  operator new  ( std::size_t count, const std::nothrow_t& tag);
void*  operator new[]( std::size_t count, const std::nothrow_t& tag);
void*  operator new  ( std::size_t count, std::align_val_t al, const std::nothrow_t&);
void*  operator new[]( std::size_t count, std::align_val_t al, const std::nothrow_t&);

// Posix
int    posix_memalign(void** p, size_t alignment, size_t size);

// Linux
void*  memalign(size_t alignment, size_t size);
void*  valloc(size_t size);
void*  pvalloc(size_t size);
size_t malloc_usable_size(void *p);
void*  reallocf(void* p, size_t newsize);

// macOS
void   vfree(void* p);
size_t malloc_size(const void* p);
size_t malloc_good_size(size_t size);

// BSD
void*  reallocarray( void* p, size_t count, size_t size );
void*  reallocf(void* p, size_t newsize);
void   cfree(void* p);

// NetBSD
int    reallocarr(void* p, size_t count, size_t size);

// Windows
void*  _expand(void* p, size_t newsize);
size_t _msize(void* p);

void*  _malloc_dbg(size_t size, int block_type, const char* fname, int line);
void*  _realloc_dbg(void* p, size_t newsize, int block_type, const char* fname, int line);
void*  _calloc_dbg(size_t count, size_t size, int block_type, const char* fname, int line);
void*  _expand_dbg(void* p, size_t size, int block_type, const char* fname, int line);
size_t _msize_dbg(void* p, int block_type);
void   _free_dbg(void* p, int block_type);
```

*/

/*! \page bench Performance

We tested _mimalloc_ against many other top allocators over a wide
range of benchmarks, ranging from various real world programs to
synthetic benchmarks that see how the allocator behaves under more
extreme circumstances.

In our benchmarks, _mimalloc_ always outperforms all other leading
allocators (_jemalloc_, _tcmalloc_, _Hoard_, etc) (Jan 2021),
and usually uses less memory (up to 25% more in the worst case).
A nice property is that it does *consistently* well over the wide
range of benchmarks.

See the [Performance](https://github.com/microsoft/mimalloc#Performance)
section in the _mimalloc_ repository for benchmark results,
or the the technical report for detailed benchmark results.

*/
