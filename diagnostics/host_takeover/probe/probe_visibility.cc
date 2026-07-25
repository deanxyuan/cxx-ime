// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <cxxime/stage_trace.h>

namespace cxxime_probe {
namespace {

constexpr UINT kShowDelayMs = 1000;
constexpr UINT kHideDelayMs = 1500;

const char* visibility_cycle_name(CandidateUiVisibilityCycle state) {
    switch (state) {
    case CandidateUiVisibilityCycle::disabled:
        return "disabled";
    case CandidateUiVisibilityCycle::armed:
        return "armed";
    case CandidateUiVisibilityCycle::waiting_to_show:
        return "waiting_to_show";
    case CandidateUiVisibilityCycle::waiting_to_hide:
        return "waiting_to_hide";
    case CandidateUiVisibilityCycle::completed:
        return "completed";
    }
    return "unknown";
}

} // namespace

bool ProbeApp::apply_candidate_ui_visibility(const char* trigger) {
    if (!ui_element_mgr_ || candidate_element_id_ == TF_INVALID_UIELEMENTID) {
        original_candidate_ui_shown_ = false;
        candidate_ui_visibility_pending_ = true;
        cxxime::write_stage_trace("probe", "probe.ui_element_visibility", {
            {"composition_id", composition_id_},
            {"element_id", candidate_element_id_},
            {"trigger", trigger ? trigger : ""},
            {"requested_show", original_candidate_ui_requested_},
            {"actual_show", false},
            {"result", "no_candidate_element"},
        });
        InvalidateRect(hwnd_, nullptr, TRUE);
        return false;
    }

    ITfUIElement* element = nullptr;
    const HRESULT get_hr = ui_element_mgr_->GetUIElement(candidate_element_id_, &element);
    if (FAILED(get_hr) || !element) {
        original_candidate_ui_shown_ = false;
        candidate_ui_visibility_pending_ = true;
        cxxime::write_stage_trace("probe", "probe.ui_element_visibility", {
            {"composition_id", composition_id_},
            {"element_id", candidate_element_id_},
            {"trigger", trigger ? trigger : ""},
            {"requested_show", original_candidate_ui_requested_},
            {"actual_show", false},
            {"get_hr", static_cast<int64_t>(get_hr)},
            {"result", "get_failed"},
        });
        InvalidateRect(hwnd_, nullptr, TRUE);
        return false;
    }

    const HRESULT show_hr = element->Show(original_candidate_ui_requested_ ? TRUE : FALSE);
    BOOL shown = FALSE;
    const HRESULT is_shown_hr = element->IsShown(&shown);
    element->Release();

    original_candidate_ui_shown_ = SUCCEEDED(is_shown_hr) && shown != FALSE;
    candidate_ui_visibility_pending_ =
        FAILED(show_hr) || FAILED(is_shown_hr) ||
        original_candidate_ui_shown_ != original_candidate_ui_requested_;
    cxxime::write_stage_trace("probe", "probe.ui_element_visibility", {
        {"composition_id", composition_id_},
        {"element_id", candidate_element_id_},
        {"trigger", trigger ? trigger : ""},
        {"requested_show", original_candidate_ui_requested_},
        {"actual_show", original_candidate_ui_shown_},
        {"show_hr", static_cast<int64_t>(show_hr)},
        {"is_shown_hr", static_cast<int64_t>(is_shown_hr)},
        {"result", candidate_ui_visibility_pending_ ? "pending" : "applied"},
    });
    InvalidateRect(hwnd_, nullptr, TRUE);
    return !candidate_ui_visibility_pending_;
}

void ProbeApp::set_candidate_ui_visibility_cycle(bool enabled, const char* trigger) {
    KillTimer(hwnd_, kCandidateUiVisibilityTimerId);
    candidate_ui_visibility_cycle_enabled_ = enabled;
    candidate_ui_visibility_cycle_ = enabled ? CandidateUiVisibilityCycle::armed
                                             : CandidateUiVisibilityCycle::disabled;
    original_candidate_ui_requested_ = false;
    candidate_ui_visibility_pending_ = false;
    cxxime::write_stage_trace("probe", "probe.ui_element_visibility_cycle", {
        {"composition_id", composition_id_},
        {"trigger", trigger ? trigger : ""},
        {"state", visibility_cycle_name(candidate_ui_visibility_cycle_)},
        {"result", "changed"},
    });
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void ProbeApp::schedule_candidate_ui_visibility_cycle(const char* trigger) {
    if (!candidate_ui_visibility_cycle_enabled_ ||
        (candidate_ui_visibility_cycle_ != CandidateUiVisibilityCycle::armed &&
         candidate_ui_visibility_cycle_ != CandidateUiVisibilityCycle::waiting_to_show)) {
        return;
    }

    candidate_ui_visibility_cycle_ = CandidateUiVisibilityCycle::waiting_to_show;
    const UINT_PTR timer = SetTimer(hwnd_, kCandidateUiVisibilityTimerId, kShowDelayMs, nullptr);
    cxxime::write_stage_trace("probe", "probe.ui_element_visibility_cycle", {
        {"composition_id", composition_id_},
        {"element_id", candidate_element_id_},
        {"trigger", trigger ? trigger : ""},
        {"state", visibility_cycle_name(candidate_ui_visibility_cycle_)},
        {"delay_ms", kShowDelayMs},
        {"result", timer != 0 ? "scheduled" : "timer_failed"},
    });
    if (timer == 0) {
        candidate_ui_visibility_cycle_ = CandidateUiVisibilityCycle::armed;
    }
}

void ProbeApp::advance_candidate_ui_visibility_cycle() {
    KillTimer(hwnd_, kCandidateUiVisibilityTimerId);
    if (candidate_ui_visibility_cycle_ == CandidateUiVisibilityCycle::waiting_to_show) {
        original_candidate_ui_requested_ = true;
        candidate_ui_visibility_pending_ = true;
        apply_candidate_ui_visibility("automatic_show");
        if (candidate_element_id_ == TF_INVALID_UIELEMENTID ||
            candidate_ui_visibility_cycle_ != CandidateUiVisibilityCycle::waiting_to_show) {
            return;
        }
        candidate_ui_visibility_cycle_ = CandidateUiVisibilityCycle::waiting_to_hide;
        const UINT_PTR timer =
            SetTimer(hwnd_, kCandidateUiVisibilityTimerId, kHideDelayMs, nullptr);
        if (timer == 0) {
            original_candidate_ui_requested_ = false;
            candidate_ui_visibility_pending_ = true;
            apply_candidate_ui_visibility("automatic_hide_timer_fallback");
            candidate_ui_visibility_cycle_ = CandidateUiVisibilityCycle::completed;
        }
    } else if (candidate_ui_visibility_cycle_ ==
               CandidateUiVisibilityCycle::waiting_to_hide) {
        original_candidate_ui_requested_ = false;
        candidate_ui_visibility_pending_ = true;
        apply_candidate_ui_visibility("automatic_hide");
        candidate_ui_visibility_cycle_ = CandidateUiVisibilityCycle::completed;
    } else {
        return;
    }

    cxxime::write_stage_trace("probe", "probe.ui_element_visibility_cycle", {
        {"composition_id", composition_id_},
        {"element_id", candidate_element_id_},
        {"state", visibility_cycle_name(candidate_ui_visibility_cycle_)},
        {"result", "advanced"},
    });
}

void ProbeApp::reset_candidate_ui_visibility_cycle(const char* trigger) {
    KillTimer(hwnd_, kCandidateUiVisibilityTimerId);
    original_candidate_ui_requested_ = false;
    candidate_ui_visibility_cycle_ = candidate_ui_visibility_cycle_enabled_
                                         ? CandidateUiVisibilityCycle::armed
                                         : CandidateUiVisibilityCycle::disabled;
    cxxime::write_stage_trace("probe", "probe.ui_element_visibility_cycle", {
        {"composition_id", composition_id_},
        {"trigger", trigger ? trigger : ""},
        {"state", visibility_cycle_name(candidate_ui_visibility_cycle_)},
        {"result", "reset"},
    });
}

} // namespace cxxime_probe
