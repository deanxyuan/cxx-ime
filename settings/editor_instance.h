// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SETTINGS_EDITOR_INSTANCE_H_
#define CXXIME_SETTINGS_EDITOR_INSTANCE_H_

#include <filesystem>

#include <windows.h>

#include <cxxime/data_path.h>

namespace cxxime {
namespace settings {

// One writer per user directory, including launches which bypass the launcher.
// The empty file stays on disk; only the open handle owns the exclusion.
class EditorInstance {
public:
    EditorInstance() {
        const auto path = std::filesystem::u8path(user_data_path("settings.lock"));
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_HIDDEN, nullptr);
        error_ = handle_ == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    }
    ~EditorInstance() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    EditorInstance(const EditorInstance&) = delete;
    EditorInstance& operator=(const EditorInstance&) = delete;

    explicit operator bool() const { return handle_ != INVALID_HANDLE_VALUE; }
    DWORD error() const { return error_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD error_ = ERROR_SUCCESS;
};

} // namespace settings
} // namespace cxxime

#endif // CXXIME_SETTINGS_EDITOR_INSTANCE_H_
