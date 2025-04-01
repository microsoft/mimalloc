/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

/*
Command: !mi_dump_process_info
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_process_info(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);

    HRESULT hr = S_OK;

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    // Get process handle
    ULONG64 processHandle = 0;
    hr = g_DebugSystemObjects->GetCurrentProcessHandle(&processHandle);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to get Process Handle.\n");
        return hr;
    }

    // Get process ID
    ULONG processId = 0;
    hr = g_DebugSystemObjects->GetCurrentProcessSystemId(&processId);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to get Process ID.\n");
        return hr;
    }

    // Get executable arguments
    ULONG64 pebAddr = 0;
    hr = g_DebugSystemObjects->GetCurrentProcessPeb(&pebAddr);
    if (FAILED(hr) || pebAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to get PEB address.\n");
        return hr;
    }

    // Get offset of ProcessParameters
    ULONG processParametersOffset = 0;
    hr = GetNtSymbolOffset("_PEB", "ProcessParameters", processParametersOffset);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to get ProcessParameters offset.\n");
        return hr;
    }

    ULONG64 processParametersAddr = 0;
    hr = ReadMemory(pebAddr + processParametersOffset, &processParametersAddr, sizeof(ULONG64));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read ProcessParameters address from PEB.\n");
        return hr;
    }

    // Get offset of CommandLine
    ULONG commandLineOffset = 0;
    hr = GetNtSymbolOffset("_RTL_USER_PROCESS_PARAMETERS", "CommandLine", commandLineOffset);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to get CommandLine offset.\n");
        return hr;
    }

    UNICODE_STRING cmdLine {};
    hr = ReadMemory(processParametersAddr + commandLineOffset, &cmdLine, sizeof(cmdLine));
    if (FAILED(hr) || cmdLine.Buffer == 0 || cmdLine.Length == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read CommandLine struct.\n");
        return hr;
    }

    // Read the command line string
    std::wstring commandLine;
    hr = ReadWideString(cmdLine.Buffer, commandLine, cmdLine.Length / sizeof(WCHAR));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read CommandLine string.\n");
        return hr;
    }

    // Get Processor Type (x86, x64, ARM64, etc.)
    ULONG processorType = 0;
    hr = g_DebugControl->GetActualProcessorType(&processorType);
    const char* arch = (processorType == IMAGE_FILE_MACHINE_AMD64)   ? "x64"
                       : (processorType == IMAGE_FILE_MACHINE_I386)  ? "x86"
                       : (processorType == IMAGE_FILE_MACHINE_ARM64) ? "ARM64"
                                                                     : "Unknown";

    // Read NUMA node count
    ULONG64 numaNodeOffSet = 0;
    hr = GetSymbolOffset("_mi_numa_node_count", numaNodeOffSet);
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to get NUMA node count address.\n");
        return hr;
    }

    ULONG numaNodeCount = 0;
    hr = ReadMemory(numaNodeOffSet, &numaNodeCount, sizeof(ULONG));
    if (FAILED(hr)) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read NUMA node count.\n");
        return hr;
    }

    // Output debuggee process info
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Debugging Process (From Debuggee Context):\n");
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  Process ID       : %u (0x%X)\n", processId, processId);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  Command Line     : %s\n", commandLine.c_str());
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  Architecture     : %s\n", arch);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  NUMA Node        : %u\n", numaNodeCount);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "  Process Handle   : %p\n", processHandle);

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");
    return S_OK;
}
