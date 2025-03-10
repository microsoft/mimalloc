/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

/*
Command: !mi_show_help
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_show_help(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);

    HRESULT hr = S_OK;

    // Print Help
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Hello from MiMalloc WinDbg Extension!\n");
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Start here:\n");

    return S_OK;
}