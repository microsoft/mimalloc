/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

/*
Command: !mi_show_extension_settings
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_show_extension_settings(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(client);
    UNREFERENCED_PARAMETER(args);

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     std::format("{:<30} {:<10} {}\n", "MinCommittedSlicesPct", MinCommittedSlicesPct, "When below threshold, potential fragmentation.").c_str());

    g_DebugControl->ControlledOutput(
        DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
        std::format("{:<30} {:<10} {}\n", "MaxAbandonedPagesRatio", MaxAbandonedPagesRatio, "When abandoned pages exceeds ratio of committed pages, potential leak.").c_str());

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    g_DebugControl->ControlledOutput(DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,
                                     "Use <b>!mi_set_extension_setting &lt;setting&gt; &lt;value&gt;</b> to set a new value for the setting.\n");

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    return S_OK;
}

/*
Command: !mi_set_extension_setting
*/
extern "C" __declspec(dllexport) HRESULT CALLBACK mi_set_extension_setting(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(client);

    if (!args || !*args) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "Usage: !mi_set_extension_setting <setting> <value>\n");
        return E_INVALIDARG;
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    char fieldName[64] = {};
    char valueStr[64] = {};

    memset(fieldName, 0, sizeof(fieldName));
    memset(valueStr, 0, sizeof(valueStr));

    // Parse the input arguments
    if (sscanf_s(args, "%63s %63s", fieldName, (unsigned)_countof(fieldName), valueStr, (unsigned)_countof(valueStr)) != 2) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Invalid input format. Expected: !mi_set_extension_setting <setting> <value>\n");
        return E_INVALIDARG;
    }

    // Update the setting based on the field name
    if (strcmp(fieldName, "MinCommittedSlicesPct") == 0) {
        double doubleValue = 0.0;
        if (sscanf_s(valueStr, "%lf", &doubleValue) != 1 || doubleValue < 0 || doubleValue > 100) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Value must be an value between 0 and 100.\n");
            return E_INVALIDARG;
        }

        MinCommittedSlicesPct = doubleValue;
    } else if (strcmp(fieldName, "MaxAbandonedPagesRatio") == 0) {
        double doubleValue = 0.0;
        if (sscanf_s(valueStr, "%lf", &doubleValue) != 1 || doubleValue < 0.0 || doubleValue > 1.0) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Value must be a value between 0.0 and 1.0.\n");
            return E_INVALIDARG;
        }

        MaxAbandonedPagesRatio = doubleValue;
    } else {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Invalid field name. Allowed fields: MinCommittedSlicesPct, MaxAbandonedPagesRatio\n");
        return E_INVALIDARG;
    }

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "Successfully updated %s to %s\n", fieldName, valueStr);
    return S_OK;
}
