/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_arenas(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);

    HRESULT hr = S_OK;

    ULONG64 subprocMainAddr = 0;
    hr = GetSymbolOffset("subproc_main", subprocMainAddr);
    if (FAILED(hr) || subprocMainAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Could not locate subproc_main.\n");
        return E_FAIL;
    }

    PrintLink(subprocMainAddr, std::format("dx -r1 (*((mimalloc!mi_subproc_s *)0x{:016X}))", subprocMainAddr), "subproc_main");

    mi_subproc_t subprocMain {};
    hr = ReadMemory(subprocMainAddr, &subprocMain, sizeof(mi_subproc_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read subproc_main at 0x%llx.\n", subprocMainAddr);
        return hr;
    }

    // Print results
    size_t arenaCount = subprocMain.arena_count.load();
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Arena count: %llu\n", arenaCount);

    for (size_t i = 0; i < arenaCount; ++i) {
        ULONG64 arenaAddr = subprocMainAddr + offsetof(mi_subproc_t, arenas) + (i * sizeof(std::atomic<mi_arena_t*>));
        PrintLink(arenaAddr, std::format("!mi_dump_arena 0x{:016X}", arenaAddr), std::format("Arena {}", i));
    }

    return S_OK;
}

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

    // g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "[0x%p]   - Slices Free (Count): %d\n", arena.slices_free->chunk_count.load());
    // g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "[0x%p]   - Slices Committed (Max Accessed): %d\n", arena.slices_committed.load());

    return S_OK;
}
