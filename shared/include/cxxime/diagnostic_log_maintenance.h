// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DIAGNOSTIC_LOG_MAINTENANCE_H_
#define CXXIME_DIAGNOSTIC_LOG_MAINTENANCE_H_

#include <cstdint>
#include <string>

#include <cxxime/diagnostics_config.h>

namespace cxxime {

enum class DiagnosticLogCleanupMode {
    kRetention,
    kPurgeHistory,
};

struct DiagnosticLogCleanupOptions {
    DiagnosticLogCleanupMode mode = DiagnosticLogCleanupMode::kRetention;
    std::uint64_t max_age_100ns = 0;
    std::uint64_t high_watermark = 0;
    std::uint64_t low_watermark = 0;
    std::uint64_t minimum_interval_100ns = 0;
};

struct DiagnosticLogCleanupResult {
    bool directory_found = false;
    bool maintenance_performed = false;
    bool throttled = false;
    bool lock_busy = false;
    std::uint32_t matching_files = 0;
    std::uint32_t deleted_files = 0;
    std::uint32_t skipped_files = 0;
    std::uint64_t deleted_bytes = 0;
    std::uint32_t error_code = 0;
};

bool is_diagnostic_log_filename(const std::wstring& filename);

DiagnosticLogCleanupOptions diagnostic_log_retention_options(const DiagnosticsConfig& config);
DiagnosticLogCleanupOptions diagnostic_log_purge_options();

DiagnosticLogCleanupResult
cleanup_diagnostic_log_directory(const std::wstring& directory,
                                 const DiagnosticLogCleanupOptions& options);

} // namespace cxxime

#endif // CXXIME_DIAGNOSTIC_LOG_MAINTENANCE_H_
