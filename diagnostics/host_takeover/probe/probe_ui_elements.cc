// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <cxxime/stage_trace.h>

#include <ctffunc.h>
#include <imm.h>

#include <utility>

namespace cxxime_probe {
namespace {

void trace_active_keyboard_profile(const char* trigger) {
    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    const HRESULT manager_result = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&profile_manager));

    TF_INPUTPROCESSORPROFILE profile = {};
    HRESULT profile_result = E_UNEXPECTED;
    if (SUCCEEDED(manager_result) && profile_manager) {
        profile_result = profile_manager->GetActiveProfile(
            GUID_TFCAT_TIP_KEYBOARD, &profile);
        profile_manager->Release();
    }

    const HKL keyboard_layout = GetKeyboardLayout(0);
    cxxime::write_stage_trace("probe", "probe.active_profile", {
        {"trigger", trigger ? trigger : ""},
        {"manager_hr", static_cast<int64_t>(manager_result)},
        {"profile_hr", static_cast<int64_t>(profile_result)},
        {"profile_type", profile.dwProfileType},
        {"langid", profile.langid},
        {"clsid", cxxime::stage_trace_guid(profile.clsid)},
        {"profile_guid", cxxime::stage_trace_guid(profile.guidProfile)},
        {"category", cxxime::stage_trace_guid(profile.catid)},
        {"profile_hkl", reinterpret_cast<uintptr_t>(profile.hkl)},
        {"profile_hkl_substitute", reinterpret_cast<uintptr_t>(profile.hklSubstitute)},
        {"profile_caps", profile.dwCaps},
        {"profile_flags", profile.dwFlags},
        {"thread_hkl", reinterpret_cast<uintptr_t>(keyboard_layout)},
        {"thread_hkl_is_ime", ImmIsIME(keyboard_layout) != FALSE},
        {"result", profile_result == S_OK ? "queried" : "failed"},
    });
}

} // namespace

HRESULT ProbeApp::on_begin_ui_element(DWORD element_id, BOOL* show) {
    if (!show) {
        return E_INVALIDARG;
    }
    *show = FALSE;
    ensure_composition_id();
    trace_active_keyboard_profile("begin_ui_element");
    cxxime::write_stage_trace("probe", "probe.ui_element", {
        {"composition_id", composition_id_},
        {"element_id", element_id},
        {"action", "begin"},
        {"show_external", false},
        {"result", "host_takeover"},
    });
    update_ui_element(element_id, "begin");
    return S_OK;
}

HRESULT ProbeApp::on_update_ui_element(DWORD element_id) {
    cxxime::write_stage_trace("probe", "probe.ui_element", {
        {"composition_id", ensure_composition_id()},
        {"element_id", element_id},
        {"action", "update"},
        {"result", "received"},
    });
    update_ui_element(element_id, "update");
    return S_OK;
}

HRESULT ProbeApp::on_end_ui_element(DWORD element_id) {
    const char* element_type = "unknown";
    if (element_id == candidate_element_id_) {
        element_type = "candidate";
        candidate_element_id_ = TF_INVALID_UIELEMENTID;
        original_candidate_ui_shown_ = false;
        candidate_ui_visibility_pending_ = false;
        reset_candidate_ui_visibility_cycle("candidate_end");
        EnableWindow(original_ui_checkbox_, TRUE);
        candidates_.clear();
        selection_ = 0;
        current_page_ = 0;
    }
    if (element_id == reading_element_id_) {
        element_type = "reading";
        reading_element_id_ = TF_INVALID_UIELEMENTID;
        reading_.clear();
    }
    cxxime::write_stage_trace("probe", "probe.ui_element", {
        {"composition_id", composition_id_},
        {"element_id", element_id},
        {"element_type", element_type},
        {"action", "end"},
        {"result", "received"},
    });
    InvalidateRect(hwnd_, nullptr, TRUE);
    if (candidate_element_id_ == TF_INVALID_UIELEMENTID &&
        reading_element_id_ == TF_INVALID_UIELEMENTID && !composition_active_) {
        composition_id_ = 0;
    }
    return S_OK;
}

void ProbeApp::update_ui_element(DWORD element_id, const char* action) {
    if (!ui_element_mgr_) {
        return;
    }
    ITfUIElement* element = nullptr;
    const HRESULT get_hr = ui_element_mgr_->GetUIElement(element_id, &element);
    if (FAILED(get_hr) || !element) {
        cxxime::write_stage_trace("probe", "probe.ui_element", {
            {"composition_id", composition_id_},
            {"element_id", element_id},
            {"action", action ? action : ""},
            {"hr", static_cast<int64_t>(get_hr)},
            {"result", "get_failed"},
        });
        return;
    }

    ITfCandidateListUIElement* candidate = nullptr;
    const HRESULT candidate_hr = element->QueryInterface(
        IID_ITfCandidateListUIElement, reinterpret_cast<void**>(&candidate));
    if (SUCCEEDED(candidate_hr) && candidate) {
        update_candidate(element, element_id, action);
        candidate->Release();
        element->Release();
        return;
    }

    ITfReadingInformationUIElement* reading = nullptr;
    const HRESULT reading_hr = element->QueryInterface(
        IID_ITfReadingInformationUIElement, reinterpret_cast<void**>(&reading));
    if (SUCCEEDED(reading_hr) && reading) {
        update_reading(element, element_id, action);
        reading->Release();
    } else {
        cxxime::write_stage_trace("probe", "probe.ui_element", {
            {"composition_id", composition_id_},
            {"element_id", element_id},
            {"action", action ? action : ""},
            {"candidate_hr", static_cast<int64_t>(candidate_hr)},
            {"reading_hr", static_cast<int64_t>(reading_hr)},
            {"result", "unknown_type"},
        });
    }
    element->Release();
}

void ProbeApp::update_candidate(ITfUIElement* element, DWORD element_id, const char* action) {
    ITfCandidateListUIElement* candidate = nullptr;
    if (FAILED(element->QueryInterface(
            IID_ITfCandidateListUIElement, reinterpret_cast<void**>(&candidate))) ||
        !candidate) {
        return;
    }

    DWORD flags = 0;
    UINT count = 0;
    UINT selection = 0;
    UINT page_count = 0;
    UINT current_page = 0;
    const HRESULT flags_hr = candidate->GetUpdatedFlags(&flags);
    const HRESULT count_hr = candidate->GetCount(&count);
    const HRESULT selection_hr = candidate->GetSelection(&selection);
    const HRESULT page_query_hr = candidate->GetPageIndex(nullptr, 0, &page_count);
    std::vector<UINT> page_indices(page_count);
    HRESULT page_hr = page_query_hr;
    if (SUCCEEDED(page_query_hr) && page_count > 0) {
        page_hr = candidate->GetPageIndex(page_indices.data(), page_count, &page_count);
    }
    const HRESULT current_page_hr = candidate->GetCurrentPage(&current_page);

    std::vector<std::wstring> next_candidates;
    nlohmann::json lengths = nlohmann::json::array();
    nlohmann::json digests = nlohmann::json::array();
    HRESULT strings_hr = S_OK;
    if (SUCCEEDED(count_hr)) {
        next_candidates.reserve(count);
        for (UINT index = 0; index < count; ++index) {
            BSTR text = nullptr;
            const HRESULT string_hr = candidate->GetString(index, &text);
            if (FAILED(string_hr)) {
                strings_hr = string_hr;
                break;
            }
            std::wstring value(text ? text : L"", text ? SysStringLen(text) : 0);
            lengths.push_back(value.size());
            digests.push_back(cxxime::stage_trace_digest_utf16(value));
            next_candidates.push_back(std::move(value));
            SysFreeString(text);
        }
    }

    ITfCandidateListUIElementBehavior* behavior = nullptr;
    const HRESULT behavior_hr = element->QueryInterface(
        IID_ITfCandidateListUIElementBehavior, reinterpret_cast<void**>(&behavior));
    if (behavior) {
        behavior->Release();
    }
    ITfIntegratableCandidateListUIElement* integratable = nullptr;
    const HRESULT integratable_hr = element->QueryInterface(
        __uuidof(ITfIntegratableCandidateListUIElement),
        reinterpret_cast<void**>(&integratable));
    if (integratable) {
        integratable->Release();
    }

    candidates_ = std::move(next_candidates);
    selection_ = selection;
    current_page_ = current_page;
    candidate_element_id_ = element_id;
    EnableWindow(original_ui_checkbox_, FALSE);
    cxxime::write_stage_trace("probe", "probe.candidate_snapshot", {
        {"composition_id", composition_id_},
        {"element_id", element_id},
        {"action", action ? action : ""},
        {"updated_flags", flags},
        {"count", count},
        {"selection", selection},
        {"page_count", page_count},
        {"current_page", current_page},
        {"page_indices", page_indices},
        {"text_lengths", std::move(lengths)},
        {"text_digests", std::move(digests)},
        {"flags_hr", static_cast<int64_t>(flags_hr)},
        {"count_hr", static_cast<int64_t>(count_hr)},
        {"selection_hr", static_cast<int64_t>(selection_hr)},
        {"page_hr", static_cast<int64_t>(page_hr)},
        {"current_page_hr", static_cast<int64_t>(current_page_hr)},
        {"strings_hr", static_cast<int64_t>(strings_hr)},
        {"behavior_hr", static_cast<int64_t>(behavior_hr)},
        {"integratable_hr", static_cast<int64_t>(integratable_hr)},
        {"result", SUCCEEDED(count_hr) && SUCCEEDED(strings_hr) ? "read" : "failed"},
    });
    if (candidate_ui_visibility_pending_) {
        apply_candidate_ui_visibility(action ? action : "candidate_update");
    }
    schedule_candidate_ui_visibility_cycle(action ? action : "candidate_update");
    trace_imm_candidate_snapshot("ui_element", element_id, action);
    candidate->Release();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void ProbeApp::update_reading(ITfUIElement* element, DWORD element_id, const char* action) {
    ITfReadingInformationUIElement* reading = nullptr;
    if (FAILED(element->QueryInterface(
            IID_ITfReadingInformationUIElement, reinterpret_cast<void**>(&reading))) ||
        !reading) {
        return;
    }
    BSTR text = nullptr;
    const HRESULT string_hr = reading->GetString(&text);
    if (SUCCEEDED(string_hr)) {
        reading_.assign(text ? text : L"", text ? SysStringLen(text) : 0);
    }
    SysFreeString(text);
    reading_element_id_ = element_id;
    cxxime::write_stage_trace("probe", "probe.reading_snapshot", {
        {"composition_id", composition_id_},
        {"element_id", element_id},
        {"action", action ? action : ""},
        {"text_len", reading_.size()},
        {"text_digest", cxxime::stage_trace_digest_utf16(reading_)},
        {"string_hr", static_cast<int64_t>(string_hr)},
        {"result", SUCCEEDED(string_hr) ? "read" : "failed"},
    });
    reading->Release();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

} // namespace cxxime_probe
