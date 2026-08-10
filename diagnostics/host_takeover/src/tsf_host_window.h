// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_HOST_WINDOW_H_
#define CXXIME_HOST_TAKEOVER_TSF_HOST_WINDOW_H_

#include <windows.h>
#include <imm.h>

#include <cstdint>

namespace cxxime_tsf {

void trace_host_window_snapshot(HWND target_hwnd,
                                HIMC target_himc,
                                uint64_t input_id,
                                uint64_t composition_id);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_HOST_WINDOW_H_
