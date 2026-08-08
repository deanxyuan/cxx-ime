// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/installer_lock.h>

namespace {

constexpr int kExitNoLocks = 0;
constexpr int kExitLocked = 2;
constexpr int kExitRebootRequired = 3;
constexpr int kExitQueryFailed = 4;
constexpr int kExitInvalidArguments = 64;

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
    if (argc < 4 || std::wstring(argv[1]) != L"query" || std::wstring(argv[2]) != L"--report") {
        return kExitInvalidArguments;
    }

    const std::wstring report_path = argv[3];
    std::vector<std::wstring> resources;
    for (int index = 4; index < argc; ++index) {
        resources.emplace_back(argv[index]);
    }
    if (resources.empty()) {
        return kExitInvalidArguments;
    }

    const auto result = cxxime::installer::query_file_locks(resources);
    const std::wstring report = cxxime::installer::format_lock_report(result);
    if (!write_utf16_report(report_path, report)) {
        return kExitQueryFailed;
    }
    if (result.status == cxxime::installer::LockQueryStatus::kFailed) {
        return kExitQueryFailed;
    }
    if (result.status == cxxime::installer::LockQueryStatus::kRebootRequired) {
        return kExitRebootRequired;
    }
    return result.applications.empty() ? kExitNoLocks : kExitLocked;
}
