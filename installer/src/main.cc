// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/installer_lock.h>
#include <cxxime/installer_prompt.h>
#include <cxxime/installer_server_process.h>
#include <cxxime/installer_tsf.h>

namespace {

constexpr int kExitNoLocks = 0;
constexpr int kExitLocked = 2;
constexpr int kExitRebootRequired = 3;
constexpr int kExitQueryFailed = 4;
// NSIS consumes these stable action codes when the helper displays a prompt.
constexpr int kExitPromptRetry = 10;
constexpr int kExitPromptDeferUntilRestart = 11;
constexpr int kExitPromptCancel = 12;
constexpr int kExitInvalidArguments = 64;

bool starts_with(const std::wstring& value, const wchar_t* prefix) {
    const std::wstring expected(prefix);
    return value.compare(0, expected.size(), expected) == 0;
}

bool parse_parent_window(const std::wstring& value, std::uintptr_t* parent_window) {
    if (!parent_window || value.empty()) {
        return false;
    }

    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(value.c_str(), &end, 0);
    if (end == value.c_str() || *end != L'\0') {
        return false;
    }
    *parent_window = static_cast<std::uintptr_t>(parsed);
    return true;
}

int prompt_exit_code(cxxime::installer::LockPromptChoice choice) {
    switch (choice) {
    case cxxime::installer::LockPromptChoice::kRetry:
        return kExitPromptRetry;
    case cxxime::installer::LockPromptChoice::kDeferUntilRestart:
        return kExitPromptDeferUntilRestart;
    case cxxime::installer::LockPromptChoice::kCancel:
        return kExitPromptCancel;
    case cxxime::installer::LockPromptChoice::kFailed:
        return kExitQueryFailed;
    }
    return kExitQueryFailed;
}

bool write_utf16_report(const std::wstring& path, const std::wstring& report) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const std::uint16_t bom = 0xfeff;
    DWORD written = 0;
    bool success =
        WriteFile(file, &bom, sizeof(bom), &written, nullptr) != FALSE && written == sizeof(bom);
    if (success && !report.empty()) {
        const DWORD byte_count = static_cast<DWORD>(report.size() * sizeof(wchar_t));
        success = WriteFile(file, report.data(), byte_count, &written, nullptr) != FALSE &&
                  written == byte_count;
    }
    CloseHandle(file);
    return success;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"release") {
        return cxxime::installer::release_input_processor();
    }
    if (argc == 3 && std::wstring(argv[1]) == L"server-running") {
        return cxxime::installer::server_running(argv[2]);
    }
    if (argc == 3 && std::wstring(argv[1]) == L"server-pid") {
        return cxxime::installer::print_server_pid(argv[2]);
    }
    if (argc == 3 && std::wstring(argv[1]) == L"stop-server") {
        return cxxime::installer::stop_server(argv[2], false);
    }
    if (argc == 3 && std::wstring(argv[1]) == L"force-stop-server") {
        return cxxime::installer::stop_server(argv[2], true);
    }
    if (argc == 3 && std::wstring(argv[1]) == L"start-server") {
        return cxxime::installer::start_server(argv[2]);
    }
    if (argc < 4 || std::wstring(argv[1]) != L"query" || std::wstring(argv[2]) != L"--report") {
        return kExitInvalidArguments;
    }

    const std::wstring report_path = argv[3];
    bool show_prompt = false;
    cxxime::installer::LockPromptMode prompt_mode = cxxime::installer::LockPromptMode::kInstall;
    std::uintptr_t parent_window = 0;
    std::vector<std::wstring> resources;
    for (int index = 4; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (starts_with(argument, L"--prompt=")) {
            const std::wstring mode = argument.substr(std::wstring(L"--prompt=").size());
            if (mode == L"install") {
                prompt_mode = cxxime::installer::LockPromptMode::kInstall;
            } else if (mode == L"uninstall") {
                prompt_mode = cxxime::installer::LockPromptMode::kUninstall;
            } else {
                return kExitInvalidArguments;
            }
            show_prompt = true;
        } else if (starts_with(argument, L"--parent=")) {
            const std::wstring value = argument.substr(std::wstring(L"--parent=").size());
            if (!parse_parent_window(value, &parent_window)) {
                return kExitInvalidArguments;
            }
        } else if (starts_with(argument, L"--")) {
            return kExitInvalidArguments;
        } else {
            resources.push_back(argument);
        }
    }
    if (resources.empty()) {
        return kExitInvalidArguments;
    }

    const auto result = cxxime::installer::query_file_locks(resources);
    const std::wstring report = cxxime::installer::format_lock_report(result);
    if (!write_utf16_report(report_path, report)) {
        return kExitQueryFailed;
    }
    const bool needs_attention =
        result.status != cxxime::installer::LockQueryStatus::kSuccess ||
        !result.applications.empty();
    if (show_prompt && needs_attention) {
        return prompt_exit_code(
            cxxime::installer::show_lock_prompt(prompt_mode, parent_window, result, report));
    }
    if (result.status == cxxime::installer::LockQueryStatus::kFailed) {
        return kExitQueryFailed;
    }
    if (result.status == cxxime::installer::LockQueryStatus::kRebootRequired) {
        return kExitRebootRequired;
    }
    return result.applications.empty() ? kExitNoLocks : kExitLocked;
}
