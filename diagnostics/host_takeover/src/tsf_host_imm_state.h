// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_HOST_IMM_STATE_H_
#define CXXIME_HOST_TAKEOVER_TSF_HOST_IMM_STATE_H_

#include <windows.h>

namespace cxxime_tsf {

bool start_host_imm_response_monitor();
void stop_host_imm_response_monitor();
void trace_host_imm_state(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_HOST_IMM_STATE_H_
