/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#error "documentation file only!"


/*! \mainpage

This is the API documentation of the
[mimalloc](https://github.com/koka-lang/mimalloc) allocator
(pronounced "me-malloc") -- a
general purpose allocator with excellent [performance](bench.html)
characteristics. Initially
developed by Daan Leijen for the run-time systems of the
[Koka](https://github.com/koka-lang/koka) and [Lean](https://github.com/leanprover/lean) languages.

Notable aspects of the design include:

- __small and consistent__: the library is less than 3500 LOC using simple and
  consistent data structures. This makes it very suitable
  to integrate and adapt in other projects. For runtime systems it
  provides hooks for a monotonic _heartbeat_ and deferred freeing (for
  bounded worst-case times with reference counting).
- __free list sharding__: "the big idea": instead of one big free list (per size class) we have
  many smaller lists per memory "page" which both reduces fragmentation
  and increases locality --
  things that are allocated close in time get allocated close in memory.
  (A memory "page" in mimalloc contains blocks of one size class and is
  usually 64KB on a 64-bit system).
- __eager page reset__: when a "page" becomes empty (with increased chance
  due to free list sharding) the memory is marked to the OS as unused ("reset" or "purged")
  reducing (real) memory pressure and fragmentation, especially in long running
  programs.
- __lazy initialization__: pages in a segment are lazily initialized so
  no memory is touched until it becomes allocated, reducing the resident
  memory and potential page faults.
- __bounded__: it does not suffer from _blowup_ \[1\], has bounded worst-case allocation
  times (_wcat_), bounded space overhead (~0.2% meta-data, with at most 16.7% waste in allocation sizes),
  and has no internal points of contention using atomic operations almost
  everywhere.

Further information:

- \ref build
- \ref using
- \ref overrides
- \ref bench
- \ref malloc
- \ref extended
- \ref aligned
- \ref heap
- \ref typed
- \ref analysis
- \ref options

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

/// Reallocate memory to \a newsize bytes, with extra memory initialized to zero.
/// @param p Pointer to a previously allocated block (or \a NULL).
/// @param newsize The new required size in bytes.
/// @returns A pointer to a re-allocated block of \a newsize bytes, or \a NULL
/// if out of memory.
///
/// If the \a newsize is larger than the original allocated size of \a p,
/// the extra bytes are initialized to zero.
void* mi_rezalloc(void* p, size_t newsize);

/// Re-allocate memory to \a count elements of \a size bytes, with extra memory initialized to zero.
/// @param p Pointer to a previously allocated block (or \a NULL).
/// @param count The number of elements.
/// @param size The size of each element.
/// @returns A pointer to a re-allocated block of \a count * \a size bytes, or \a NULL
/// if out of memory or if \a count * \a size overflows.
///
/// If there is no overflow, it behaves exactly like `mi_rezalloc(p,count*size)`.
/// @see mi_reallocn()
/// @see mi_rezalloc()
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
/// If there is no overflow, it behaves exactly like `mi_malloc(p,count*size)`.
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
/// @see mi_recalloc()
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
/// allocated size of \a p, and, in the current design,
/// should be less than 16.7% more.
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
void   mi_collect(bool force);

/// Print statistics.
/// @param out Output file. Use \a NULL for \a stderr.
///
/// Most detailed when using a debug build.
void   mi_stats_print(FILE* out);

/// Reset statistics.
void   mi_stats_reset();

/// Initialize mimalloc on a thread.
/// Should not be used as on most systems (pthreads, windows) this is done
/// automatically.
void   mi_thread_init();

/// Uninitialize mimalloc on a thread.
/// Should not be used as on most systems (pthreads, windows) this is done
/// automatically. Ensures that any memory that is not freed yet (but will
/// be freed by other threads in the future) is properly handled.
void   mi_thread_done();

/// Print out heap statistics for this thread.
/// @param out Output file. Use \a NULL for \a stderr.
///
/// Most detailed when using a debug build.
void   mi_thread_stats_print(FILE* out);

/// Type of deferred free functions.
/// @param force If \a true all outstanding items should be freed.
/// @param heartbeat A monotonically increasing count.
///
/// @see mi_register_deferred_free
typedef void (mi_deferred_free_fun)(bool force, unsigned long long heartbeat);

/// Register a deferred free function.
/// @param deferred_free Address of a deferred free-ing function or \a NULL to unregister.
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
void   mi_register_deferred_free(mi_deferred_free_fun* deferred_free);

/// \}

// ------------------------------------------------------
// Aligned allocation
// ------------------------------------------------------

/// \defgroup aligned Aligned Allocation
///
/// Allocating aligned memory blocks.
///
/// \{

/// Allocate \a size bytes aligned by \a alignment.
/// @param size  number of bytes to allocate.
/// @param alignment  the minimal alignment of the allocated memory.
/// @returns pointer to the allocated memory or \a NULL if out of memory.
/// The returned pointer is aligned by \a alignment, i.e.
/// `(uintptr_t)p % alignment == 0`.
///
/// Returns a unique pointer if called with \a size 0.
/// @see [_aligned_malloc](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc?view=vs-2017) (on Windows)
/// @see [aligned_alloc](http://man.openbsd.org/reallocarray) (on BSD, with switched arguments!)
/// @see [posix_memalign](https://linux.die.net/man/3/posix_memalign) (on Posix, with switched arguments!)
/// @see [memalign](https://linux.die.net/man/3/posix_memalign) (on Linux, with switched arguments!)
void* mi_malloc_aligned(size_t size, size_t alignment);
void* mi_zalloc_aligned(size_t size, size_t alignment);
void* mi_calloc_aligned(size_t count, size_t size, size_t alignment);
void* mi_realloc_aligned(void* p, size_t newsize, size_t alignment);
void* mi_rezalloc_aligned(void* p, size_t newsize, size_t alignment);
void* mi_recalloc_aligned(void* p, size_t count, size_t size, size_t alignment);

/// Allocate \a size bytes aligned by \a alignment at a specified \a offset.
/// @param size  number of bytes to allocate.
/// @param alignment  the minimal alignment of the allocated memory at \a offset.
/// @param offset     the offset that should be aligned.
/// @returns pointer to the allocated memory or \a NULL if out of memory.
/// The returned pointer is aligned by \a alignment at \a offset, i.e.
/// `((uintptr_t)p + offset) % alignment == 0`.
///
/// Returns a unique pointer if called with \a size 0.
/// @see [_aligned_offset_malloc](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-offset-malloc?view=vs-2017) (on Windows)
void* mi_malloc_aligned_at(size_t size, size_t alignment, size_t offset);
void* mi_zalloc_aligned_at(size_t size, size_t alignment, size_t offset);
void* mi_calloc_aligned_at(size_t count, size_t size, size_t alignment, size_t offset);
void* mi_realloc_aligned_at(void* p, size_t newsize, size_t alignment, size_t offset);
void* mi_rezalloc_aligned_at(void* p, size_t newsize, size_t alignment, size_t offset);
void* mi_recalloc_aligned_at(void* p, size_t count, size_t size, size_t alignment, size_t offset);

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
/// still allocated blocks in this heap (efficienty)
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

/// Set the default heap to use for mi_malloc() et al.
/// @param heap  The new default heap.
/// @returns The previous default heap.
mi_heap_t* mi_heap_set_default(mi_heap_t* heap);

/// Get the default heap that is used for mi_malloc() et al.
/// @returns The current default heap.
mi_heap_t* mi_heap_get_default();

/// Get the backing heap.
/// The _backing_ heap is the initial default heap for
/// a thread and always available for allocations.
/// It cannot be destroyed or deleted
/// except by exiting the thread.
mi_heap_t* mi_heap_get_backing();

/// Allocate in a specific heap.
/// @see mi_malloc()
void* mi_heap_malloc(mi_heap_t* heap, size_t size);

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


/// \}

/// \defgroup typed Typed Macros
///
/// Typed allocation macros
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

/// Re-allocate to \a count zero-initialized blocks of type \a tp.
#define mi_recalloc_tp(p,tp,count)  ((tp*)mi_recalloc(p,count,sizeof(tp)))

/// Allocate a block of type \a tp in a heap \a hp.
#define mi_heap_malloc_tp(hp,tp)        ((tp*)mi_malloc(hp,sizeof(tp)))

/// Allocate a zero-initialized block of type \a tp in a heap \a hp.
#define mi_heap_zalloc_tp(hp,tp)        ((tp*)mi_zalloc(hp,sizeof(tp)))

/// Allocate \a count zero-initialized blocks of type \a tp in a heap \a hp.
#define mi_heap_calloc_tp(hp,tp,count)      ((tp*)mi_calloc(hp,count,sizeof(tp)))

/// Allocate \a count blocks of type \a tp in a heap \a hp.
#define mi_heap_mallocn_tp(hp,tp,count)     ((tp*)mi_mallocn(hp,count,sizeof(tp)))

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

/// \}

/// \defgroup options Runtime Options
///
/// Set runtime behavior.
///
/// \{

/// Runtime options.
typedef enum mi_option_e {
  mi_option_page_reset,   ///< Reset page memory when it becomes free.
  mi_option_cache_reset,  ///< Reset segment memory when a segment is cached.
  mi_option_pool_commit,  ///< Commit segments in large pools.
  mi_option_show_stats,   ///< Print statistics to `stderr` when the program is done.
  mi_option_show_errors,  ///< Print error messages to `stderr`.
  mi_option_verbose,      ///< Print verbose messages to `stderr`.
  _mi_option_last
} mi_option_t;

bool  mi_option_enabled(mi_option_t option);
void  mi_option_enable(mi_option_t option, bool enable);
void  mi_option_enable_default(mi_option_t option, bool enable);

long  mi_option_get(mi_option_t option);
void  mi_option_set(mi_option_t option, long value);
void  mi_option_set_default(mi_option_t option, long value);


/// \}

/*! \page build Building

Checkout the sources from Github:
```
git clone https://github.com/koka-lang/mimalloc.git
```

## Windows

Open `ide/vs2017/mimalloc.sln` in Visual Studio 2017 and build.
The `mimalloc` project builds a static library, while the
`mimalloc-override` project builds a DLL for overriding malloc
in the entire program.

## MacOSX, Linux, BSD, etc.

We use [`cmake`](https://cmake.org)<sup>1</sup> as the build system:

- `mkdir -p out/release` (create a build directory)
- `cd out/release` (go to it)
- `cmake ../..` (generate the make file)
- `make` (and build)

  This will build the library as a shared (dynamic)
  library (`.so` or `.dylib`), a static library (`.a`), and
  as a single object file (`.o`).

- `sudo make install` (install the library and header files in `/usr/lib` and `/usr/include`)

  Use the option `-DCMAKE_INSTALL_PREFIX=../local` (for example) to the `ccmake`
  command to install to a local directory to see what gets installed.

You can build the debug version which does many internal checks and
maintains detailed statistics as:

-  `mkdir -p out/debug`
-  `cd out/debug`
-  `cmake -DCMAKE_BUILD_TYPE=Debug ../..`
-  `make`

   This will name the shared library as `libmimalloc-debug.so`.

Or build with `clang`:

- `CC=clang cmake ../..`

Use `ccmake`<sup>2</sup> instead of `cmake`
to see and customize all the available build options.

Notes:
1. Install CMake: `sudo apt-get install cmake`
2. Install CCMake: `sudo apt-get install cmake-curses-gui`
*/

/*! \page using Using the library

The preferred usage is including `<mimalloc.h>`, linking with
the shared- or static library, and using the `mi_malloc` API exclusively for allocation. For example,
```
gcc -o myprogram -lmimalloc myfile.c
```

mimalloc uses only safe OS calls (`mmap` and `VirtualAlloc`) and can co-exist
with other allocators linked to the same program.
If you use `cmake`, you can simply use:
```
find_package(mimalloc 1.0 REQUIRED)
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


You can pass environment variables to print verbose messages (`MIMALLOC_VERBOSE=1`)
and statistics (`MIMALLOC_STATS=1`) (in the debug version):
```
> env MIMALLOC_STATS=1 ./cfrac 175451865205073170563711388363

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

/*! \page overrides Overriding Malloc

Overriding standard malloc can be done either _dynamically_ or _statically_.

## Dynamic override

This is the recommended way to override the standard malloc interface.

### Unix, BSD, MacOSX

On these system we preload the mimalloc shared
library so all calls to the standard `malloc` interface are
resolved to the _mimalloc_ library.

- `env LD_PRELOAD=/usr/lib/libmimalloc.so myprogram` (on Linux, BSD, etc.)
- `env DYLD_INSERT_LIBRARIES=usr/lib/libmimalloc.dylib myprogram` (On MacOSX)

  Note certain security restrictions may apply when doing this from
  the [shell](https://stackoverflow.com/questions/43941322/dyld-insert-libraries-ignored-when-calling-application-through-bash).

You can set extra environment variables to check that mimalloc is running,
like:
```
env MIMALLOC_VERBOSE=1 LD_PRELOAD=/usr/lib/libmimalloc.so myprogram
```
or run with the debug version to get detailed statistics:
```
env MIMALLOC_STATS=1 LD_PRELOAD=/usr/lib/libmimallocd.so myprogram
```

### Windows

On Windows you need to link your program explicitly with the mimalloc
DLL, and use the C-runtime library as a DLL (the `/MD` or `/MDd` switch).
To ensure the mimalloc DLL gets loaded it is easiest to insert some
call to the mimalloc API in the `main` function, like `mi_version()`.

Due to the way mimalloc overrides the standard malloc at runtime, it is best
to link to the mimalloc import library first on the command line so it gets
loaded right after the universal C runtime DLL (`ucrtbase`). See
the `mimalloc-override-test` project for an example.


## Static override

You can also statically link with _mimalloc_ to override the standard
malloc interface. The recommended way is to link the final program with the
_mimalloc_ single object file (`mimalloc-override.o` (or `.obj`)). We use
an object file instead of a library file as linkers give preference to
that over archives to resolve symbols. To ensure that the standard
malloc interface resolves to the _mimalloc_ library, link it as the first
object file. For example:

```
gcc -o myprogram mimalloc-override.o  myfile1.c ...
```



## List of Overrides:

The specific functions that get redirected to the _mimalloc_ library are:

```
// C
void*  malloc(size_t size);
void*  calloc(size_t size, size_t n);
void*  realloc(void* p, size_t newsize);
void   free(void* p);

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
void*  aligned_alloc(size_t alignment, size_t size);
void*  valloc(size_t size);
void*  pvalloc(size_t size);
size_t malloc_usable_size(void *p);

// BSD
void*  reallocarray( void* p, size_t count, size_t size );
void*  reallocf(void* p, size_t newsize);
void   cfree(void* p);

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


tldr: In our benchmarks, mimalloc always outperforms
all other leading allocators (jemalloc, tcmalloc, hoard, and glibc), and usually
uses less memory (with less then 25% more in the worst case) (as of Jan 2019).
A nice property is that it does consistently well over a wide range of benchmarks.

Disclaimer: allocators are interesting as there is no optimal algorithm -- for
a given allocator one can always construct a workload where it does not do so well.
The goal is thus to find an allocation strategy that performs well over a wide
range of benchmarks without suffering from underperformance in less
common situations (which is what our second benchmark set tests for).


## Benchmarking

We tested _mimalloc_ with 5 other allocators over 11 benchmarks.
The tested allocators are:

- **mi**: The mimalloc allocator (version tag `v1.0.0`).
- **je**: [jemalloc](https://github.com/jemalloc/jemalloc), by [Jason Evans](https://www.facebook.com/notes/facebook-engineering/scalable-memory-allocation-using-jemalloc/480222803919) (Facebook);
  currently (2018) one of the leading allocators and is widely used, for example
  in BSD, Firefox, and at Facebook. Installed as package `libjemalloc-dev:amd64/bionic 3.6.0-11`.
- **tc**: [tcmalloc](https://github.com/gperftools/gperftools), by Google as part of the performance tools.
  Highly performant and used in the Chrome browser. Installed as package `libgoogle-perftools-dev:amd64/bionic 2.5-2.2ubuntu3`.
- **jx**: A compiled version of a more recent instance of [jemalloc](https://github.com/jemalloc/jemalloc).
      Using commit ` 7a815c1b` ([dev](https://github.com/jemalloc/jemalloc/tree/dev), 2019-01-15).
- **hd**: [Hoard](https://github.com/emeryberger/Hoard), by Emery Berger \[1].
      One of the first multi-thread scalable allocators.
      ([master](https://github.com/emeryberger/Hoard), 2019-01-01, version tag `3.13`)
- **mc**: The system allocator. Here we use the LibC allocator (which is originally based on
      PtMalloc). Using version 2.27. (Note that version 2.26 significantly improved scalability over
      earlier versions).

All allocators run exactly the same benchmark programs and use `LD_PRELOAD` to override the system allocator.
The wall-clock elapsed time and peak resident memory (_rss_) are
measured with the `time` program. The best scores over 5 runs are used.
Performance is reported relative to mimalloc, e.g. a time of 66% means that
mimalloc ran 1.5&times; faster (i.e. that mimalloc finished in 66% of the time
that the other allocator needed).

## On a 16-core AMD EPYC running Linux

Testing on a big Amazon EC2 instance ([r5a.4xlarge](https://aws.amazon.com/ec2/instance-types/))
consisting of a 16-core AMD EPYC 7000 at 2.5GHz
with 128GB ECC memory, running	Ubuntu 18.04.1 with LibC 2.27 and GCC 7.3.0.


The first benchmark set consists of programs that allocate a lot. Relative
elapsed time:

![bench-r5a-4xlarge-t1](bench-r5a-4xlarge-t1.png)

and memory usage:

![bench-r5a-4xlarge-m1](bench-r5a-4xlarge-m1.png)

The benchmarks above are (with N=16 in our case):

- __cfrac__: by Dave Barrett, implementation of continued fraction factorization:
  uses many small short-lived allocations. Factorizes as `./cfrac 175451865205073170563711388363274837927895`.
- __espresso__: a programmable logic array analyzer \[3].
- __barnes__: a hierarchical n-body particle solver \[4]. Simulates 163840 particles.
- __leanN__: by Leonardo de Moura _et al_, the [lean](https://github.com/leanprover/lean)
  compiler, version 3.4.1, compiling its own standard library concurrently using N cores (`./lean --make -j N`).
  Big real-world workload with intensive allocation, takes about 1:40s when running on a
  single high-end core.
- __redis__: running the [redis](https://redis.io/) 5.0.3 server on
  1 million requests pushing 10 new list elements and then requesting the
  head 10 elements. Measures the requests handled per second.
- __alloc-test__: a modern [allocator test](http://ithare.com/testing-memory-allocators-ptmalloc2-tcmalloc-hoard-jemalloc-while-trying-to-simulate-real-world-loads/)
  developed by by OLogN Technologies AG at [ITHare.com](http://ithare.com). Simulates intensive allocation workloads with a Pareto
  size distribution. The `alloc-testN` benchmark runs on N cores doing 100&times;10<sup>6</sup>
  allocations per thread with objects up to 1KB in size.
  Using commit `94f6cb` ([master](https://github.com/node-dot-cpp/alloc-test), 2018-07-04)

We can see mimalloc outperforms the other allocators moderately but all
these modern allocators perform well.
In `cfrac`, mimalloc is about 13%
faster than jemalloc for many small and short-lived allocations.
The `cfrac` and `espresso` programs do not use much
memory (~1.5MB) so it does not matter too much, but still mimalloc uses  about half the resident
memory of tcmalloc (and almost 5&times; less than Hoard on `espresso`).

_The `leanN` program is most interesting as a large realistic and concurrent
workload and there is a 6% speedup over both tcmalloc and jemalloc. This is
quite significant: if Lean spends (optimistically) 20% of its time in the allocator
that means that mimalloc is 1.5&times; faster than the others._

The `alloc-test` is very allocation intensive and we see the larger
diffrerences here. Since all allocators perform almost identical on `alloc-test1`
as `alloc-testN`, we can see that they are all excellent and scale (almost) linearly.

The second benchmark set test specific aspects of the allocators and
shows more extreme differences between allocators:

![bench-r5a-4xlarge-t2](bench-r5a-4xlarge-t2.png)

&nbsp;

![bench-r5a-4xlarge-m2](bench-r5a-4xlarge-m2.png)

The benchmarks in the second set are (again with N=16):

- __larson__: by Larson and Krishnan \[2]. Simulates a server workload using 100
   separate threads where
   they allocate and free many objects but leave some objects to
   be freed by other threads. Larson and Krishnan observe this behavior
   (which they call _bleeding_) in actual server applications, and the
   benchmark simulates this.
- __sh6bench__: by [MicroQuill](http://www.microquill.com) as part of SmartHeap. Stress test for
   single-threaded allocation where some of the objects are freed
   in a usual last-allocated, first-freed (LIFO) order, but others
   are freed in reverse order. Using the public [source](http://www.microquill.com/smartheap/shbench/bench.zip) (retrieved 2019-01-02)
- __sh8bench__: by [MicroQuill](http://www.microquill.com) as part of SmartHeap. Stress test for
  multithreaded allocation (with N threads) where, just as in `larson`, some objects are freed
  by other threads, and some objects freed in reverse (as in `sh6bench`).
  Using the public [source](http://www.microquill.com/smartheap/SH8BENCH.zip) (retrieved 2019-01-02)
- __cache-scratch__: by Emery Berger _et al_ \[1]. Introduced with the Hoard
  allocator to test for _passive-false_ sharing of cache lines: first some
  small objects are allocated and given to each thread; the threads free that
  object and allocate another one and access that repeatedly. If an allocator
  allocates objects from different threads close to each other this will
  lead to cache-line contention.

In the `larson` server workload mimalloc is 2.5&times; faster than
tcmalloc and jemalloc which is quite surprising -- probably due to the object
migration between different threads. Also in `sh6bench` mimalloc does much
better than the others (more than 4&times; faster than jemalloc). a
We cannot explain this well but believe it may be
caused in part by the "reverse" free-ing in `sh6bench`. Again in `sh8bench`
the mimalloc allocator handles object migration between threads much better .

The `cache-scratch` benchmark also demonstrates the different architectures
of the allocators nicely. With a single thread they all perform the same, but when
running with multiple threads the allocator induced false sharing of the
cache lines causes large run-time differences, where mimalloc is up to
20&times; faster than tcmalloc here. Only the original jemalloc does almost
as well (but the most recent version, jxmalloc, regresses). The
Hoard allocator is specifically designed to avoid this false sharing and we
are not sure why it is not doing well here (although it runs still 5&times; as
fast as tcmalloc and jxmalloc).


## On a 8-core Intel Xeon running Linux

Testing on a compute optimized Amazon EC2 instance ([c5d.2xlarge](https://aws.amazon.com/ec2/instance-types/))
consisting of a 8-core Intel Xeon Platinum at 3GHz (up to 3.5GHz turbo boost)
with 16GB ECC memory, running	Ubuntu 18.04.1 with LibC 2.27 and GCC 7.3.0.

First the regular workload benchmarks (with N=8):

![bench-c5d-2xlarge-t1](bench-c5d-2xlarge-t1.png)

&nbsp;

![bench-c5d-2xlarge-m1](bench-c5d-2xlarge-m1.png)

Most results are quite similar to the 16-core AMD machine except the
the differences are less pronounced with all a bit closer to mimalloc performance.

This is shown too in the second set of benchmarks:

![bench-c5d-2xlarge-t2](bench-c5d-2xlarge-t2.png)

&nbsp;

![bench-c5d-2xlarge-m2](bench-c5d-2xlarge-m2.png)

On the server workload of `larson` everyone does a bit better on the 8-cores
than on 16. On the other benchmarks the performance does not improve though.


## On Windows (4-core Intel Xeon)

Testing on a HP Z4 G4 Workstation with a 4-core Intel® Xeon® W2123 at 3.6 GHz
with 16GB ECC memory, running	Windows 10 Pro (version	10.0.17134 Build 17134)
with Visual Studio 2017 (version 15.8.9).

Since we cannot use `LD_PRELOAD` on Windows we compiled a subset of our
allocators and benchmarks and linked them statically. The **je** benchmark
is therefore equivalent to the **jx** benchmark in the previous graphs.
The **mc** allocator now refers to the standard Microsoft allocator.
Unfortunately we could not get Hoard to work on Windows at this time.

We used the Windows call `QueryPerformanceCounter` to measure elapsed wall-clock
times, and `GetProcessMemoryInfo` to measure the peak working set (rss).

First the regular workload benchmarks:

![bench-z4-win-t1](bench-z4-win-t1.png)

&nbsp;

![bench-z4-win-m1](bench-z4-win-m1.png)

Here mimalloc and tcmalloc perform very similar, and outperform the system
allocator by a significant margin. Somehow jemalloc does much worse than
running on Linux. It it not clear why yet, but it might be a compilation issue:
when running through the profiler the `__chkstk` routine takes
quite some time. This is a compiler inserted runtime function to check for enough
stack space if there are many local variables or when the compiler cannot make
a static estimate. Perhaps this is the culprit but it needs more investigation.

The second set of benchmarks shows again more pronounced differences:

![bench-z4-win-t2](bench-z4-win-t2.png)

&nbsp;

![bench-z4-win-m2](bench-z4-win-m2.png)

In the `larson` server workload mimalloc is 25% faster than
tcmalloc, and both significantly outperform the system allocator.
(again probably due to the object
migration between different threads).
Also in `sh6bench` and `sh8bench`, mimalloc scales much
better than the others.

## References

- \[1] Emery D. Berger, Kathryn S. McKinley, Robert D. Blumofe, and Paul R. Wilson.
   _Hoard: A Scalable Memory Allocator for Multithreaded Applications_
   the Ninth International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS-IX). Cambridge, MA, November 2000.
   [pdf](http://www.cs.utexas.edu/users/mckinley/papers/asplos-2000.pdf)

- \[2] P. Larson and M. Krishnan. _Memory allocation for long-running server applications_. In ISMM, Vancouver, B.C., Canada, 1998.
      [pdf](http://citeseemi.ist.psu.edu/viewdoc/download;jsessionid=5F0BFB4F57832AEB6C11BF8257271088?doi=10.1.1.45.1947&rep=rep1&type=pdf)

- \[3] D. Grunwald, B. Zorn, and R. Henderson.
  _Improving the cache locality of memory allocation_. In R. Cartwright, editor,
  Proceedings of the Conference on Programming Language Design and Implementation, pages 177–186, New York, NY, USA, June 1993.
  [pdf](http://citeseemi.ist.psu.edu/viewdoc/download?doi=10.1.1.43.6621&rep=rep1&type=pdf)

- \[4] J. Barnes and P. Hut. _A hierarchical O(n*log(n)) force-calculation algorithm_. Nature, 324:446-449, 1986.

*/
