// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "user_data_file.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>

#include <windows.h>

namespace cxxime {
namespace {

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), &result[0], length) != length) {
        return {};
    }
    return result;
}

std::wstring temporary_path(const std::wstring& path) {
    static std::atomic<std::uint32_t> sequence{0};
    return path + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
           std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
}

bool write_all(HANDLE file, const std::string& contents) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const std::size_t remaining = contents.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            (std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool read_user_data_file_impl(const std::string& path, bool allow_missing, std::uint64_t max_size,
                              std::string* contents) {
    if (!contents) {
        return false;
    }
    contents->clear();

    const std::wstring wide_path = utf8_to_wide(path);
    if (wide_path.empty()) {
        return false;
    }
    HANDLE file = CreateFileW(wide_path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return allow_missing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > max_size ||
        static_cast<unsigned long long>(size.QuadPart) >
            static_cast<unsigned long long>((std::numeric_limits<std::size_t>::max)())) {
        CloseHandle(file);
        return false;
    }
    contents->resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < contents->size()) {
        const std::size_t remaining = contents->size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            (std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD read = 0;
        if (!ReadFile(file, &(*contents)[offset], chunk, &read, nullptr) || read != chunk) {
            CloseHandle(file);
            contents->clear();
            return false;
        }
        offset += read;
    }
    CloseHandle(file);
    return true;
}

} // namespace

bool read_user_data_file(const std::string& path, std::string* contents) {
    return read_user_data_file_impl(path, true, (std::numeric_limits<std::uint64_t>::max)(),
                                    contents);
}

bool read_user_data_file(const std::string& path, std::uint64_t max_size, std::string* contents) {
    return read_user_data_file_impl(path, true, max_size, contents);
}

bool read_existing_user_data_file(const std::string& path, std::uint64_t max_size,
                                  std::string* contents) {
    return read_user_data_file_impl(path, false, max_size, contents);
}

bool write_user_data_file_atomically(const std::string& path, const std::string& contents) {
    const std::wstring wide_path = utf8_to_wide(path);
    if (wide_path.empty()) {
        return false;
    }
    const std::wstring temp_path = temporary_path(wide_path);
    HANDLE file = CreateFileW(temp_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const bool written = write_all(file, contents) && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!written) {
        DeleteFileW(temp_path.c_str());
        return false;
    }
    if (!MoveFileExW(temp_path.c_str(), wide_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_path.c_str());
        return false;
    }
    return true;
}

} // namespace cxxime
