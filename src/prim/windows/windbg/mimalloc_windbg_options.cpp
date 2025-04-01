/* ----------------------------------------------------------------------------
Copyright (c) Microsoft Research
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "mimalloc_windbg_utils.h"

extern "C" __declspec(dllexport) HRESULT CALLBACK mi_dump_options(PDEBUG_CLIENT client, PCSTR args) {
    UNREFERENCED_PARAMETER(args);

    HRESULT hr = S_OK;

    g_DebugControl->Output(DEBUG_OUTPUT_NORMAL, "\n");

    ULONG64 optionsAddr = 0;
    hr = GetSymbolOffset("mi_options", optionsAddr);
    if (FAILED(hr) || optionsAddr == 0) {
        g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Could not locate optionsAddr.\n");
        return E_FAIL;
    }

    const int optionsSize = static_cast<int>(mi_option_e::_mi_option_last);
    PrintLink(optionsAddr, std::format("dx -r1 (*((mimalloc!mi_option_desc_s (*)[{}])0x{:016X}))", optionsSize, optionsAddr),
              std::format("Dumping Options at Address: 0x{:016X}\n", optionsAddr));

    for (size_t i = 0; i < optionsSize; ++i) {
        ULONG64 optionItemAddr = optionsAddr + (i * sizeof(mi_option_desc_t));

        mi_option_desc_t optionDesc {};
        hr = ReadMemory(optionItemAddr, &optionDesc, sizeof(mi_option_desc_t));
        if (FAILED(hr)) {
            g_DebugControl->Output(DEBUG_OUTPUT_ERROR, "ERROR: Failed to read mi_option_desc_t at 0x%llx.\n", optionItemAddr);
            return hr;
        }

        PrintLink(optionItemAddr, std::format("dx -r1 (*((mimalloc!mi_option_desc_s *)0x{:016X}))", optionItemAddr), std::to_string(i),
                  std::format(" {}: {}", optionDesc.name, optionDesc.value));
    }

    return S_OK;
}