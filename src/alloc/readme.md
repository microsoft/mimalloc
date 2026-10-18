# Allocation and Freeing

- [`alloc.c`](alloc.c): Main allocation and reallocation functions, including the allocation fast paths.
- [`alloc-generic.c`](alloc-generic.c): Allocation slow path (`mi_malloc_generic`), page selection, deferred-free callbacks.
- [`alloc-aligned.c`](alloc-aligned.c): Aligned allocation and reallocation, including allocations with an alignment offset.
- [`alloc-posix.c`](alloc-posix.c): POSIX, Unix, and Microsoft-compatible allocation interfaces and convenience functions.
- [`alloc-override.c`](alloc-override.c): Standard allocator and C++ allocation overrides, included by `alloc.c`.
- [`free.c`](free.c): Local and cross-thread freeing, pointer validation, and usable-size queries, included by `alloc.c`.
- [`page.c`](page.c): Allocation-page initialization, free-list management, collection, and retirement.
- [`page.h`](page.h): Private declarations for page helpers used by generic allocation.
- [`page-queue.c`](page-queue.c): Size-class mapping and management of page queues.
- [`page-queue.h`](page-queue.h): Private page-queue declarations and inline helpers.

Remote frees of small and medium blocks in the process main heap are grouped by
page in a 64-entry, direct-mapped thread-local cache. Each list is published with one
atomic operation. Pending blocks remain counted as used, keeping their pages
alive until publication. The cache is flushed when its pending block sizes reach
256 KiB, during collection and periodic allocation maintenance, and before thread
or process teardown. Fork-child cleanup also flushes the caches of vanished
threads. Idle threads can retain a bounded set of pages until collection or exit;
the byte bound on pending blocks is not a bound on the size of those pages.

Destroyable heaps, subprocess heaps, detached metadata pages, guarded blocks,
large blocks, and frees without initialized thread state use immediate
publication. In particular, bypassing destroyable heaps prevents their
destruction from invalidating another thread's cached pointers. The local free
path and the receiver's free-list format are unchanged.
