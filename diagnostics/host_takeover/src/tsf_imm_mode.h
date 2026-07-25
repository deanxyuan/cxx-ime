// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_IMM_MODE_H_
#define CXXIME_HOST_TAKEOVER_TSF_IMM_MODE_H_

#include <windows.h>
#include <imm.h>

#include <cstdint>
#include <string>

namespace cxxime_tsf {

struct StageImmProfileSnapshot {
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

StageImmProfileSnapshot capture_stage_imm_profile(HWND hwnd);

void trace_stage_imm_open_status(HWND hwnd,
                                 HIMC himc,
                                 bool open_before,
                                 bool set_attempted,
                                 bool set_succeeded,
                                 bool open_after,
                                 uint64_t input_id,
                                 uint64_t composition_id);

void trace_stage_conversion_compartment(bool chinese_mode,
                                         HRESULT get_value_hr,
                                         DWORD before_mode,
                                         DWORD requested_mode,
                                         bool set_attempted,
                                         HRESULT set_value_hr);

void trace_stage_imm_conversion_status(HWND hwnd,
                                       HIMC himc,
                                       bool query_ok,
                                       DWORD conversion_mode,
                                       DWORD sentence_mode,
                                       uint64_t input_id,
                                       uint64_t composition_id);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_IMM_MODE_H_
