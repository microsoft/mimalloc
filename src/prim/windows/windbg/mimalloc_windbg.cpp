/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

// Entry point for the extension
extern "C" __declspec(dllexport) HRESULT CALLBACK DebugExtensionInitialize(PULONG version, PULONG flags) {
    UNREFERENCED_PARAMETER(flags);

    // Ensure Version is valid
    if (!version) {
        return E_INVALIDARG;
    }

    // Set the version
    *version = DEBUG_EXTENSION_VERSION(1, 0);

    // Create the debug interfaces
    HRESULT hr = DebugCreate(__uuidof(IDebugClient), (void**)&g_DebugClient);
    if (FAILED(hr)) {
        return hr;
    }

    // Query for the IDebugControl interface
    hr = g_DebugClient->QueryInterface(__uuidof(IDebugControl4), (void**)&g_DebugControl);
    if (FAILED(hr)) {
        g_DebugClient->Release();

        return hr;
    }

    // Query for the IDebugSymbols3 interface
    hr = g_DebugClient->QueryInterface(__uuidof(IDebugSymbols3), (void**)&g_DebugSymbols);
    if (FAILED(hr)) {
        g_DebugControl->Release();
        g_DebugClient->Release();

        return hr;
    }

    // Query for the IDebugDataSpaces interface
    hr = g_DebugClient->QueryInterface(__uuidof(IDebugDataSpaces), (void**)&g_DataSpaces);
    if (FAILED(hr)) {
        g_DebugSymbols->Release();
        g_DebugControl->Release();
        g_DebugClient->Release();

        return hr;
    }

    // Find mimalloc base address at startup
    hr = FindMimallocBase();
    if (FAILED(hr) || g_MiMallocBase == 0) {
        return E_FAIL; // Prevent extension from loading
    }

    // Register output callbacks
    mi_register_output(
        [](const char* msg, void* arg) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, msg);
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "\n");
        },
        nullptr);

    // Print the mimalloc base address, this indicates extension was load successfully
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "mimalloc.dll base address found: 0x%llx\n\n", g_MiMallocBase);

    // show extension version
    const int vermajor = MI_MALLOC_VERSION / 100;
    const int verminor = (MI_MALLOC_VERSION % 100) / 10;
    const int verpatch = (MI_MALLOC_VERSION % 10);
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "mimalloc extension %s\n", std::format("v{}.{}.{} (built on {}, {})\n", vermajor, verminor, verpatch, __DATE__, __TIME__).c_str());

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Start here:\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_dump_options\">Dump Options</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_dump_arenas\">Dump Arenas</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_dump_stats\">Dump Statistics</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_show_extension_settings\">Show Extension Settings</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_show_help\">Show Help commands</link>\n");
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    return S_OK;
}

// Notifies the extension that a debug event has occurred
extern "C" __declspec(dllexport) void CALLBACK DebugExtensionNotify(ULONG notify, ULONG64 argument) {
    UNREFERENCED_PARAMETER(notify);
    UNREFERENCED_PARAMETER(argument);
}

// Uninitializes the extension
extern "C" __declspec(dllexport) void CALLBACK DebugExtensionUninitialize() {
    if (g_DebugSymbols) {
        g_DebugSymbols->Release();
        g_DebugSymbols = nullptr;
    }

    if (g_DebugControl) {
        g_DebugControl->Release();
        g_DebugControl = nullptr;
    }

    if (g_DebugClient) {
        g_DebugClient->Release();
        g_DebugClient = nullptr;
    }
}