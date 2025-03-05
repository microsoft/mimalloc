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
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Could not locate mi_subproc_t.\n");
        return E_FAIL;
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "mi_subproc_t found at 0x%llx\n\n", subprocMainAddr);

    mi_subproc_t subprocMain {};
    hr = ReadMemory(subprocMainAddr, &subprocMain, sizeof(mi_subproc_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_subproc_t at 0x%llx.\n", subprocMainAddr);
        return hr;
    }

    // Print results
    size_t arenaCount = subprocMain.arena_count.load();
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Arena count: %llu\n\n", arenaCount);

    for (size_t i = 0; i < arenaCount; ++i) {
        ULONG64 arenaAddr = subprocMainAddr + offsetof(mi_subproc_t, arenas) + (i * sizeof(std::atomic<mi_arena_t*>));

        // Print clickable Arena Index link
        g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                         "[0x%p] [<link cmd=\"!mi_dump_arena %p\">Arena %llu</link>]\n", (void*)arenaAddr, (void*)arenaAddr, i);
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

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Dumping Arena at Address: 0x%016llx\n\n", arenaAddr);

    //// Read the `mi_arena_t` structure from the memory
    // mi_arena_t arena{};
    // HRESULT hr = ReadMemory(arenaAddr, &arena, sizeof(mi_arena_t));
    // if (FAILED(hr))
    //{
    //     g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_arena_t at 0x%016llx.\n", arenaAddr);
    //     return hr;
    // }

    // g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  - Slice Count: %llu\n", arena.slice_count);
    // g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  - Info Slices: %llu\n", arena.info_slices);
    // g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  - NUMA Node: %d\n", arena.numa_node);
    // g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  - Exclusive: %s\n", arena.is_exclusive ? "Yes" : "No");
    //
    return S_OK;
}
