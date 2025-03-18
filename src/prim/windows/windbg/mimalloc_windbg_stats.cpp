/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include <cassert>
#include "mimalloc_windbg_utils.h"

HRESULT PrintItemStats(ULONG64 statsAddr, std::ptrdiff_t fieldOffset, std::string_view fieldName, std::string_view name, bool formatAsSize) {
    HRESULT hr = S_OK;
    ULONG64 itemAddr = statsAddr + fieldOffset;
    mi_stat_count_s item {};
    hr = ReadMemory(itemAddr, &item, sizeof(mi_stat_count_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, std::format("ERROR: Failed to read {0} at 0x{1:016X}.\n", fieldName, itemAddr).c_str());
        return hr;
    }

    std::string peak = "";
    std::string total = "";
    std::string current = "";

    if (formatAsSize) {
        peak = FormatSize(item.peak);
        total = FormatSize(item.total);
        current = FormatSize(item.current);
    } else {
        peak = FormatNumber(item.peak);
        total = FormatNumber(item.total);
        current = FormatNumber(item.current);
    }

    const std::string pad = name.size() < 10 ? std::string(10 - name.size(), ' ') : "";
    g_DebugControl->ControlledOutput(
        DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
        std::format("{0}<link cmd=\"dx -r1 (*((mimalloc!mi_stat_count_s *)0x{1:016X}))\">{2}</link> {3:>20} {4:>20} {5:>20}\n", pad, itemAddr, name, peak, total, current).c_str());

    return S_OK;
}

/*
Command: !mi_dump_stats
    -
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_stats(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);

    HRESULT hr = S_OK;

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    ULONG64 subprocMainAddr = 0;
    hr = GetSymbolOffset("subproc_main", subprocMainAddr);
    if (FAILED(hr) || subprocMainAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Could not locate subproc_main.\n");
        return E_FAIL;
    }

    mi_subproc_t subprocMain {};
    hr = ReadMemory(subprocMainAddr, &subprocMain, sizeof(mi_subproc_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read subproc_main at 0x%llx.\n", subprocMainAddr);
        return hr;
    }

    // Read _mi_heap_empty
    ULONG64 miHeapEmptyAddr = 0;
    hr = GetSymbolOffset("_mi_heap_empty", miHeapEmptyAddr);
    if (FAILED(hr) || subprocMainAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Could not locate _mi_heap_empty.\n");
        return E_FAIL;
    }

    mi_heap_t miHeapEmpty {};
    hr = ReadMemory(miHeapEmptyAddr, &miHeapEmpty, sizeof(mi_heap_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_heap_t at 0x%llx.\n", miHeapEmptyAddr);
        return hr;
    }

    ULONG64 statsAddr = subprocMainAddr + offsetof(mi_subproc_t, stats);
    ULONG64 mallocBinsAddr = statsAddr + offsetof(mi_stats_t, malloc_bins);

    // Print Heap Header
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("<link cmd=\"dx -r1 (*((mimalloc!mi_stat_count_s (*)[{0}])0x{1:016X}))\">{2:>10}</link> {3:>20} {4:>20} {5:>20} {6:>20} {7:>20}\n",
                                                 MI_BIN_HUGE + 1, mallocBinsAddr, "Heap Stats", "Peak", "Total", "Current", "Block Size", "Total#")
                                         .c_str());

    for (size_t i = 0; i <= MI_BIN_HUGE; i++) {
        ULONG64 binAddr = mallocBinsAddr + (i * sizeof(mi_stat_count_t));
        mi_stat_count_t malloc_bin {};
        hr = ReadMemory(binAddr, &malloc_bin, sizeof(mi_stat_count_t));
        if (FAILED(hr)) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read malloc_bins at 0x%llx.\n", binAddr);
            return hr;
        }

        if (malloc_bin.total > 0) {
            ULONG64 pagesAddr = miHeapEmptyAddr + offsetof(mi_heap_t, pages) + (i * sizeof(mi_page_queue_t));
            mi_page_queue_t page {};
            hr = ReadMemory(pagesAddr, &page, sizeof(mi_page_queue_t));
            if (FAILED(hr)) {
                g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read page_bins at 0x%llx.\n", pagesAddr);
                return hr;
            }

            int64_t unit = page.block_size;

            g_DebugControl->ControlledOutput(
                DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                std::format("bin {0}<link cmd=\"dx -r1 (*((mimalloc!mi_stat_count_s *)0x{1:016X}))\">{2}</link> {3:>20} {4:>20} {5:>20} {6:>20} {7:>20}\n",
                            std::string(6 - std::to_string(i).size(), ' '), binAddr, i, FormatSize(malloc_bin.peak * unit), FormatSize(malloc_bin.total * unit),
                            FormatSize(malloc_bin.current * unit), FormatSize(unit), FormatNumber(malloc_bin.total))
                    .c_str());
        }
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("{0:>10} {1:>20} {2:>20} {3:>20}\n", "Heap Stats", "Peak", "Total", "Current").c_str());

    ULONG64 mallocNormalAddr = statsAddr + offsetof(mi_stats_t, malloc_normal);
    mi_stat_count_s mallocNormal {};
    hr = ReadMemory(mallocNormalAddr, &mallocNormal, sizeof(mi_stat_count_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read malloc_normal at 0x%llx.\n", mallocNormalAddr);
        return hr;
    }

    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("    <link cmd=\"dx -r1 (*((mimalloc!mi_stat_count_s *)0x{0:016X}))\">normal</link> {1:>20} {2:>20} {3:>20}\n", mallocNormalAddr,
                                                 FormatSize(mallocNormal.peak), FormatSize(mallocNormal.total), FormatSize(mallocNormal.current))
                                         .c_str());

    ULONG64 mallocHugeAddr = statsAddr + offsetof(mi_stats_t, malloc_huge);
    mi_stat_count_s mallocHuge {};
    hr = ReadMemory(mallocHugeAddr, &mallocHuge, sizeof(mi_stat_count_t));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read malloc_huge at 0x%llx.\n", mallocHugeAddr);
        return hr;
    }

    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("      <link cmd=\"dx -r1 (*((mimalloc!mi_stat_count_s *)0x{0:016X}))\">huge</link> {1:>20} {2:>20} {3:>20}\n", mallocHugeAddr,
                                                 FormatSize(mallocHuge.peak), FormatSize(mallocHuge.total), FormatSize(mallocHuge.current))
                                         .c_str());

    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("     total {0:>20} {1:>20} {2:>20}\n", FormatSize(mallocNormal.peak + mallocHuge.peak),
                                                 FormatSize(mallocNormal.total + mallocHuge.total), FormatSize(mallocNormal.current + mallocHuge.current))
                                         .c_str());

    PrintItemStats(statsAddr, offsetof(mi_stats_t, malloc_requested), "malloc_requested", "requested", true);

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    // Reserved
    PrintItemStats(statsAddr, offsetof(mi_stats_t, reserved), "reserved", "reserved", true);

    // Committed
    PrintItemStats(statsAddr, offsetof(mi_stats_t, committed), "committed", "committed", true);

    // Reset
    PrintItemStats(statsAddr, offsetof(mi_stats_t, reset), "reset", "reset", true);

    // Purged
    PrintItemStats(statsAddr, offsetof(mi_stats_t, purged), "purged", "purged", true);

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    // pages
    PrintItemStats(statsAddr, offsetof(mi_stats_t, pages), "pages", "pages", false);

    // abandoned
    PrintItemStats(statsAddr, offsetof(mi_stats_t, pages_abandoned), "pages_abandoned", "abandoned", false);

    // pages_reclaim_on_alloc
    PrintItemStats(statsAddr, offsetof(mi_stats_t, pages_reclaim_on_alloc), "pages_reclaim_on_alloc", "reclaim A", false);

    // pages_reclaim_on_free
    PrintItemStats(statsAddr, offsetof(mi_stats_t, pages_reclaim_on_free), "pages_reclaim_on_free", "reclaim F", false);

    // pages_reabandon_full
    PrintItemStats(statsAddr, offsetof(mi_stats_t, pages_reabandon_full), "pages_reabandon_full", "reabandon", false);

    // pages_unabandon_busy_wait
    PrintItemStats(statsAddr, offsetof(mi_stats_t, pages_unabandon_busy_wait), "pages_unabandon_busy_wait", "waits", false);

    // pages_retire
    PrintItemStats(statsAddr, offsetof(mi_stats_t, pages_retire), "pages_retire", "retire", false);

    // arena_count
    PrintItemStats(statsAddr, offsetof(mi_stats_t, arena_count), "arena_count", "arenas", false);

    // arena_rollback_count
    PrintItemStats(statsAddr, offsetof(mi_stats_t, arena_rollback_count), "arena_rollback_count", "rollback", false);

    // mmap_calls
    PrintItemStats(statsAddr, offsetof(mi_stats_t, mmap_calls), "mmap_calls", "mmaps", false);

    // commit_calls
    PrintItemStats(statsAddr, offsetof(mi_stats_t, commit_calls), "commit_calls", "commits", false);

    // reset_calls
    PrintItemStats(statsAddr, offsetof(mi_stats_t, reset_calls), "reset_calls", "resets", false);

    // purge_calls
    PrintItemStats(statsAddr, offsetof(mi_stats_t, purge_calls), "purge_calls", "purges", false);

    // malloc_guarded_count
    PrintItemStats(statsAddr, offsetof(mi_stats_t, malloc_guarded_count), "malloc_guarded_count", "guarded", false);

    // threads
    PrintItemStats(statsAddr, offsetof(mi_stats_t, threads), "threads", "threads", false);

    // page_searches
    PrintItemStats(statsAddr, offsetof(mi_stats_t, page_searches), "page_searches", "searches", false);

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    return S_OK;
}