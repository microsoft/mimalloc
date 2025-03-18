/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_heap.h"
#include <stdio.h>
#include <vector>
#include "mimalloc_internal.h"
#include "mimalloc_windbg_utils.h"

/*
Command: !mi_dump_current_thread_heap

    - Retrieves the global _mi_heap_default symbol.
    - Reads the mi_heap_t structure from the debuggee memory.
    - Outputs key details of the current thread's heap (thread ID, page count, segment count).
    - Provides a rich link to trigger a detailed dump of the heap.
    - This command is critical for diagnosing per-thread memory allocation behavior.
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_current_thread_heap(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);
    UNREFERENCED_PARAMETER(client);

    ULONG64 heapAddress = 0;
    HRESULT hr = GetSymbolOffset("_mi_heap_default", heapAddress);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Failed to locate _mi_heap_default\n");
        return hr;
    }

    mi_heap_t heap = {0};
    hr = ReadMemory(heapAddress, &heap, sizeof(heap));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Failed to read heap structure at 0x%llx\n", heapAddress);
        return hr;
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Heap for thread %u:\n  Page Count: %u, Segment Count: %u\n", heap.thread_id, heap.page_count, heap.segment_count);

    char link[128];
    snprintf(link, sizeof(link), "!mi.DumpHeap 0x%llx", heapAddress);
    PrintLink(link, "Click here to dump detailed heap info", heapAddress);

    return S_OK;
}

/*
Command: !mi_dump_all_thread_heaps

    - Retrieves the _mi_heaps array and the heap count (_mi_heap_count).
    - Iterates over each thread's heap, reading its mi_heap_t structure.
    - Outputs a summary for each heap along with rich links for detailed inspection.
    - Useful for understanding the distribution of memory usage across threads.
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_all_thread_heaps(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);
    UNREFERENCED_PARAMETER(client);

    ULONG64 heapsAddress = 0;
    HRESULT hr = GetSymbolOffset("_mi_heaps", heapsAddress);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Failed to locate _mi_heaps\n");
        return hr;
    }

    ULONG64 heapCountAddress = 0;
    hr = GetSymbolOffset("_mi_heap_count", heapCountAddress);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Failed to locate _mi_heap_count\n");
        return hr;
    }

    size_t heapCount = 0;
    hr = ReadMemory(heapCountAddress, &heapCount, sizeof(heapCount));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Failed to read heap count\n");
        return hr;
    }

    std::vector<mi_heap_t*> heaps(heapCount);
    hr = ReadMemory(heapsAddress, heaps.data(), heapCount * sizeof(mi_heap_t*));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Failed to read heaps array\n");
        return hr;
    }

    for (size_t i = 0; i < heapCount; i++) {
        ULONG64 heapAddr = (ULONG64)heaps[i];
        mi_heap_t heap = {0};
        hr = ReadMemory(heapAddr, &heap, sizeof(heap));
        if (FAILED(hr))
            continue;

        g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\nHeap for thread %u:\n  Page Count: %u, Segment Count: %u\n", heap.thread_id, heap.page_count, heap.segment_count);

        char link[128];
        snprintf(link, sizeof(link), "!mi.DumpHeap 0x%llx", heapAddr);
        PrintLink(link, "Click here for detailed heap info", heapAddr);
    }

    return S_OK;
}

/*
Command: !mi_dump_aggregated_thread_heap_stats

    - Aggregates statistics from all thread heaps.
    - Sums the page count and segment count for all heaps.
    - Outputs an aggregated view that helps identify global memory usage patterns and potential imbalances.
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_aggregated_thread_heap_stats(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);
    UNREFERENCED_PARAMETER(client);

    ULONG64 heapsAddress = 0;
    HRESULT hr = GetSymbolOffset("_mi_heaps", heapsAddress);
    if (FAILED(hr))
        return hr;

    ULONG64 heapCountAddress = 0;
    hr = GetSymbolOffset("_mi_heap_count", heapCountAddress);
    if (FAILED(hr))
        return hr;

    size_t heapCount = 0;
    hr = ReadMemory(heapCountAddress, &heapCount, sizeof(heapCount));
    if (FAILED(hr))
        return hr;

    std::vector<mi_heap_t*> heaps(heapCount);
    hr = ReadMemory(heapsAddress, heaps.data(), heapCount * sizeof(mi_heap_t*));
    if (FAILED(hr))
        return hr;

    size_t totalPages = 0;
    size_t totalSegments = 0;
    for (size_t i = 0; i < heapCount; i++) {
        ULONG64 heapAddr = (ULONG64)heaps[i];
        mi_heap_t heap = {0};
        hr = ReadMemory(heapAddr, &heap, sizeof(heap));
        if (FAILED(hr))
            continue;
        totalPages += heap.page_count;
        totalSegments += heap.segment_count;
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Aggregated Heap Statistics:\n  Total Heaps: %zu\n  Total Pages: %zu, Total Segments: %zu\n", heapCount, totalPages, totalSegments);

    return S_OK;
}
