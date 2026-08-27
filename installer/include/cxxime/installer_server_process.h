// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_SERVER_PROCESS_H_
#define CXXIME_INSTALLER_SERVER_PROCESS_H_

#include <string>

namespace cxxime {
namespace installer {

int server_running(const std::wstring& path);
int server_ready(const std::wstring& path);
int print_server_pid(const std::wstring& path);
int stop_server(const std::wstring& path, bool force);
int start_server(const std::wstring& path);

} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_SERVER_PROCESS_H_
