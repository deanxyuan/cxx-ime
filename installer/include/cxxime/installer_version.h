// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_VERSION_H_
#define CXXIME_INSTALLER_VERSION_H_

#include <string>

namespace cxxime {
namespace installer {

enum class VersionOrder {
    kOlder = -1,
    kEqual = 0,
    kNewer = 1,
};

bool compare_semantic_versions(const std::string& left, const std::string& right,
                               VersionOrder* order);
int compare_version_command(const wchar_t* installed, const wchar_t* package);

} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_VERSION_H_
