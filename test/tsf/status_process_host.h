// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_TSF_STATUS_PROCESS_HOST_H_
#define CXXIME_TEST_TSF_STATUS_PROCESS_HOST_H_

#include <string>

#include <windows.h>

namespace status_test {

constexpr wchar_t kHostWindowClass[] = L"CxxIME.Test.StatusProcessHost";
constexpr UINT kHostCommand = WM_APP + 1;
enum class HostCommand {
    Session,
    Focus,
    Blur,
    Keyboard,
    LateTimer,
    PauseTimers,
    PendingTimer,
    LanguageShortcut,
    CapsKey,
    ConfirmFocus
};

int run_host(const std::wstring& pipe, const std::wstring& title);

} // namespace status_test

#endif // CXXIME_TEST_TSF_STATUS_PROCESS_HOST_H_
