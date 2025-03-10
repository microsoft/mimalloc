/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#ifndef MIMALLOC_WINDBG_UTILS_H
#define MIMALLOC_WINDBG_UTILS_H

#include <DbgEng.h>
#include <Windows.h>
#include <format>
#include <string>
#include <vector>

#include <mimalloc.h>
#include <mimalloc/internal.h>
#include "../src/bitmap.h"

extern ULONG64 g_MiMallocBase;

extern IDebugClient* g_DebugClient;
extern IDebugControl4* g_DebugControl;
extern IDebugSymbols3* g_DebugSymbols;
extern IDebugDataSpaces* g_DataSpaces;

constexpr double MinCommittedSlicesPct = 60.0; // if below threshold, potential fragmentation.
constexpr double MaxAbandonedPagesRatio = 0.4; // If abandoned pages exceed 40% of committed pages.

HRESULT FindMimallocBase();
HRESULT GetSymbolOffset(const char* symbolName, ULONG64& outOffset);
HRESULT ReadMemory(const char* symbolName, void* outBuffer, size_t bufferSize);
HRESULT ReadMemory(ULONG64 address, void* outBuffer, size_t bufferSize);
HRESULT ReadString(const char* symbolName, std::string& outBuffer);
size_t mi_bitmap_count(mi_bitmap_t* bmp);

inline void PrintLink(ULONG64 addr, std::string cmd, std::string linkText, std::string extraText = "") {
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("[0x{:016X}] <link cmd=\"{}\">{}</link> {}\n", addr, cmd, linkText, extraText).c_str());
}

#if defined(_MSC_VER)
#include <intrin.h>
static inline int popcount64(uint64_t x) {
    return (int)__popcnt64(x);
}
#else
// Portable fallback: count bits in a 64-bit value.
static inline int popcount64(uint64_t x) {
    int count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}
#endif

// TODO Remove the code below once it is avaialble in the mimalloc header
typedef struct mi_arena_s {
    mi_memid_t memid;      // memid of the memory area
    mi_subproc_t* subproc; // subprocess this arena belongs to (`this 'in' this->subproc->arenas`)

    size_t slice_count;               // total size of the area in arena slices (of `MI_ARENA_SLICE_SIZE`)
    size_t info_slices;               // initial slices reserved for the arena bitmaps
    int numa_node;                    // associated NUMA node
    bool is_exclusive;                // only allow allocations if specifically for this arena
    _Atomic(mi_msecs_t) purge_expire; // expiration time when slices can be purged from `slices_purge`.

    mi_bbitmap_t* slices_free;                  // is the slice free? (a binned bitmap with size classes)
    mi_bitmap_t* slices_committed;              // is the slice committed? (i.e. accessible)
    mi_bitmap_t* slices_dirty;                  // is the slice potentially non-zero?
    mi_bitmap_t* slices_purge;                  // slices that can be purged
    mi_bitmap_t* pages;                         // all registered pages (abandoned and owned)
    mi_bitmap_t* pages_abandoned[MI_BIN_COUNT]; // abandoned pages per size bin (a set bit means the start of the page)
                                                // the full queue contains abandoned full pages
                                                // followed by the bitmaps (whose sizes depend on the arena size)
                                                // note: when adding bitmaps revise `mi_arena_info_slices_needed`
} mi_arena_t;

typedef enum mi_init_e {
    UNINIT,     // not yet initialized
    DEFAULTED,  // not found in the environment, use default value
    INITIALIZED // found in environment or set explicitly
} mi_init_t;

typedef struct mi_option_desc_s {
    long value;              // the value
    mi_init_t init;          // is it initialized yet? (from the environment)
    mi_option_t option;      // for debugging: the option index should match the option
    const char* name;        // option name without `mimalloc_` prefix
    const char* legacy_name; // potential legacy option name
} mi_option_desc_t;

#endif // MIMALLOC_WINDBG_UTILS_H