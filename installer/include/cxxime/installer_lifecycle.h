// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_LIFECYCLE_H_
#define CXXIME_INSTALLER_LIFECYCLE_H_

#include <cstdint>
#include <string>

namespace cxxime {
namespace installer {

struct InstallLifecycleResult {
    std::wstring install_target;
    std::uint32_t cleaned_generations = 0;
    std::uint32_t scheduled_files = 0;
    std::uint32_t unknown_files = 0;
    std::uint32_t remaining_generations = 0;
};

bool prepare_install_lifecycle(const std::wstring& root, const std::wstring& registered_active,
                               const std::string& version, InstallLifecycleResult* result,
                               unsigned long* error_code);
bool get_prepared_install_target(const std::wstring& root, InstallLifecycleResult* result,
                                 unsigned long* error_code);
bool commit_install_lifecycle(const std::wstring& root, const std::wstring& new_active,
                              const std::wstring& old_active, InstallLifecycleResult* result,
                              unsigned long* error_code);
bool collect_install_garbage(const std::wstring& root, InstallLifecycleResult* result,
                             unsigned long* error_code);
bool validate_uninstall_lifecycle(const std::wstring& root, const std::wstring& active,
                                  unsigned long* error_code);
bool uninstall_install_lifecycle(const std::wstring& root, const std::wstring& active,
                                 InstallLifecycleResult* result, unsigned long* error_code);
bool write_install_lifecycle_result(const std::wstring& path, const InstallLifecycleResult& result,
                                    unsigned long* error_code);

} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_LIFECYCLE_H_
