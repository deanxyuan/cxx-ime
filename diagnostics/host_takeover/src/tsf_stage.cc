// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_stage.h"

#include "tsf_host_callsite.h"
#include "tsf_sdl_runtime.h"

#include "candidate_ui_element.h"
#include "globals.h"
#include "text_service.h"

#include <cxxime/stage_trace.h>

#include <immdev.h>

#include <cstring>
#include <utility>

namespace cxxime_tsf {

namespace {

void trace_ui(TextService* service,
              const char* event,
              const char* element_type,
              DWORD element_id,
              nlohmann::json fields) {
    fields["input_id"] = service ? service->stage_input_id() : 0;
    fields["composition_id"] = service ? service->stage_composition_id() : 0;
    fields["element_type"] = element_type ? element_type : "";
    fields["element_id"] = element_id;
    cxxime::write_stage_trace("tsf", event, std::move(fields));
}

std::string window_class_utf8(HWND hwnd) {
    wchar_t class_name[256] = {};
    if (!hwnd || !GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name))) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, class_name, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, &result[0], required, nullptr, nullptr);
    result.pop_back();
    return result;
}

struct ImmTextReadback {
    LONG bytes = IMM_ERROR_GENERAL;
    std::wstring text;
};

ImmTextReadback read_imm_text(HIMC himc, DWORD index) {
    ImmTextReadback result;
    if (!himc) {
        return result;
    }
    result.bytes = ImmGetCompositionStringW(himc, index, nullptr, 0);
    if (result.bytes <= 0) {
        return result;
    }
    result.text.resize(static_cast<size_t>(result.bytes) / sizeof(wchar_t));
    const LONG copied = ImmGetCompositionStringW(
        himc, index, &result.text[0], static_cast<DWORD>(result.bytes));
    if (copied < 0) {
        result.text.clear();
    } else {
        result.text.resize(static_cast<size_t>(copied) / sizeof(wchar_t));
    }
    return result;
}

struct ImmCandidateReadback {
    DWORD bytes = 0;
    DWORD count = 0;
    DWORD selection = 0;
    DWORD page_start = 0;
    DWORD page_size = 0;
    bool valid = false;
    nlohmann::json digests = nlohmann::json::array();
};

ImmCandidateReadback read_imm_candidates(HIMC himc) {
    ImmCandidateReadback result;
    if (!himc) {
        return result;
    }
    result.bytes = ImmGetCandidateListW(himc, 0, nullptr, 0);
    if (result.bytes < offsetof(CANDIDATELIST, dwOffset)) {
        return result;
    }

    std::vector<BYTE> storage(result.bytes);
    const DWORD copied = ImmGetCandidateListW(
        himc, 0, reinterpret_cast<LPCANDIDATELIST>(storage.data()), result.bytes);
    if (copied < offsetof(CANDIDATELIST, dwOffset)) {
        return result;
    }

    const auto* list = reinterpret_cast<const CANDIDATELIST*>(storage.data());
    const size_t offset_bytes = offsetof(CANDIDATELIST, dwOffset) +
                                static_cast<size_t>(list->dwCount) * sizeof(DWORD);
    if (offset_bytes > copied) {
        return result;
    }

    result.count = list->dwCount;
    result.selection = list->dwSelection;
    result.page_start = list->dwPageStart;
    result.page_size = list->dwPageSize;
    for (DWORD index = 0; index < list->dwCount; ++index) {
        const DWORD offset = list->dwOffset[index];
        if (offset >= copied || (offset % sizeof(wchar_t)) != 0) {
            return result;
        }
        const wchar_t* text = reinterpret_cast<const wchar_t*>(storage.data() + offset);
        const size_t max_length = (copied - offset) / sizeof(wchar_t);
        const size_t length = wcsnlen_s(text, max_length);
        if (length == max_length) {
            return result;
        }
        result.digests.push_back(cxxime::stage_trace_digest_utf16(text, length));
    }
    result.valid = true;
    return result;
}

nlohmann::json text_digests(const std::vector<std::wstring>& values) {
    nlohmann::json digests = nlohmann::json::array();
    for (const auto& value : values) {
        digests.push_back(cxxime::stage_trace_digest_utf16(value));
    }
    return digests;
}

} // namespace

void trace_stage_runtime_activate(DWORD activate_flags, TfClientId client_id) {
    TF_INPUTPROCESSORPROFILE profile = {};
    HRESULT profile_manager_hr = E_UNEXPECTED;
    HRESULT profile_hr = E_UNEXPECTED;
    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    profile_manager_hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&profile_manager));
    if (SUCCEEDED(profile_manager_hr) && profile_manager) {
        profile_hr = profile_manager->GetProfile(
            TF_PROFILETYPE_INPUTPROCESSOR, TEXTSERVICE_LANGID_HANS,
            c_clsidTextService, c_guidProfile, nullptr, &profile);
        profile_manager->Release();
    }
    const HKL keyboard_layout = GetKeyboardLayout(0);

    cxxime::write_stage_trace("tsf", "runtime.component_status", {
        {"name", "cxxime-tsf"},
        {"result", "loaded"},
    });
    cxxime::write_stage_trace("tsf", "runtime.activate", {
        {"activate_flags", activate_flags},
        {"client_id", client_id},
        {"hkl", reinterpret_cast<uintptr_t>(keyboard_layout)},
        {"hkl_is_ime", ImmIsIME(keyboard_layout) != FALSE},
        {"profile_query_hr", static_cast<int64_t>(profile_hr)},
        {"profile_manager_hr", static_cast<int64_t>(profile_manager_hr)},
        {"profile_type", profile.dwProfileType},
        {"profile_hkl", reinterpret_cast<uintptr_t>(profile.hkl)},
        {"profile_hkl_substitute", reinterpret_cast<uintptr_t>(profile.hklSubstitute)},
        {"profile_caps", profile.dwCaps},
        {"profile_flags", profile.dwFlags},
        {"ui_element_only", (activate_flags & TF_TMF_UIELEMENTENABLEDONLY) != 0},
        {"result", "success"},
    });
    if ((activate_flags & TF_TMF_UIELEMENTENABLEDONLY) != 0) {
        trace_stage_sdl_runtime();
    }
}

void trace_stage_key_route(uint64_t input_id,
                           uint64_t composition_id,
                           uint32_t virtual_key,
                           uint32_t engine_calls,
                           const char* result,
                           const char* reason) {
    nlohmann::json fields = {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"owner", "tsf"},
        {"vk", virtual_key},
        {"engine_calls", engine_calls},
        {"result", result ? result : ""},
    };
    if (reason) {
        fields["reason"] = reason;
    }
    cxxime::write_stage_trace("tsf", "key.route", std::move(fields));
}

void trace_stage_context(uint64_t input_id,
                         uint64_t composition_id,
                         ITfContext* input_context,
                         ITfThreadMgr* thread_mgr,
                         const char* composition_transport) {
    ITfDocumentMgr* document_mgr = nullptr;
    ITfContext* top_context = nullptr;
    ITfInsertAtSelection* insert_at_selection = nullptr;
    ITfContextComposition* context_composition = nullptr;
    const HRESULT focus_hr = thread_mgr ? thread_mgr->GetFocus(&document_mgr) : E_POINTER;
    const HRESULT top_hr = document_mgr ? document_mgr->GetTop(&top_context) : E_POINTER;
    const HRESULT insert_hr = input_context
        ? input_context->QueryInterface(
            IID_ITfInsertAtSelection, reinterpret_cast<void**>(&insert_at_selection))
        : E_POINTER;
    const HRESULT composition_hr = input_context
        ? input_context->QueryInterface(
            IID_ITfContextComposition, reinterpret_cast<void**>(&context_composition))
        : E_POINTER;
    cxxime::write_stage_trace("tsf", "tsf.context", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"context_present", input_context != nullptr},
        {"document_mgr_present", document_mgr != nullptr},
        {"top_context_present", top_context != nullptr},
        {"input_is_top_context", input_context && input_context == top_context},
        {"focus_hr", static_cast<int64_t>(focus_hr)},
        {"top_hr", static_cast<int64_t>(top_hr)},
        {"insert_at_selection_hr", static_cast<int64_t>(insert_hr)},
        {"context_composition_hr", static_cast<int64_t>(composition_hr)},
        {"composition_transport", composition_transport ? composition_transport : ""},
    });
    if (top_context) {
        top_context->Release();
    }
    if (document_mgr) {
        document_mgr->Release();
    }
    if (insert_at_selection) {
        insert_at_selection->Release();
    }
    if (context_composition) {
        context_composition->Release();
    }
}

void trace_stage_key_result(uint64_t input_id,
                            uint64_t composition_id,
                            uint32_t virtual_key,
                            bool eaten,
                            size_t preedit_length,
                            uint32_t candidate_count,
                            size_t commit_length,
                            const char* result) {
    cxxime::write_stage_trace("tsf", "key.result", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"vk", virtual_key},
        {"eaten", eaten},
        {"preedit_len", preedit_length},
        {"candidate_count", candidate_count},
        {"commit_len", commit_length},
        {"result", result ? result : ""},
    });
}

void trace_stage_composition_end(uint64_t input_id,
                                 uint64_t composition_id,
                                 const char* reason) {
    if (composition_id == 0) {
        return;
    }
    cxxime::write_stage_trace("tsf", "composition.lifecycle", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"action", "end"},
        {"reason", reason ? reason : ""},
        {"result", "closed"},
    });
}

void trace_stage_ui_query(TextService* service,
                          const char* element_type,
                          REFIID iid,
                          HRESULT result) {
    trace_ui(service, "ui_element.query_interface", element_type, TF_INVALID_UIELEMENTID, {
        {"iid", cxxime::stage_trace_guid(iid)},
        {"hr", static_cast<int64_t>(result)},
        {"result", SUCCEEDED(result) ? "supported" : "unsupported"},
    });
}

void trace_stage_ui_show(TextService* service,
                         const char* element_type,
                         DWORD element_id,
                         bool requested_show,
                         bool actual_show,
                         HRESULT result) {
    trace_ui(service, "ui_element.show", element_type, element_id, {
        {"requested_show", requested_show},
        {"actual_show", actual_show},
        {"hr", static_cast<int64_t>(result)},
        {"result", SUCCEEDED(result) ? "success" : "failed"},
    });
}

void trace_stage_ui_get_number(TextService* service,
                               const char* element_type,
                               DWORD element_id,
                               const char* method,
                               const char* field,
                               uint64_t value,
                               HRESULT result) {
    if (method && std::strcmp(method, "GetUpdatedFlags") == 0) {
        trace_stage_host_ui_callsite(
            "ITfCandidateListUIElement::GetUpdatedFlags",
            element_id != TF_INVALID_UIELEMENTID);
    }
    nlohmann::json fields = {
        {"method", method ? method : ""},
        {"hr", static_cast<int64_t>(result)},
    };
    fields[field ? field : "value"] = value;
    trace_ui(service, "ui_element.get", element_type, element_id, std::move(fields));
}

void trace_stage_ui_get_bool(TextService* service,
                             const char* element_type,
                             DWORD element_id,
                             const char* method,
                             const char* field,
                             bool value,
                             HRESULT result) {
    nlohmann::json fields = {
        {"method", method ? method : ""},
        {"hr", static_cast<int64_t>(result)},
    };
    fields[field ? field : "value"] = value;
    trace_ui(service, "ui_element.get", element_type, element_id, std::move(fields));
}

void trace_stage_ui_get_presence(TextService* service,
                                 const char* element_type,
                                 DWORD element_id,
                                 const char* method,
                                 const char* field,
                                 bool present,
                                 HRESULT result) {
    nlohmann::json fields = {
        {"method", method ? method : ""},
        {"hr", static_cast<int64_t>(result)},
    };
    fields[field ? field : "present"] = present;
    trace_ui(service, "ui_element.get", element_type, element_id, std::move(fields));
}

void trace_stage_candidate_get_string(TextService* service,
                                      DWORD element_id,
                                      UINT index,
                                      const std::wstring* text,
                                      HRESULT result) {
    nlohmann::json fields = {
        {"method", "GetString"},
        {"index", index},
        {"text_len", text ? text->size() : 0},
        {"hr", static_cast<int64_t>(result)},
    };
    if (text) {
        fields["text_digest"] = cxxime::stage_trace_digest_utf16(*text);
    }
    trace_ui(service, "ui_element.get", "candidate", element_id, std::move(fields));
}

void trace_stage_candidate_get_page(TextService* service,
                                    DWORD element_id,
                                    UINT buffer_size,
                                    UINT page_count,
                                    bool query_only,
                                    UINT first_page_index,
                                    HRESULT result) {
    trace_ui(service, "ui_element.get", "candidate", element_id, {
        {"method", "GetPageIndex"},
        {"buffer_size", buffer_size},
        {"page_count", page_count},
        {"query_only", query_only},
        {"first_page_index", first_page_index},
        {"hr", static_cast<int64_t>(result)},
    });
}

void trace_stage_candidate_page_set(TextService* service,
                                    DWORD element_id,
                                    UINT page_count,
                                    UINT first_page_index,
                                    HRESULT result) {
    trace_ui(service, "candidate.behavior", "candidate", element_id, {
        {"method", "SetPageIndex"},
        {"page_count", page_count},
        {"first_page_index", first_page_index},
        {"hr", static_cast<int64_t>(result)},
    });
}

void trace_stage_candidate_behavior_number(TextService* service,
                                           DWORD element_id,
                                           const char* method,
                                           const char* field,
                                           uint64_t value,
                                           HRESULT result) {
    nlohmann::json fields = {
        {"method", method ? method : ""},
        {"hr", static_cast<int64_t>(result)},
    };
    fields[field ? field : "value"] = value;
    trace_ui(service, "candidate.behavior", "candidate", element_id, std::move(fields));
}

void trace_stage_candidate_behavior_bool(TextService* service,
                                         DWORD element_id,
                                         const char* method,
                                         const char* field,
                                         bool value,
                                         HRESULT result) {
    nlohmann::json fields = {
        {"method", method ? method : ""},
        {"hr", static_cast<int64_t>(result)},
    };
    fields[field ? field : "value"] = value;
    trace_ui(service, "candidate.behavior", "candidate", element_id, std::move(fields));
}

void trace_stage_candidate_integration_style(TextService* service,
                                             DWORD element_id,
                                             REFGUID integration_style,
                                             HRESULT result) {
    trace_ui(service, "candidate.behavior", "candidate", element_id, {
        {"method", "SetIntegrationStyle"},
        {"integration_style", cxxime::stage_trace_guid(integration_style)},
        {"hr", static_cast<int64_t>(result)},
    });
}

void trace_stage_candidate_key(TextService* service,
                               DWORD element_id,
                               WPARAM virtual_key,
                               LPARAM key_data,
                               bool eaten,
                               HRESULT result) {
    trace_ui(service, "candidate.behavior", "candidate", element_id, {
        {"method", "OnKeyDown"},
        {"vk", static_cast<uint64_t>(virtual_key)},
        {"key_data", static_cast<int64_t>(key_data)},
        {"eaten", eaten},
        {"hr", static_cast<int64_t>(result)},
    });
}

void trace_stage_candidate_snapshot(TextService* service,
                                    const std::vector<std::wstring>& candidates,
                                    UINT selection,
                                    int page_current,
                                    int page_total) {
    nlohmann::json lengths = nlohmann::json::array();
    for (const auto& candidate : candidates) {
        lengths.push_back(candidate.size());
    }
    trace_ui(service, "candidate.snapshot", "candidate", TF_INVALID_UIELEMENTID, {
        {"count", candidates.size()},
        {"selection", selection},
        {"page_starts", {0}},
        {"current_page", 0},
        {"engine_page_current", page_current},
        {"engine_page_total", page_total},
        {"updated_flags", CandidateUIElement::kPublishedUpdatedFlags},
        {"text_lengths", std::move(lengths)},
        {"text_digests", text_digests(candidates)},
        {"result", "updated"},
    });
}

void trace_stage_candidate_lifecycle(TextService* service,
                                     const char* action,
                                     DWORD element_id,
                                     HRESULT result,
                                     const char* result_name,
                                     const bool* show_external) {
    nlohmann::json fields = {
        {"hr", static_cast<int64_t>(result)},
        {"result", result_name ? result_name : ""},
    };
    if (show_external) {
        fields["show_external"] = *show_external;
    }
    std::string event = "ui_element.";
    event += action ? action : "";
    trace_ui(service, event.c_str(), "candidate", element_id, std::move(fields));
}

void trace_stage_external_ui_decision(TextService* service,
                                      DWORD element_id,
                                      bool show_external) {
    trace_ui(service, "external_ui.decision", "candidate", element_id, {
        {"show_external", show_external},
        {"result", show_external ? "allowed" : "suppressed"},
        {"reason", show_external ? "host_requested_tip_ui" : "host_takeover"},
    });
}

void trace_stage_reading_snapshot(TextService* service,
                                  const std::wstring& text,
                                  UINT max_length,
                                  bool context_present) {
    trace_ui(service, "reading.snapshot", "reading", TF_INVALID_UIELEMENTID, {
        {"text_len", text.size()},
        {"text_digest", cxxime::stage_trace_digest_utf16(text)},
        {"max_length", max_length},
        {"context_present", context_present},
        {"result", "updated"},
    });
}

void trace_stage_reading_lifecycle(TextService* service,
                                   const char* action,
                                   DWORD element_id,
                                   HRESULT result,
                                   const char* result_name,
                                   const bool* show_external) {
    nlohmann::json fields = {
        {"action", action ? action : ""},
        {"hr", static_cast<int64_t>(result)},
        {"result", result_name ? result_name : ""},
    };
    if (show_external) {
        fields["show_external"] = *show_external;
    }
    trace_ui(service, "reading.lifecycle", "reading", element_id, std::move(fields));
}

void trace_stage_imm_target(const char* action,
                            HWND hwnd,
                            HIMC himc,
                            const char* source,
                            uint64_t input_id,
                            uint64_t composition_id) {
    DWORD process_id = 0;
    const DWORD thread_id = hwnd ? GetWindowThreadProcessId(hwnd, &process_id) : 0;
    HWND imc_hwnd = nullptr;
    if (himc) {
        LPINPUTCONTEXT input_context = ImmLockIMC(himc);
        if (input_context) {
            imc_hwnd = input_context->hWnd;
            ImmUnlockIMC(himc);
        }
    }
    const HWND default_ime_hwnd = hwnd ? ImmGetDefaultIMEWnd(hwnd) : nullptr;
    cxxime::write_stage_trace("tsf", "imm.target", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"action", action ? action : ""},
        {"source", source ? source : ""},
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"window_pid", process_id},
        {"window_tid", thread_id},
        {"window_class", window_class_utf8(hwnd)},
        {"imc_hwnd", reinterpret_cast<uintptr_t>(imc_hwnd)},
        {"imc_window_class", window_class_utf8(imc_hwnd)},
        {"default_ime_hwnd", reinterpret_cast<uintptr_t>(default_ime_hwnd)},
        {"default_ime_window_class", window_class_utf8(default_ime_hwnd)},
        {"default_ime_visible", default_ime_hwnd && IsWindowVisible(default_ime_hwnd) != FALSE},
        {"hkl", reinterpret_cast<uintptr_t>(thread_id ? GetKeyboardLayout(thread_id) : nullptr)},
        {"open", himc ? ImmGetOpenStatus(himc) != FALSE : false},
        {"result", himc ? "acquired" : "not_found"},
    });
}

void trace_stage_imm_write(const char* action,
                           HIMC himc,
                           const std::wstring& composition,
                           const std::wstring& result,
                           bool write_ok,
                           uint64_t input_id,
                           uint64_t composition_id) {
    const ImmTextReadback composition_readback = read_imm_text(himc, GCS_COMPSTR);
    const ImmTextReadback result_readback = read_imm_text(himc, GCS_RESULTSTR);
    cxxime::write_stage_trace("tsf", "imm.write", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"action", action ? action : ""},
        {"comp_len", composition.size()},
        {"comp_digest", cxxime::stage_trace_digest_utf16(composition)},
        {"result_len", result.size()},
        {"result_digest", cxxime::stage_trace_digest_utf16(result)},
        {"write_ok", write_ok},
        {"readback_comp_bytes", composition_readback.bytes},
        {"readback_comp_digest", cxxime::stage_trace_digest_utf16(composition_readback.text)},
        {"readback_result_bytes", result_readback.bytes},
        {"readback_result_digest", cxxime::stage_trace_digest_utf16(result_readback.text)},
        {"result", write_ok ? "written" : "failed"},
    });
}

void trace_stage_imm_candidate(const char* action,
                               HIMC himc,
                               const std::vector<std::wstring>& candidates,
                               uint32_t selection,
                               WPARAM command,
                               bool write_ok,
                               bool message_ok,
                               uint64_t input_id,
                               uint64_t composition_id) {
    nlohmann::json lengths = nlohmann::json::array();
    for (const auto& candidate : candidates) {
        lengths.push_back(candidate.size());
    }
    const nlohmann::json expected_digests = text_digests(candidates);
    const ImmCandidateReadback readback = read_imm_candidates(himc);
    const bool readback_matches = candidates.empty()
        ? readback.count == 0
        : readback.valid && readback.count == candidates.size() &&
          readback.selection == selection && readback.digests == expected_digests;
    cxxime::write_stage_trace("tsf", "imm.candidate", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"action", action ? action : ""},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"count", candidates.size()},
        {"selection", selection},
        {"page_start", 0},
        {"page_size", candidates.size()},
        {"text_lengths", std::move(lengths)},
        {"text_digests", expected_digests},
        {"write_ok", write_ok},
        {"message", WM_IME_NOTIFY},
        {"command", static_cast<uint64_t>(command)},
        {"candidate_list_mask", 1},
        {"message_ok", message_ok},
        {"readback_bytes", readback.bytes},
        {"readback_count", readback.count},
        {"readback_selection", readback.selection},
        {"readback_page_start", readback.page_start},
        {"readback_page_size", readback.page_size},
        {"readback_digests", readback.digests},
        {"readback_valid", readback.valid},
        {"readback_matches", readback_matches},
        {"result", write_ok && message_ok && readback_matches ? "mirrored" : "failed"},
    });
}

void trace_stage_imm_candidate_lifecycle(const char* action,
                                        HIMC himc,
                                        WPARAM command,
                                        bool message_ok,
                                        const char* transport,
                                        uint64_t input_id,
                                        uint64_t composition_id) {
    const ImmCandidateReadback readback = read_imm_candidates(himc);
    cxxime::write_stage_trace("tsf", "imm.candidate_lifecycle", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"action", action ? action : ""},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"message", WM_IME_NOTIFY},
        {"command", static_cast<uint64_t>(command)},
        {"candidate_list_mask", 1},
        {"message_ok", message_ok},
        {"transport", transport ? transport : ""},
        {"candidate_list_bytes", readback.bytes},
        {"candidate_list_valid", readback.valid},
        {"candidate_count", readback.count},
        {"candidate_selection", readback.selection},
        {"candidate_page_start", readback.page_start},
        {"candidate_page_size", readback.page_size},
        {"result", message_ok ? "generated" : "failed"},
    });
}

void trace_stage_imm_message(UINT message,
                             LPARAM flags,
                             bool ok,
                             uint64_t input_id,
                             uint64_t composition_id) {
    cxxime::write_stage_trace("tsf", "imm.message", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"message", message},
        {"flags", static_cast<uint64_t>(flags)},
        {"result", ok ? "generated" : "failed"},
    });
}

} // namespace cxxime_tsf
