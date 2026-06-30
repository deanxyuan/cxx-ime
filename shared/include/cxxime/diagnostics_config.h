// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DIAGNOSTICS_CONFIG_H_
#define CXXIME_DIAGNOSTICS_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace cxxime {

enum class DiagnosticTraceMode {
    kOff = 0,
    kError,
    kNormal,
    kVerbose,
};

struct DiagnosticsConfig {
    DiagnosticTraceMode trace_mode = DiagnosticTraceMode::kNormal;
    std::size_t log_max_size = 8 * 1024 * 1024;
    int log_max_files = 4;
    int normal_sample_rate = 0;
    int truncated_sample_rate = 100;
    int slow_query_us = 30000;
    int cache_miss_slow_us = 10000;
    int slow_ipc_us = 2000;
    int slow_window_us = 5000;
    int slow_total_us = 10000;
};

DiagnosticsConfig diagnostics_config();
void set_diagnostics_config(const DiagnosticsConfig& config);
void reset_diagnostics_config();

bool load_diagnostics_config(const std::string& path, DiagnosticsConfig* config);
DiagnosticsConfig load_runtime_diagnostics_config();

const char* diagnostic_trace_mode_name(DiagnosticTraceMode mode);
DiagnosticTraceMode parse_diagnostic_trace_mode(const std::string& mode);

} // namespace cxxime

#endif // CXXIME_DIAGNOSTICS_CONFIG_H_
