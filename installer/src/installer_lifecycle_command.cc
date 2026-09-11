// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "installer_lifecycle_command.h"

#include <algorithm>
#include <string>

#include <windows.h>

#include <cxxime/installer_lifecycle.h>

namespace cxxime {
namespace installer {
namespace {

std::wstring optional_path(const wchar_t* value) {
    return value && std::wstring(value) != L"-" ? value : L"";
}

bool narrow_ascii(const wchar_t* value, std::string* result) {
    if (!value || !result) {
        return false;
    }
    const std::wstring wide(value);
    if (wide.empty() ||
        std::any_of(wide.begin(), wide.end(), [](wchar_t ch) { return ch < 0x20 || ch > 0x7e; })) {
        return false;
    }
    result->clear();
    result->reserve(wide.size());
    for (wchar_t ch : wide) {
        result->push_back(static_cast<char>(ch));
    }
    return true;
}

int command_result(bool succeeded, unsigned long error_code) {
    return succeeded
               ? 0
               : static_cast<int>(error_code == ERROR_SUCCESS ? ERROR_INVALID_DATA : error_code);
}

} // namespace

int prepare_install_lifecycle_command(const wchar_t* root, const wchar_t* active,
                                      const wchar_t* version, const wchar_t* result_path) {
    InstallLifecycleResult result;
    unsigned long error = ERROR_SUCCESS;
    std::string narrow_version;
    const bool prepared =
        narrow_ascii(version, &narrow_version) &&
        prepare_install_lifecycle(root, optional_path(active), narrow_version, &result, &error);
    return command_result(prepared && write_install_lifecycle_result(result_path, result, &error),
                          error);
}

int commit_install_lifecycle_command(const wchar_t* root, const wchar_t* new_active,
                                     const wchar_t* old_active) {
    InstallLifecycleResult result;
    unsigned long error = ERROR_SUCCESS;
    const bool committed =
        commit_install_lifecycle(root, new_active, optional_path(old_active), &result, &error);
    return command_result(committed, error);
}

int collect_install_garbage_command(const wchar_t* root, const wchar_t* result_path) {
    InstallLifecycleResult result;
    unsigned long error = ERROR_SUCCESS;
    const bool collected = collect_install_garbage(root, &result, &error);
    return command_result(collected && write_install_lifecycle_result(result_path, result, &error),
                          error);
}

int prepared_install_target_command(const wchar_t* root, const wchar_t* result_path) {
    InstallLifecycleResult result;
    unsigned long error = ERROR_SUCCESS;
    const bool loaded = get_prepared_install_target(root, &result, &error);
    return command_result(loaded && write_install_lifecycle_result(result_path, result, &error),
                          error);
}

int validate_uninstall_lifecycle_command(const wchar_t* root, const wchar_t* active) {
    unsigned long error = ERROR_SUCCESS;
    return command_result(validate_uninstall_lifecycle(root, active, &error), error);
}

int uninstall_lifecycle_command(const wchar_t* root, const wchar_t* active,
                                const wchar_t* result_path) {
    InstallLifecycleResult result;
    unsigned long error = ERROR_SUCCESS;
    const bool uninstalled =
        uninstall_install_lifecycle(root, optional_path(active), &result, &error);
    return command_result(
        uninstalled && write_install_lifecycle_result(result_path, result, &error), error);
}

} // namespace installer
} // namespace cxxime
