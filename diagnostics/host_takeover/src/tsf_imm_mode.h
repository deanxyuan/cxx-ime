// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_IMM_MODE_H_
#define CXXIME_HOST_TAKEOVER_TSF_IMM_MODE_H_

#include <windows.h>
#include <imm.h>

#include <cstdint>
#include <string>

namespace cxxime_tsf {

struct TraceImmProfileSnapshot {
    DWORD window_thread_id = 0;
    uintptr_t keyboard_layout = 0;
    WORD keyboard_layout_language = 0;
    WORD keyboard_layout_device = 0;
    bool keyboard_layout_is_ime = false;
    UINT ime_file_name_length = 0;
    std::string ime_file_name;
    DWORD property = 0;
    DWORD conversion = 0;
    DWORD sentence = 0;
    DWORD ui = 0;
    DWORD set_composition_string = 0;
    DWORD select = 0;
    DWORD ime_version = 0;
};

TraceImmProfileSnapshot capture_imm_profile(HWND hwnd);

void trace_imm_open_status(HWND hwnd,
                           HIMC himc,
                           bool open_before,
                           bool set_attempted,
                           bool set_succeeded,
                           bool open_after,
                           uint64_t input_id,
                           uint64_t composition_id);

void trace_conversion_compartment(bool chinese_mode,
                                  HRESULT get_value_hr,
                                  DWORD before_mode,
                                  DWORD requested_mode,
                                  bool set_attempted,
                                  HRESULT set_value_hr);

void trace_conversion_sink_lifecycle(const char* action,
                                     HRESULT manager_hr,
                                     HRESULT compartment_hr,
                                     HRESULT source_hr,
                                     HRESULT operation_hr,
                                     DWORD cookie);

struct TraceConversionSinkChange {
    HRESULT value_hr = E_UNEXPECTED;
    uint16_t value_type = 0;
    DWORD conversion_mode = 0;
    bool self_write = false;
    bool composing = false;
    bool before_chinese = false;
    bool requested_chinese = false;
    bool set_attempted = false;
    bool set_succeeded = false;
    bool commit_requested = false;
    uint32_t commit_text_length = 0;
    int64_t ipc_us = 0;
    bool after_chinese = false;
    bool status_details = false;
    bool before_full_shape = false;
    bool after_full_shape = false;
    bool before_chinese_punct = false;
    bool after_chinese_punct = false;
    uint32_t before_input_mode = 0;
    uint32_t after_input_mode = 0;
    const char* result = "";
};

void trace_conversion_sink_change(const TraceConversionSinkChange& change);

void trace_imm_conversion_status(HWND hwnd,
                                 HIMC himc,
                                 bool query_ok,
                                 DWORD conversion_mode,
                                 DWORD sentence_mode,
                                 uint64_t input_id,
                                 uint64_t composition_id);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_IMM_MODE_H_
