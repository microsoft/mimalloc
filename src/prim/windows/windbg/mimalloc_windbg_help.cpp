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
    UNREFERENCED_PARAMETER(client);
    UNREFERENCED_PARAMETER(args);

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    // Print Help
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Hello from MiMalloc WinDbg Extension!\n");
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Start here:\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_dump_options\">Dump Options</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_dump_arenas\">Dump Arenas</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_dump_stats\">Dump Statistics</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_show_extension_settings\">Show Extension Settings</link>\n");
    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL, "<link cmd=\"!mi_show_help\">Show Help commands</link>\n");
    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    return S_OK;
}