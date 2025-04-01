/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

ULONG64 g_MiMallocBase = 0;

IDebugClient* g_DebugClient = nullptr;
IDebugControl4* g_DebugControl = nullptr;
IDebugSymbols3* g_DebugSymbols = nullptr;
IDebugDataSpaces* g_DataSpaces = nullptr;
IDebugSystemObjects4* g_DebugSystemObjects = nullptr;

// Function to find mimalloc.dll base address at startup
HRESULT FindMimallocBase() {
    if (g_DebugSymbols == nullptr) {
        return E_FAIL;
    }

    return g_DebugSymbols->GetModuleByModuleName("mimalloc", 0, NULL, &g_MiMallocBase);
}

// Function to get the offset of a symbol in the debuggee process
HRESULT GetSymbolOffset(const char* symbolName, ULONG64& outOffset) {
    // Ensure debug interfaces are valid
    if (!g_DebugSymbols || g_MiMallocBase == 0) {
        return E_FAIL;
    }

    // Construct the full symbol name: "mimalloc!<symbolName>"
    std::string fullSymbol = "mimalloc!";
    fullSymbol += symbolName;

    // Retrieve the memory offset of the symbol in the debuggee process
    HRESULT hr = g_DebugSymbols->GetOffsetByName(fullSymbol.c_str(), &outOffset);
    if (FAILED(hr) || outOffset == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to locate symbol: %s\n", fullSymbol.c_str());
        return E_FAIL;
    }

    return S_OK;
}

// Function to get the offset of a nt symbol in the debuggee process
HRESULT GetNtSymbolOffset(const char* typeName, const char* fieldName, ULONG& outOffset) {
    // Ensure debug interfaces are valid
    if (!g_DebugSymbols || g_MiMallocBase == 0) {
        return E_FAIL;
    }

    ULONG index = 0;
    ULONG64 base = 0;
    HRESULT hr = g_DebugSymbols->GetModuleByModuleName("ntdll", 0, &index, &base);
    if (FAILED(hr) || index == 0 || base == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to locate module name for ntdll");
        return E_FAIL;
    }

    ULONG typeId = 0;
    hr = g_DebugSymbols->GetTypeId(base, typeName, &typeId);
    if (FAILED(hr) || typeId == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to locate type id for: %s\n", typeName);
        return E_FAIL;
    }

    hr = g_DebugSymbols->GetFieldTypeAndOffset(base, typeId, fieldName, nullptr, &outOffset);
    if (FAILED(hr) || outOffset == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to locate symbol: %s\n", fieldName);
        return E_FAIL;
    }

    return S_OK;
}

// Function to read memory from the debuggee process
HRESULT ReadMemory(const char* symbolName, void* outBuffer, ULONG bufferSize) {
    if (!g_DataSpaces) {
        return E_FAIL;
    }

    // Step 1: Get the memory address of the symbol
    ULONG64 symbolOffset = 0;
    HRESULT hr = GetSymbolOffset(symbolName, symbolOffset);
    if (FAILED(hr)) {
        return hr;
    }

    // Step 2: Read memory from the debuggee
    hr = g_DataSpaces->ReadVirtual(symbolOffset, outBuffer, bufferSize, nullptr);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read memory for symbol: %s\n", symbolName);
        return hr;
    }

    return S_OK;
}

// Function to read memory from a specific address
HRESULT ReadMemory(ULONG64 address, void* outBuffer, ULONG bufferSize) {
    if (!g_DataSpaces) {
        return E_FAIL;
    }

    // Read memory directly from the given address
    HRESULT hr = g_DataSpaces->ReadVirtual(address, outBuffer, bufferSize, nullptr);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read memory at address: 0x%llx\n", address);
        return hr;
    }

    return S_OK;
}

// Function to read a string from the debuggee process
HRESULT ReadString(const char* symbolName, std::string& outBuffer) {
    if (!g_DataSpaces) {
        return E_FAIL;
    }

    // Step 1: Get the memory address of the symbol (pointer to string)
    ULONG64 stringPtr = 0;
    HRESULT hr = ReadMemory(symbolName, &stringPtr, sizeof(ULONG64));
    if (FAILED(hr) || stringPtr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Symbol %s is NULL or invalid.\n", symbolName);
        return E_FAIL;
    }

    // Step 2: Read the actual string data from the debuggee memory
    char tempChar = 0;
    ULONG length = 0;

    do {
        hr = g_DataSpaces->ReadVirtual(stringPtr + length, &tempChar, sizeof(char), nullptr);
        if (FAILED(hr)) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read string from address 0x%llx.\n", stringPtr);
            return hr;
        }

        length++;
    } while (tempChar != '\0');

    outBuffer.resize(length);
    hr = g_DataSpaces->ReadVirtual(stringPtr, &outBuffer[0], length, nullptr);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read string from address 0x%llx.\n", stringPtr);
        return hr;
    }

    return S_OK;
}

HRESULT ReadWideString(ULONG64 address, std::wstring& outString, size_t maxLength) {
    if (!g_DataSpaces) {
        return E_FAIL;
    }

    WCHAR buffer[1025] = {0}; // max 1024 chars + null terminator
    size_t readLength = min(maxLength, 1024);

    HRESULT hr = g_DataSpaces->ReadVirtual(address, buffer, (ULONG)(readLength * sizeof(WCHAR)), nullptr);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read string from address 0x%llx.\n", address);
        return hr;
    }

    outString = buffer;
    return S_OK;
}

// Helper function to count the number of set bits in a mi_bitmap_t.
// This implementation assumes that the bitmap is organized into chunks,
// and that each chunk contains MI_BCHUNK_FIELDS of type mi_bfield_t (usually a 64-bit word).
// It uses the popcount64 function (which uses __popcnt64 on MSVC) to count bits.
size_t mi_bitmap_count(mi_bitmap_t* bmp) {
    size_t chunkCount = bmp->chunk_count.load();
    size_t totalCount = 0;
    for (size_t i = 0; i < chunkCount; i++) {
        for (size_t j = 0; j < MI_BCHUNK_FIELDS; j++) {
            mi_bfield_t field = bmp->chunks[i].bfields[j];
            totalCount += (size_t)popcount64(field);
        }
    }
    return totalCount;
}

std::string FormatSize(int64_t value) {
    const char* suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    constexpr int maxIndex = static_cast<int>(std::size(suffixes)) - 1;
    int index = 0;

    double size = static_cast<double>(value);
    while (size >= 1024.0 && index < maxIndex) {
        size /= 1024.0;
        ++index;
    }

    return std::format("{:.2f} {}", size, suffixes[index]);
}

std::string FormatNumber(int64_t value) {
    const char* suffixes[] = {"", "K", "M", "B", "T"};
    constexpr int maxIndex = static_cast<int>(std::size(suffixes)) - 1;
    int index = 0;

    double num = static_cast<double>(value);
    while (num >= 1000.0 && index < maxIndex) {
        num /= 1000.0;
        ++index;
    }

    return std::format("{:.1f}{}", num, suffixes[index]);
}