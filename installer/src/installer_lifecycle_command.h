// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_LIFECYCLE_COMMAND_H_
#define CXXIME_INSTALLER_LIFECYCLE_COMMAND_H_

namespace cxxime {
namespace installer {

int prepare_install_lifecycle_command(const wchar_t* root, const wchar_t* active,
                                      const wchar_t* version, const wchar_t* result_path);
int commit_install_lifecycle_command(const wchar_t* root, const wchar_t* new_active,
                                     const wchar_t* old_active);
int collect_install_garbage_command(const wchar_t* root, const wchar_t* result_path);
int prepared_install_target_command(const wchar_t* root, const wchar_t* result_path);
int validate_uninstall_lifecycle_command(const wchar_t* root, const wchar_t* active);
int uninstall_lifecycle_command(const wchar_t* root, const wchar_t* active,
                                const wchar_t* result_path);

} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_LIFECYCLE_COMMAND_H_
