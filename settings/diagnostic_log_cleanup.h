// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SETTINGS_DIAGNOSTIC_LOG_CLEANUP_H_
#define CXXIME_SETTINGS_DIAGNOSTIC_LOG_CLEANUP_H_

#include <cstdint>

namespace cxxime {
namespace settings {

struct DiagnosticsCleanupSummary {
    std::uint32_t directories = 0;
    std::uint32_t deleted_files = 0;
    std::uint32_t skipped_files = 0;
    std::uint32_t inaccessible_directories = 0;
    std::uint64_t deleted_bytes = 0;
};

DiagnosticsCleanupSummary cleanup_current_user_diagnostic_logs();

} // namespace settings
} // namespace cxxime

#endif // CXXIME_SETTINGS_DIAGNOSTIC_LOG_CLEANUP_H_
