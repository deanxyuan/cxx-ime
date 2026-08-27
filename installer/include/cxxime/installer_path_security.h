// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_PATH_SECURITY_H_
#define CXXIME_INSTALLER_PATH_SECURITY_H_

#include <string>

namespace cxxime {
namespace installer {

int secure_install_root(const std::wstring& path);
int validate_install_directory(const std::wstring& path);

} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_PATH_SECURITY_H_
