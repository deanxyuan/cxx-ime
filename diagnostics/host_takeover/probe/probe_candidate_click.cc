// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <cxxime/stage_trace.h>

#include <algorithm>

namespace cxxime_probe {

bool ProbeApp::candidate_index_at_point(POINT point, UINT* index) const {
    if (!index || !candidate_should_draw() ||
        candidate_element_id_ == TF_INVALID_UIELEMENTID) {
        return false;
    }

    RECT client = {};
    GetClientRect(hwnd_, &client);
    if (point.x < kCandidateLeft || point.x >= client.right - kCandidateRightMargin ||
        point.y < kCandidateTop) {
        return false;
    }

    const int offset = point.y - kCandidateTop;
    const size_t row = static_cast<size_t>(offset / kCandidateRowStride);
    const size_t row_offset = static_cast<size_t>(offset % kCandidateRowStride);
    const size_t visible_count = std::min(candidates_.size(), kMaximumCandidateRows);
    if (row >= visible_count || row_offset >= kCandidateRowHeight) {
        return false;
    }

    *index = static_cast<UINT>(row);
    return true;
}

void ProbeApp::click_candidate(UINT index) {
    const uint64_t composition_id = composition_id_;
    const DWORD element_id = candidate_element_id_;
    const size_t committed_length = committed_.size();
    HRESULT get_hr = E_UNEXPECTED;
    HRESULT behavior_hr = E_UNEXPECTED;
    HRESULT selection_hr = E_UNEXPECTED;
    HRESULT finalize_hr = E_UNEXPECTED;

    ITfUIElement* element = nullptr;
    if (ui_element_mgr_) {
        get_hr = ui_element_mgr_->GetUIElement(element_id, &element);
    }

    ITfCandidateListUIElementBehavior* behavior = nullptr;
    if (SUCCEEDED(get_hr) && element) {
        behavior_hr = element->QueryInterface(
            IID_ITfCandidateListUIElementBehavior,
            reinterpret_cast<void**>(&behavior));
    }
    if (SUCCEEDED(behavior_hr) && behavior) {
        selection_hr = behavior->SetSelection(index);
    }

    const bool finalize_attempted = SUCCEEDED(selection_hr);
    cxxime::write_stage_trace("probe", "probe.candidate_click", {
        {"composition_id", composition_id},
        {"element_id", element_id},
        {"index", index},
        {"committed_len_before", committed_length},
        {"get_hr", static_cast<int64_t>(get_hr)},
        {"behavior_hr", static_cast<int64_t>(behavior_hr)},
        {"set_selection_hr", static_cast<int64_t>(selection_hr)},
        {"finalize_attempted", finalize_attempted},
        {"result", finalize_attempted ? "finalize_call" : "failed"},
    });

    if (finalize_attempted) {
        candidate_click_pending_ = true;
        candidate_click_index_ = index;
        candidate_click_element_id_ = element_id;
        candidate_click_composition_id_ = composition_id;
        candidate_click_committed_length_ = committed_length;
        finalize_hr = behavior->Finalize();
        cxxime::write_stage_trace("probe", "probe.candidate_click_return", {
            {"composition_id", composition_id},
            {"element_id", element_id},
            {"index", index},
            {"finalize_hr", static_cast<int64_t>(finalize_hr)},
            {"result", SUCCEEDED(finalize_hr) ? "success" : "failed"},
        });
        if (FAILED(finalize_hr)) {
            finish_candidate_click("finalize_failed", 0);
        }
    }

    if (behavior) {
        behavior->Release();
    }
    if (element) {
        element->Release();
    }
}

void ProbeApp::finish_candidate_click(const char* result, LONG result_bytes) {
    if (!candidate_click_pending_) {
        return;
    }

    cxxime::write_stage_trace("probe", "probe.candidate_click_result", {
        {"composition_id", candidate_click_composition_id_},
        {"element_id", candidate_click_element_id_},
        {"index", candidate_click_index_},
        {"result_bytes", result_bytes},
        {"committed_len_before", candidate_click_committed_length_},
        {"committed_len_after", committed_.size()},
        {"result", result ? result : ""},
    });
    candidate_click_pending_ = false;
}

} // namespace cxxime_probe
