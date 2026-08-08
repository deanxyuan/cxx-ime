// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_LOCK_H_
#define CXXIME_INSTALLER_LOCK_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cxxime {
namespace installer {

enum class LockQueryStatus {
    kSuccess,
    kRebootRequired,
    kFailed,
};

struct LockingApplication {
    std::uint32_t process_id = 0;
    std::wstring name;
};

struct LockQueryResult {
    LockQueryStatus status = LockQueryStatus::kSuccess;
    std::uint32_t error_code = 0;
    std::uint32_t reboot_reasons = 0;
    std::vector<LockingApplication> applications;
};

LockQueryResult query_file_locks(const std::vector<std::wstring>& paths);
std::wstring format_lock_report(const LockQueryResult& result, std::size_t max_applications = 8);

} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_LOCK_H_
