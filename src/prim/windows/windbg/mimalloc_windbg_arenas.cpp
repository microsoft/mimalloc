/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

/*
Command: !mi_dump_arenas
    - Arenas are a fundamental component of mimalloc’s memory management.
    - A high-level dump of arenas helps diagnose overall memory allocation behavior,
      including fragmentation and leaks.
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_arenas(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);

    HRESULT hr = S_OK;

    ULONG64 subprocMainAddr = 0;
    hr = GetSymbolOffset("subproc_main", subprocMainAddr);
    if (FAILED(hr) || subprocMainAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Could not locate subproc_main.\n");
        return E_FAIL;
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");
    PrintLink(subprocMainAddr, std::format("dx -r1 (*((mimalloc!mi_subproc_s *)0x{:016X}))", subprocMainAddr), "subproc_main");

    mi_subproc_t subprocMain {};
    hr = ReadMemory(subprocMainAddr, &subprocMain, sizeof(mi_subproc_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read subproc_main at 0x%llx.\n", subprocMainAddr);
        return hr;
    }

    // Print results
    size_t arenaCount = subprocMain.arena_count.load();

    size_t totalSlices = 0;
    size_t totalCommittedSlices = 0;
    size_t totalCommittedPages = 0;
    size_t totalAbandonedPages = 0;

    for (size_t i = 0; i < arenaCount; ++i) {
        ULONG64 arenaAddr = subprocMainAddr + offsetof(mi_subproc_t, arenas) + (i * sizeof(std::atomic<mi_arena_t*>));
        ULONG64 arenaValueAddr = 0;
        HRESULT hr = ReadMemory(arenaAddr, &arenaValueAddr, sizeof(ULONG64));
        if (FAILED(hr) || arenaValueAddr == 0) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_arena_t pointer at 0x%016llx.\n", arenaAddr);
            return hr;
        }

        mi_arena_t arena {};
        hr = ReadMemory(arenaValueAddr, &arena, sizeof(mi_arena_t));
        if (FAILED(hr)) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_arena_t at 0x%016llx.\n", arenaAddr);
            return hr;
        }

        totalSlices += arena.slice_count;

        // Count committed slices using the bitmap in arena->slices_committed.
        mi_bitmap_t slicesCommitted {};
        hr = ReadMemory(reinterpret_cast<ULONG64>(arena.slices_committed), &slicesCommitted, sizeof(mi_bitmap_t));
        if (FAILED(hr) || arenaValueAddr == 0) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read arena.slices_committed pointer at 0x%016llx.\n", reinterpret_cast<ULONG64>(arena.slices_committed));
            return hr;
        }

        size_t committed = mi_bitmap_count(&slicesCommitted);
        totalCommittedSlices += committed;

        // Count committed pages using the bitmap in arena->pages.
        mi_bitmap_t pages {};
        hr = ReadMemory(reinterpret_cast<ULONG64>(arena.pages), &pages, sizeof(mi_bitmap_t));
        if (FAILED(hr) || arenaValueAddr == 0) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read pages pointer at 0x%016llx.\n", reinterpret_cast<ULONG64>(arena.pages));
            return hr;
        }
        size_t committedPages = mi_bitmap_count(&pages);
        totalCommittedPages += committedPages;

        // For abandoned pages, iterate over each bin (0 to MI_BIN_COUNT - 1).
        for (int bin = 0; bin < MI_BIN_COUNT; bin++) {
            ULONG64 itemAdr = reinterpret_cast<ULONG64>(arena.pages_abandoned[bin]);
            mi_bitmap_t abandonedBmp {};
            hr = ReadMemory(itemAdr, &abandonedBmp, sizeof(mi_bitmap_t));
            if (FAILED(hr) || arenaValueAddr == 0) {
                g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read pages_abandoned item at 0x%016llx.\n", itemAdr);
                return hr;
            }

            size_t abandonedPages = mi_bitmap_count(&abandonedBmp);
            totalAbandonedPages += abandonedPages;
        }
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\nTotal Arenas: %d\n", arenaCount);

    // Summary that aids in detecting fragmentation or undercommitment.
    double pctCommitted = (totalSlices > 0) ? (totalCommittedSlices * 100.0 / totalSlices) : 0.0;
    const auto arenaStats = std::format(" Total Slices: {}\n Committed Slices: {} ({}%)", totalSlices, totalCommittedSlices, pctCommitted);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\nAggregated Arena Statistics:\n%s\n", arenaStats.c_str());

    const auto pageStats = std::format(" Committed Pages: {}\n Abandoned Pages: {}", totalCommittedPages, totalAbandonedPages);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\nAggregated Page Statistics:\n%s\n", pageStats.c_str());
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    for (size_t i = 0; i < arenaCount; ++i) {
        ULONG64 arenaAddr = subprocMainAddr + offsetof(mi_subproc_t, arenas) + (i * sizeof(std::atomic<mi_arena_t*>));
        PrintLink(arenaAddr, std::format("!mi_dump_arena 0x{:016X}", arenaAddr), std::format("Arena {}", i));
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    return S_OK;
}

/*
Command: !mi_dump_arena <arena_address>
    - This command provides a detailed view of a specific arena, including its
      internal structures and state.
    - It helps diagnose issues related to specific arenas, such as memory leaks,
      fragmentation, or incorrect state.
    - The detailed dump includes information about slices, pages, and other
      critical components of the arena.
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_arena(PDEBUG_CLIENT client, PCSTR args) {
    if (!args || !*args) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Usage: !mi_dump_arena <arena_address>\n");
        return E_INVALIDARG;
    }

    ULONG64 arenaAddr = 0;
    if (sscanf_s(args, "%llx", &arenaAddr) != 1 || arenaAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Invalid arena address provided: %s\n", args);
        return E_INVALIDARG;
    }

    ULONG64 arenaValueAddr = 0;
    HRESULT hr = ReadMemory(arenaAddr, &arenaValueAddr, sizeof(ULONG64));
    if (FAILED(hr) || arenaValueAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_arena_t pointer at 0x%016llx.\n", arenaAddr);
        return hr;
    }

    PrintLink(arenaAddr, std::format("dx -r1 ((mimalloc!mi_arena_s *)0x{:016X})", arenaValueAddr), std::format("Dumping Arena at Address: 0x{:016X}\n", arenaAddr));

    // Read the `mi_arena_t` structure from the memory
    mi_arena_t arena {};
    hr = ReadMemory(arenaValueAddr, &arena, sizeof(mi_arena_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_arena_t at 0x%016llx.\n", arenaAddr);
        return hr;
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "[0x%p]   - Slice Count: %llu\n", arenaAddr + offsetof(mi_arena_t, slice_count), arena.slice_count);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "[0x%p]   - Info Slices: %llu\n", arenaAddr + offsetof(mi_arena_t, info_slices), arena.info_slices);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "[0x%p]   - NUMA Node: %d\n", arenaAddr + offsetof(mi_arena_t, numa_node), arena.numa_node);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "[0x%p]   - Exclusive: %s\n", arenaAddr + offsetof(mi_arena_t, is_exclusive), arena.is_exclusive ? "Yes" : "No");
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "[0x%p]   - Purge Expire: %d\n", arenaAddr + offsetof(mi_arena_t, purge_expire), arena.purge_expire.load());

    size_t totalSlices = 0;
    size_t totalCommittedSlices = 0;
    size_t totalCommittedPages = 0;
    size_t totalAbandonedPages = 0;

    totalSlices += arena.slice_count;

    // Count committed slices using the bitmap in arena->slices_committed.
    mi_bitmap_t slicesCommitted {};
    hr = ReadMemory(reinterpret_cast<ULONG64>(arena.slices_committed), &slicesCommitted, sizeof(mi_bitmap_t));
    if (FAILED(hr) || arenaValueAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read arena.slices_committed pointer at 0x%016llx.\n", reinterpret_cast<ULONG64>(arena.slices_committed));
        return hr;
    }

    size_t committed = mi_bitmap_count(&slicesCommitted);
    totalCommittedSlices += committed;

    // Count committed pages using the bitmap in arena->pages.
    mi_bitmap_t pages {};
    hr = ReadMemory(reinterpret_cast<ULONG64>(arena.pages), &pages, sizeof(mi_bitmap_t));
    if (FAILED(hr) || arenaValueAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read pages pointer at 0x%016llx.\n", reinterpret_cast<ULONG64>(arena.pages));
        return hr;
    }
    size_t committedPages = mi_bitmap_count(&pages);
    totalCommittedPages += committedPages;

    // For abandoned pages, iterate over each bin (0 to MI_BIN_COUNT - 1).
    for (int bin = 0; bin < MI_BIN_COUNT; bin++) {
        ULONG64 itemAdr = reinterpret_cast<ULONG64>(arena.pages_abandoned[bin]);
        mi_bitmap_t abandonedBmp {};
        hr = ReadMemory(itemAdr, &abandonedBmp, sizeof(mi_bitmap_t));
        if (FAILED(hr) || arenaValueAddr == 0) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read pages_abandoned item at 0x%016llx.\n", itemAdr);
            return hr;
        }

        size_t abandonedPages = mi_bitmap_count(&abandonedBmp);
        totalAbandonedPages += abandonedPages;
    }

    // Summary that aids in detecting fragmentation or undercommitment.
    double pctCommitted = (totalSlices > 0) ? (totalCommittedSlices * 100.0 / totalSlices) : 0.0;
    const auto arenaStats = std::format(" Total Slices: {}\n Committed Slices: {} ({}%)", totalSlices, totalCommittedSlices, pctCommitted);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\nAggregated Arena Statistics:\n%s\n", arenaStats.c_str());

    const auto pageStats = std::format(" Committed Pages: {}\n Abandoned Pages: {}", totalCommittedPages, totalAbandonedPages);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\nAggregated Page Statistics:\n%s\n", pageStats.c_str());
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    return S_OK;
}
