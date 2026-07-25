// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_composition.h"

#include "text_service.h"

#include <cxxime/stage_trace.h>

namespace cxxime_tsf {

void trace_stage_composition_edit(TextService* service,
                                  const StageCompositionEditResult& result) {
    const bool operation_succeeded = SUCCEEDED(result.request_hr) &&
                                     SUCCEEDED(result.edit_hr) &&
                                     SUCCEEDED(result.action_hr);
    const char* outcome = "failed";
    if (operation_succeeded && result.composition_active) {
        outcome = "active";
    } else if (result.start_attempted && SUCCEEDED(result.start_hr) &&
               !result.composition_returned) {
        outcome = "host_rejected";
    } else if (result.async_fallback && result.action_hr == E_PENDING) {
        outcome = "async_pending";
    }
    cxxime::write_stage_trace("tsf", "tsf.composition", {
        {"input_id", service ? service->stage_input_id() : 0},
        {"composition_id", service ? service->stage_composition_id() : 0},
        {"action", result.action ? result.action : ""},
        {"text_len", result.text_length},
        {"sync_requested", result.sync_requested},
        {"async_fallback", result.async_fallback},
        {"initial_request_hr", static_cast<int64_t>(result.initial_request_hr)},
        {"request_hr", static_cast<int64_t>(result.request_hr)},
        {"edit_hr", static_cast<int64_t>(result.edit_hr)},
        {"action_hr", static_cast<int64_t>(result.action_hr)},
        {"start_attempted", result.start_attempted},
        {"start_hr", static_cast<int64_t>(result.start_hr)},
        {"composition_returned", result.composition_returned},
        {"composition_active", result.composition_active},
        {"result", outcome},
    });
}

} // namespace cxxime_tsf
