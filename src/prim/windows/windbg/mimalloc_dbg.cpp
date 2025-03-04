/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include <atomic>
#include <DbgEng.h>
#include <map>
#include <string>
#include <vector>
#include <Windows.h>

#include "mimalloc.h"
#include "mimalloc/internal.h"

ULONG64 g_MiMallocBase = 0;
IDebugClient* g_DebugClient = nullptr;
IDebugControl* g_DebugControl = nullptr;
IDebugSymbols3* g_DebugSymbols = nullptr;
IDebugDataSpaces* g_DataSpaces = nullptr;

// Function to find mimalloc.dll base address at startup
HRESULT FindMimallocBase()
{
    if (g_DebugSymbols == nullptr)
    {
        return E_FAIL;
    }

    return g_DebugSymbols->GetModuleByModuleName("mimalloc", 0, NULL, &g_MiMallocBase);
}

// Entry point for the extension
extern "C" __declspec(dllexport) HRESULT CALLBACK DebugExtensionInitialize(PULONG version, PULONG flags)
{
    UNREFERENCED_PARAMETER(flags);

    // Ensure Version is valid
    if (!version)
    {
        return E_INVALIDARG;
    }

    // Set the version
    *version = DEBUG_EXTENSION_VERSION(1, 0);

    HRESULT hr = DebugCreate(__uuidof(IDebugClient), (void**)&g_DebugClient);
    if (FAILED(hr))
    {
        return hr;
    }

    // Query for the IDebugControl interface
    hr = g_DebugClient->QueryInterface(__uuidof(IDebugControl), (void**)&g_DebugControl);
    if (FAILED(hr))
    {
        g_DebugClient->Release();

        return hr;
    }

    hr = g_DebugClient->QueryInterface(__uuidof(IDebugSymbols3), (void**)&g_DebugSymbols);
    if (FAILED(hr))
    {
        g_DebugControl->Release();
        g_DebugClient->Release();

        return hr;
    }

    hr = g_DebugClient->QueryInterface(__uuidof(IDebugDataSpaces), (void**)&g_DataSpaces);
    if (FAILED(hr))
    {
        g_DebugSymbols->Release();
        g_DebugControl->Release();
        g_DebugClient->Release();

        return hr;
    }

    // Find mimalloc base address at startup
    hr = FindMimallocBase();
    if (FAILED(hr) || g_MiMallocBase == 0)
    {
        return E_FAIL;  // Prevent extension from loading
    }

    mi_register_output(
        [](const char* msg, void* arg) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, msg);
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "\n");
        },
        nullptr);

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "mimalloc.dll base address found: 0x%llx\n", g_MiMallocBase);

    return S_OK;
}

// Notifies the extension that a debug event has occurred
extern "C" __declspec(dllexport) void CALLBACK DebugExtensionNotify(ULONG notify, ULONG64 argument)
{
    UNREFERENCED_PARAMETER(notify);
    UNREFERENCED_PARAMETER(argument);
}

// Uninitializes the extension
extern "C" __declspec(dllexport) void CALLBACK DebugExtensionUninitialize()
{
    if (g_DebugSymbols)
    {
        g_DebugSymbols->Release();
        g_DebugSymbols = nullptr;
    }

    if (g_DebugControl)
    {
        g_DebugControl->Release();
        g_DebugControl = nullptr;
    }

    if (g_DebugClient)
    {
        g_DebugClient->Release();
        g_DebugClient = nullptr;
    }
}

// Sample command: !mi_help
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_help(PDEBUG_CLIENT Client, PCSTR args)
{
    UNREFERENCED_PARAMETER(args);

    // Print Help
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Hello from MiMalloc WinDbg Extension!\n");

    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_arenas(PDEBUG_CLIENT client, PCSTR args)
{
    mi_debug_show_arenas();
    return S_OK;
}