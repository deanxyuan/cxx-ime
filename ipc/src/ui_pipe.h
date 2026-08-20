// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_UI_PIPE_H_
#define CXXIME_UI_PIPE_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

namespace cxxime {
namespace ui_pipe {

constexpr DWORD kConnectRetryMs = 100;
constexpr DWORD kWriteTimeoutMs = 2000;

HANDLE connect_pipe(const std::wstring& pipe_name, HANDLE stop_event);
bool connect_pipe_instance(HANDLE pipe, HANDLE stop_event, const std::atomic<bool>& running);
bool write_packet(HANDLE pipe, HANDLE stop_event, const std::vector<std::uint8_t>& packet);

} // namespace ui_pipe
} // namespace cxxime

#endif // CXXIME_UI_PIPE_H_
