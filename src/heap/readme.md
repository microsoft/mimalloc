# Heaps and Initialization

- [`heap.c`](heap.c): First-class heap creation, destruction, ownership, and management.
- [`theap.c`](theap.c): Thread-local heap instances and page collection.
- [`init.c`](init.c): Process and thread initialization, shutdown, and initial allocator state.
- [`subproc.c`](subproc.c): Subprocess allocation domains, metadata allocation, and subprocess lifecycle management.
- [`page-map.c`](page-map.c): Address-to-page mapping to safely map any pointer
to a valid mimalloc page (or `NULL`).

## Explicit thread-local cleanup

`mi_theap_abandon(theap)` releases empty pages and abandons pages with live blocks
to the same heap. It preserves the theap, its configuration, and its TLS references,
so its owner can resume allocating or terminate normally. The caller must keep
the theap and heap alive and exclude concurrent teardown. The owner must be the
calling thread or quiescent outside mimalloc for the entire call. List locks do
not establish that quiescence. This operation does not invoke the deferred-free
callback.

`mi_theap_delete(theap)` is an owning-thread operation. It abandons pages, clears
that thread's per-heap TLS entry and cache, and removes the theap from both lists.
Deleting the default switches the default and thread-exit callback to the thread's
main theap. The main theap itself is only abandoned: it remains as the backing
state for the living thread and its other theaps. A later `mi_heap_theap(heap)`
creates a new instance for a deleted non-main theap. Live blocks remain valid in
their original heap.

Owner deletion synchronizes with heap deletion through the tld and heap list
locks; it checks list membership before accessing the supplied theap. If heap
deletion has already detached it, only the owner's default/cache references need
clearing. This does not make allocation concurrent with heap destruction safe.
Cross-thread abandonment, unlike owner deletion, requires excluding heap deletion.
After a fork, abandonment also accepts a retained theap already invalidated by the
child handler; it does nothing. Neither operation frees live allocations, and
both accept `NULL`.
