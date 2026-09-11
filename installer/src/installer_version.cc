// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/installer_version.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cxxime {
namespace installer {
namespace {

struct SemanticVersion {
    std::array<std::uint64_t, 3> core = {};
    std::vector<std::string> prerelease;
};

bool parse_number(const std::string& value, std::uint64_t* result) {
    if (!result || value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch);
        })) {
        return false;
    }
    std::uint64_t number = 0;
    for (const unsigned char ch : value) {
        const std::uint64_t digit = ch - '0';
        if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        number = number * 10 + digit;
    }
    *result = number;
    return true;
}

std::vector<std::string> split(const std::string& value, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = value.find(separator, start);
        parts.push_back(value.substr(start, end - start));
        if (end == std::string::npos) {
            return parts;
        }
        start = end + 1;
    }
}

bool parse_version(const std::string& value, SemanticVersion* result) {
    if (!result || value.empty() || value.find('+') != std::string::npos) {
        return false;
    }
    const std::size_t separator = value.find('-');
    const std::string core = value.substr(0, separator);
    const auto core_parts = split(core, '.');
    if (core_parts.size() != 3) {
        return false;
    }
    SemanticVersion parsed;
    for (std::size_t index = 0; index < core_parts.size(); ++index) {
        if (!parse_number(core_parts[index], &parsed.core[index])) {
            return false;
        }
    }
    if (separator != std::string::npos) {
        const std::string prerelease = value.substr(separator + 1);
        parsed.prerelease = split(prerelease, '.');
        for (const auto& identifier : parsed.prerelease) {
            if (identifier.empty() ||
                !std::all_of(identifier.begin(), identifier.end(),
                             [](unsigned char ch) { return std::isalnum(ch) || ch == '-'; })) {
                return false;
            }
        }
    }
    *result = std::move(parsed);
    return true;
}

int compare_identifier(const std::string& left, const std::string& right) {
    std::uint64_t left_number = 0;
    std::uint64_t right_number = 0;
    const bool left_numeric = parse_number(left, &left_number);
    const bool right_numeric = parse_number(right, &right_number);
    if (left_numeric && right_numeric) {
        return left_number < right_number ? -1 : left_number > right_number ? 1 : 0;
    }
    if (left_numeric != right_numeric) {
        return left_numeric ? -1 : 1;
    }
    return left < right ? -1 : left > right ? 1 : 0;
}

int compare_versions(const SemanticVersion& left, const SemanticVersion& right) {
    if (left.core != right.core) {
        return left.core < right.core ? -1 : 1;
    }
    if (left.prerelease.empty() != right.prerelease.empty()) {
        return left.prerelease.empty() ? 1 : -1;
    }
    const std::size_t count = std::min(left.prerelease.size(), right.prerelease.size());
    for (std::size_t index = 0; index < count; ++index) {
        const int order = compare_identifier(left.prerelease[index], right.prerelease[index]);
        if (order != 0) {
            return order;
        }
    }
    return left.prerelease.size() < right.prerelease.size()   ? -1
           : left.prerelease.size() > right.prerelease.size() ? 1
                                                              : 0;
}

bool narrow_ascii(const wchar_t* value, std::string* result) {
    if (!value || !result) {
        return false;
    }
    result->clear();
    for (; *value != L'\0'; ++value) {
        if (*value < 0x20 || *value > 0x7e) {
            return false;
        }
        result->push_back(static_cast<char>(*value));
    }
    return !result->empty();
}

} // namespace

bool compare_semantic_versions(const std::string& left, const std::string& right,
                               VersionOrder* order) {
    if (!order) {
        return false;
    }
    SemanticVersion left_version;
    SemanticVersion right_version;
    if (!parse_version(left, &left_version) || !parse_version(right, &right_version)) {
        return false;
    }
    const int comparison = compare_versions(left_version, right_version);
    *order = comparison < 0   ? VersionOrder::kOlder
             : comparison > 0 ? VersionOrder::kNewer
                              : VersionOrder::kEqual;
    return true;
}

int compare_version_command(const wchar_t* installed, const wchar_t* package) {
    std::string installed_version;
    std::string package_version;
    VersionOrder order = VersionOrder::kEqual;
    if (!narrow_ascii(installed, &installed_version) || !narrow_ascii(package, &package_version) ||
        !compare_semantic_versions(installed_version, package_version, &order)) {
        return 64;
    }
    if (order == VersionOrder::kOlder) {
        return 1;
    }
    if (order == VersionOrder::kNewer) {
        return 2;
    }
    return 0;
}

} // namespace installer
} // namespace cxxime
