// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_imm_mode.h"

#include <cxxime/stage_trace.h>
#include <msctf.h>

#include <cstring>

namespace cxxime_tsf {

StageImmProfileSnapshot capture_stage_imm_profile(HWND hwnd) {
    StageImmProfileSnapshot snapshot;
    snapshot.window_thread_id = hwnd ? GetWindowThreadProcessId(hwnd, nullptr)
                                    : GetCurrentThreadId();
    const HKL keyboard_layout = GetKeyboardLayout(snapshot.window_thread_id);
    snapshot.keyboard_layout = reinterpret_cast<uintptr_t>(keyboard_layout);
    snapshot.keyboard_layout_language = LOWORD(snapshot.keyboard_layout);
    snapshot.keyboard_layout_device = HIWORD(snapshot.keyboard_layout);
    snapshot.keyboard_layout_is_ime = keyboard_layout && ImmIsIME(keyboard_layout) != FALSE;

    char ime_file_name[MAX_PATH] = {};
    snapshot.ime_file_name_length = ImmGetIMEFileNameA(
        keyboard_layout, ime_file_name, ARRAYSIZE(ime_file_name));
    if (snapshot.ime_file_name_length > 0 &&
        snapshot.ime_file_name_length < ARRAYSIZE(ime_file_name)) {
        const char* basename = strrchr(ime_file_name, '\\');
        const char* alternate_basename = strrchr(ime_file_name, '/');
        if (!basename || (alternate_basename && alternate_basename > basename)) {
            basename = alternate_basename;
        }
        snapshot.ime_file_name = basename ? basename + 1 : ime_file_name;
    }

    snapshot.property = ImmGetProperty(keyboard_layout, IGP_PROPERTY);
    snapshot.conversion = ImmGetProperty(keyboard_layout, IGP_CONVERSION);
    snapshot.sentence = ImmGetProperty(keyboard_layout, IGP_SENTENCE);
    snapshot.ui = ImmGetProperty(keyboard_layout, IGP_UI);
    snapshot.set_composition_string = ImmGetProperty(keyboard_layout, IGP_SETCOMPSTR);
    snapshot.select = ImmGetProperty(keyboard_layout, IGP_SELECT);
    snapshot.ime_version = ImmGetProperty(keyboard_layout, IGP_GETIMEVERSION);
    return snapshot;
}

void trace_stage_imm_open_status(HWND hwnd,
                                 HIMC himc,
                                 bool open_before,
                                 bool set_attempted,
                                 bool set_succeeded,
                                 bool open_after,
                                 uint64_t input_id,
                                 uint64_t composition_id) {
    const char* result = "already_open";
    if (set_attempted) {
        result = set_succeeded && open_after ? "opened" : "failed";
    }
    cxxime::write_stage_trace("tsf", "imm.open_status", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"open_before", open_before},
        {"set_attempted", set_attempted},
        {"set_succeeded", set_succeeded},
        {"open_after", open_after},
        {"thread_id", GetCurrentThreadId()},
        {"result", result},
    });
}

void trace_stage_conversion_compartment(bool chinese_mode,
                                        HRESULT get_value_hr,
                                        DWORD before_mode,
                                        DWORD requested_mode,
                                        bool set_attempted,
                                        HRESULT set_value_hr) {
    const bool success = set_attempted
        ? SUCCEEDED(set_value_hr)
        : SUCCEEDED(get_value_hr);
    cxxime::write_stage_trace("tsf", "runtime.conversion_compartment", {
        {"chinese_mode", chinese_mode},
        {"get_value_hr", static_cast<int64_t>(get_value_hr)},
        {"before_mode", before_mode},
        {"before_native", (before_mode & TF_CONVERSIONMODE_NATIVE) != 0},
        {"before_symbol", (before_mode & TF_CONVERSIONMODE_SYMBOL) != 0},
        {"requested_mode", requested_mode},
        {"requested_native", (requested_mode & TF_CONVERSIONMODE_NATIVE) != 0},
        {"requested_symbol", (requested_mode & TF_CONVERSIONMODE_SYMBOL) != 0},
        {"set_attempted", set_attempted},
        {"set_value_hr", static_cast<int64_t>(set_value_hr)},
        {"thread_id", GetCurrentThreadId()},
        {"result", success ? (set_attempted ? "set" : "already_aligned")
                           : "failed"},
    });
}

void trace_stage_imm_conversion_status(HWND hwnd,
                                       HIMC himc,
                                       bool query_ok,
                                       DWORD conversion_mode,
                                       DWORD sentence_mode,
                                       uint64_t input_id,
                                       uint64_t composition_id) {
    constexpr DWORD kObservedMicrosoftMode =
        IME_CMODE_NATIVE | IME_CMODE_SYMBOL;
    const bool aligned = query_ok && conversion_mode == kObservedMicrosoftMode;
    cxxime::write_stage_trace("tsf", "imm.conversion_status", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"query_ok", query_ok},
        {"conversion_mode", conversion_mode},
        {"sentence_mode", sentence_mode},
        {"native", (conversion_mode & IME_CMODE_NATIVE) != 0},
        {"symbol", (conversion_mode & IME_CMODE_SYMBOL) != 0},
        {"thread_id", GetCurrentThreadId()},
        {"result", aligned ? "aligned" : "different"},
    });
}

} // namespace cxxime_tsf
