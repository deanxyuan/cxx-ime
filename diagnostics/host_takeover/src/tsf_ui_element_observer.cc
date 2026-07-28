// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_ui_element_observer.h"
#include "tsf_ui_element_identity.h"

#include <cxxime/stage_trace.h>

#include <ctffunc.h>

#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace cxxime_tsf {
namespace {

constexpr UINT kMaximumTracedStrings = 64;
constexpr UINT kMaximumTracedPages = 64;

ITfSource* g_ui_element_source = nullptr;
ITfUIElementSink* g_ui_element_sink = nullptr;
DWORD g_ui_element_cookie = TF_INVALID_COOKIE;
DWORD g_ui_element_thread_id = 0;

void add_active_profile(nlohmann::json& fields) {
    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    const HRESULT manager_hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&profile_manager));
    TF_INPUTPROCESSORPROFILE profile = {};
    const HRESULT profile_hr = profile_manager
        ? profile_manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)
        : E_NOINTERFACE;
    if (profile_manager) {
        profile_manager->Release();
    }

    fields["profile_manager_hr"] = static_cast<int64_t>(manager_hr);
    fields["profile_hr"] = static_cast<int64_t>(profile_hr);
    fields["profile_type"] = profile.dwProfileType;
    fields["profile_clsid"] = cxxime::stage_trace_guid(profile.clsid);
    fields["profile_guid"] = cxxime::stage_trace_guid(profile.guidProfile);
    fields["profile_hkl"] = reinterpret_cast<uintptr_t>(profile.hkl);
}

void add_element_fields(ITfUIElement* element, nlohmann::json& fields) {
    BSTR description = nullptr;
    const HRESULT description_hr = element->GetDescription(&description);
    const UINT description_length = description ? SysStringLen(description) : 0;
    const std::string description_digest = cxxime::stage_trace_digest_utf16(
        description ? description : L"", description_length);

    GUID element_guid = {};
    const HRESULT guid_hr = element->GetGUID(&element_guid);
    BOOL shown = FALSE;
    const HRESULT shown_hr = element->IsShown(&shown);

    fields["description_hr"] = static_cast<int64_t>(description_hr);
    fields["description_len"] = description_length;
    fields["description_digest"] = description_digest;
    fields["guid_hr"] = static_cast<int64_t>(guid_hr);
    fields["element_guid"] = cxxime::stage_trace_guid(element_guid);
    fields["is_shown_hr"] = static_cast<int64_t>(shown_hr);
    fields["is_shown"] = shown != FALSE;
    SysFreeString(description);
}

void trace_candidate_snapshot(ITfThreadMgr* thread_mgr,
                              ITfUIElement* element,
                              ITfCandidateListUIElement* candidate,
                              DWORD element_id,
                              const char* action) {
    DWORD updated_flags = 0;
    const HRESULT flags_hr = candidate->GetUpdatedFlags(&updated_flags);

    ITfDocumentMgr* document_mgr = nullptr;
    const HRESULT document_mgr_hr = candidate->GetDocumentMgr(&document_mgr);
    ITfDocumentMgr* focused_document_mgr = nullptr;
    const HRESULT focused_document_mgr_hr = thread_mgr
        ? thread_mgr->GetFocus(&focused_document_mgr)
        : E_POINTER;

    UINT candidate_count = 0;
    UINT selection = 0;
    const HRESULT count_hr = candidate->GetCount(&candidate_count);
    const HRESULT selection_hr = candidate->GetSelection(&selection);

    UINT page_count = 0;
    const HRESULT page_query_hr = candidate->GetPageIndex(nullptr, 0, &page_count);
    const UINT pages_to_read = std::min(page_count, kMaximumTracedPages);
    std::vector<UINT> page_indices(pages_to_read);
    UINT returned_page_count = page_count;
    HRESULT page_hr = page_query_hr;
    if (SUCCEEDED(page_query_hr) && pages_to_read > 0) {
        returned_page_count = pages_to_read;
        page_hr = candidate->GetPageIndex(
            page_indices.data(), pages_to_read, &returned_page_count);
        page_indices.resize(std::min(returned_page_count, pages_to_read));
    }

    UINT current_page = 0;
    const HRESULT current_page_hr = candidate->GetCurrentPage(&current_page);

    nlohmann::json text_lengths = nlohmann::json::array();
    nlohmann::json text_digests = nlohmann::json::array();
    const UINT strings_to_read = SUCCEEDED(count_hr)
        ? std::min(candidate_count, kMaximumTracedStrings)
        : 0;
    HRESULT strings_hr = S_OK;
    UINT strings_read = 0;
    for (UINT index = 0; index < strings_to_read; ++index) {
        BSTR text = nullptr;
        const HRESULT string_hr = candidate->GetString(index, &text);
        if (FAILED(string_hr)) {
            strings_hr = string_hr;
            SysFreeString(text);
            break;
        }
        const UINT length = text ? SysStringLen(text) : 0;
        text_lengths.push_back(length);
        text_digests.push_back(cxxime::stage_trace_digest_utf16(
            text ? text : L"", length));
        SysFreeString(text);
        ++strings_read;
    }

    ITfCandidateListUIElementBehavior* behavior = nullptr;
    const HRESULT behavior_hr = element->QueryInterface(
        IID_ITfCandidateListUIElementBehavior,
        reinterpret_cast<void**>(&behavior));
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

    nlohmann::json fields = {
        {"action", action ? action : ""},
        {"element_id", element_id},
        {"element_type", "candidate"},
        {"updated_flags", updated_flags},
        {"flags_hr", static_cast<int64_t>(flags_hr)},
        {"document_mgr_hr", static_cast<int64_t>(document_mgr_hr)},
        {"document_mgr", reinterpret_cast<uintptr_t>(document_mgr)},
        {"document_mgr_present", document_mgr != nullptr},
        {"focused_document_mgr_hr", static_cast<int64_t>(focused_document_mgr_hr)},
        {"focused_document_mgr", reinterpret_cast<uintptr_t>(focused_document_mgr)},
        {"document_mgr_matches_focus", document_mgr && document_mgr == focused_document_mgr},
        {"count", candidate_count},
        {"count_hr", static_cast<int64_t>(count_hr)},
        {"selection", selection},
        {"selection_hr", static_cast<int64_t>(selection_hr)},
        {"page_count", page_count},
        {"page_query_hr", static_cast<int64_t>(page_query_hr)},
        {"page_hr", static_cast<int64_t>(page_hr)},
        {"page_indices", page_indices},
        {"pages_truncated", page_count > kMaximumTracedPages},
        {"current_page", current_page},
        {"current_page_hr", static_cast<int64_t>(current_page_hr)},
        {"strings_read", strings_read},
        {"strings_hr", static_cast<int64_t>(strings_hr)},
        {"strings_truncated", candidate_count > kMaximumTracedStrings},
        {"text_lengths", std::move(text_lengths)},
        {"text_digests", std::move(text_digests)},
        {"behavior_hr", static_cast<int64_t>(behavior_hr)},
        {"integratable_hr", static_cast<int64_t>(integratable_hr)},
        {"thread_id", GetCurrentThreadId()},
        {"result", SUCCEEDED(count_hr) && SUCCEEDED(strings_hr) ? "read" : "incomplete"},
    };
    add_element_fields(element, fields);
    add_stage_ui_element_identity_fields(element, fields);
    cxxime::write_stage_trace("tsf", "host.candidate_ui_element", std::move(fields));

    if (focused_document_mgr) {
        focused_document_mgr->Release();
    }
    if (document_mgr) {
        document_mgr->Release();
    }
}

class StageUiElementSink final : public ITfUIElementSink {
public:
    StageUiElementSink(ITfThreadMgr* thread_mgr, ITfUIElementMgr* ui_element_mgr)
        : thread_mgr_(thread_mgr), ui_element_mgr_(ui_element_mgr) {
        thread_mgr_->AddRef();
        ui_element_mgr_->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (!object) {
            return E_INVALIDARG;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElementSink)) {
            *object = static_cast<ITfUIElementSink*>(this);
        }
        if (!*object) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return ++refs_;
    }

    STDMETHODIMP_(ULONG) Release() override {
        const ULONG refs = --refs_;
        if (refs == 0) {
            delete this;
        }
        return refs;
    }

    STDMETHODIMP BeginUIElement(DWORD element_id, BOOL* show) override {
        nlohmann::json fields = {
            {"action", "begin"},
            {"element_id", element_id},
            {"show_present", show != nullptr},
            {"show_on_entry", show && *show != FALSE},
            {"show_unchanged", true},
            {"thread_id", GetCurrentThreadId()},
            {"result", "observed"},
        };
        add_active_profile(fields);
        cxxime::write_stage_trace("tsf", "host.ui_element", std::move(fields));
        trace_snapshot(element_id, "begin");
        return S_OK;
    }

    STDMETHODIMP UpdateUIElement(DWORD element_id) override {
        cxxime::write_stage_trace("tsf", "host.ui_element", {
            {"action", "update"},
            {"element_id", element_id},
            {"thread_id", GetCurrentThreadId()},
            {"result", "observed"},
        });
        trace_snapshot(element_id, "update");
        return S_OK;
    }

    STDMETHODIMP EndUIElement(DWORD element_id) override {
        cxxime::write_stage_trace("tsf", "host.ui_element", {
            {"action", "end"},
            {"element_id", element_id},
            {"thread_id", GetCurrentThreadId()},
            {"result", "observed"},
        });
        return S_OK;
    }

private:
    ~StageUiElementSink() {
        ui_element_mgr_->Release();
        thread_mgr_->Release();
    }

    void trace_snapshot(DWORD element_id, const char* action) {
        ITfUIElement* element = nullptr;
        const HRESULT get_hr = ui_element_mgr_->GetUIElement(element_id, &element);
        if (FAILED(get_hr) || !element) {
            cxxime::write_stage_trace("tsf", "host.ui_element_snapshot", {
                {"action", action ? action : ""},
                {"element_id", element_id},
                {"get_hr", static_cast<int64_t>(get_hr)},
                {"result", "get_failed"},
            });
            return;
        }

        ITfCandidateListUIElement* candidate = nullptr;
        const HRESULT candidate_hr = element->QueryInterface(
            IID_ITfCandidateListUIElement,
            reinterpret_cast<void**>(&candidate));
        if (SUCCEEDED(candidate_hr) && candidate) {
            trace_candidate_snapshot(
                thread_mgr_, element, candidate, element_id, action);
            candidate->Release();
            element->Release();
            return;
        }

        ITfReadingInformationUIElement* reading = nullptr;
        const HRESULT reading_hr = element->QueryInterface(
            IID_ITfReadingInformationUIElement,
            reinterpret_cast<void**>(&reading));
        if (reading) {
            reading->Release();
        }
        nlohmann::json fields = {
            {"action", action ? action : ""},
            {"element_id", element_id},
            {"candidate_hr", static_cast<int64_t>(candidate_hr)},
            {"reading_hr", static_cast<int64_t>(reading_hr)},
            {"element_type", SUCCEEDED(reading_hr) ? "reading" : "unknown"},
            {"result", "read"},
        };
        add_element_fields(element, fields);
        cxxime::write_stage_trace("tsf", "host.ui_element_snapshot", std::move(fields));
        element->Release();
    }

    std::atomic<ULONG> refs_{1};
    ITfThreadMgr* thread_mgr_ = nullptr;
    ITfUIElementMgr* ui_element_mgr_ = nullptr;
};

} // namespace

void start_stage_ui_element_observer(ITfThreadMgr* thread_mgr, DWORD activate_flags) {
    if ((activate_flags & TF_TMF_UIELEMENTENABLEDONLY) == 0) {
        cxxime::write_stage_trace("tsf", "host.ui_element_observer", {
            {"action", "start"},
            {"activate_flags", activate_flags},
            {"thread_id", GetCurrentThreadId()},
            {"result", "skipped_standard_host"},
        });
        return;
    }

    if (g_ui_element_cookie != TF_INVALID_COOKIE) {
        cxxime::write_stage_trace("tsf", "host.ui_element_observer", {
            {"action", "start"},
            {"installed_thread_id", g_ui_element_thread_id},
            {"current_thread_id", GetCurrentThreadId()},
            {"result", "already_installed"},
        });
        return;
    }

    ITfUIElementMgr* ui_element_mgr = nullptr;
    const HRESULT manager_hr = thread_mgr
        ? thread_mgr->QueryInterface(
            IID_ITfUIElementMgr, reinterpret_cast<void**>(&ui_element_mgr))
        : E_POINTER;
    ITfSource* source = nullptr;
    const HRESULT source_hr = ui_element_mgr
        ? ui_element_mgr->QueryInterface(
            IID_ITfSource, reinterpret_cast<void**>(&source))
        : E_NOINTERFACE;
    StageUiElementSink* sink = nullptr;
    HRESULT advise_hr = E_OUTOFMEMORY;
    DWORD cookie = TF_INVALID_COOKIE;
    if (source && thread_mgr) {
        sink = new (std::nothrow) StageUiElementSink(thread_mgr, ui_element_mgr);
        if (sink) {
            advise_hr = source->AdviseSink(
                IID_ITfUIElementSink, static_cast<ITfUIElementSink*>(sink), &cookie);
        }
    }

    const bool installed = SUCCEEDED(advise_hr) && cookie != TF_INVALID_COOKIE;
    if (installed) {
        g_ui_element_source = source;
        g_ui_element_sink = sink;
        g_ui_element_cookie = cookie;
        g_ui_element_thread_id = GetCurrentThreadId();
    } else {
        if (sink) {
            sink->Release();
        }
        if (source) {
            source->Release();
        }
    }
    if (ui_element_mgr) {
        ui_element_mgr->Release();
    }

    cxxime::write_stage_trace("tsf", "host.ui_element_observer", {
        {"action", "start"},
        {"manager_hr", static_cast<int64_t>(manager_hr)},
        {"source_hr", static_cast<int64_t>(source_hr)},
        {"advise_hr", static_cast<int64_t>(advise_hr)},
        {"cookie", cookie},
        {"thread_id", GetCurrentThreadId()},
        {"result", installed ? "installed" : "failed"},
    });
}

} // namespace cxxime_tsf
