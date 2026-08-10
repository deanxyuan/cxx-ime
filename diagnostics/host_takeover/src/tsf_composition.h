// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_COMPOSITION_H_
#define CXXIME_HOST_TAKEOVER_TSF_COMPOSITION_H_

#include "pch.h"

#include <cstddef>

class TextService;

namespace cxxime_tsf {

struct TraceCompositionEditResult {
    const char* action = nullptr;
    size_t text_length = 0;
    size_t selection_offset = 0;
    bool sync_requested = false;
    bool async_fallback = false;
    HRESULT initial_request_hr = E_PENDING;
    HRESULT request_hr = E_PENDING;
    HRESULT edit_hr = E_PENDING;
    HRESULT action_hr = E_PENDING;
    bool start_attempted = false;
    HRESULT start_hr = E_PENDING;
    bool composition_returned = false;
    bool composition_active = false;
};

void trace_composition_edit(TextService* service,
                            const TraceCompositionEditResult& result);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_COMPOSITION_H_
