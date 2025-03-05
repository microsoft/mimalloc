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
#include <string>
#include <vector>

#include "mimalloc.h"
#include "mimalloc/internal.h"

extern ULONG64 g_MiMallocBase;

extern IDebugClient* g_DebugClient;
extern IDebugControl4* g_DebugControl;
extern IDebugSymbols3* g_DebugSymbols;
extern IDebugDataSpaces* g_DataSpaces;

HRESULT FindMimallocBase();
HRESULT GetSymbolOffset(const char* symbolName, ULONG64& outOffset);
HRESULT ReadMemory(const char* symbolName, void* outBuffer, size_t bufferSize);
HRESULT ReadMemory(ULONG64 address, void* outBuffer, size_t bufferSize);
HRESULT ReadString(const char* symbolName, std::string& outBuffer);

#endif // MIMALLOC_WINDBG_UTILS_H