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
extern IDebugSystemObjects4* g_DebugSystemObjects;

static inline double MinCommittedSlicesPct = 60.0; // if below threshold, potential fragmentation.
static inline double MaxAbandonedPagesRatio = 0.4; // If abandoned pages exceed 40% of committed pages.

HRESULT FindMimallocBase();
HRESULT GetSymbolOffset(const char* symbolName, ULONG64& outOffset);
HRESULT GetNtSymbolOffset(const char* typeName, const char* fieldName, ULONG& outOffset);
HRESULT ReadMemory(const char* symbolName, void* outBuffer, size_t bufferSize);
HRESULT ReadMemory(ULONG64 address, void* outBuffer, size_t bufferSize);
HRESULT ReadString(const char* symbolName, std::string& outBuffer);
HRESULT ReadWideString(ULONG64 address, std::wstring& outString, size_t maxLength = 1024);
size_t mi_bitmap_count(mi_bitmap_t* bmp);
std::string FormatSize(std::size_t bytes);
std::string FormatNumber(double num);

inline void PrintLink(ULONG64 addr, std::string cmd, std::string linkText, std::string extraText = "") {
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("[0x{:016X}] <link cmd=\"{}\">{}</link> {}\n", addr, cmd, linkText, extraText).c_str());
}

struct UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    ULONG64 Buffer;
};

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

#endif // MIMALLOC_WINDBG_UTILS_H