// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/diagnostics_config.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>

#include <json.hpp>

namespace cxxime {
namespace {

std::mutex g_config_mutex;

DiagnosticsConfig default_diagnostics_config() {
    DiagnosticsConfig config;
#ifdef CXXIME_ENABLE_HOST_DIAGNOSTICS
    config.trace_mode = DiagnosticTraceMode::kNormal;
#endif
    return config;
}

DiagnosticsConfig g_config = default_diagnostics_config();

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

void clamp(DiagnosticsConfig& config) {
    const std::size_t one_mib = 1024 * 1024;
    if (config.log_max_size < one_mib)
        config.log_max_size = one_mib;
    if (config.log_max_size > 128 * one_mib)
        config.log_max_size = 128 * one_mib;

    if (config.log_max_files < 1)
        config.log_max_files = 1;
    if (config.log_max_files > 16)
        config.log_max_files = 16;

    if (config.normal_sample_rate < 0)
        config.normal_sample_rate = 0;
    if (config.normal_sample_rate > 100000)
        config.normal_sample_rate = 100000;

    if (config.truncated_sample_rate < 0)
        config.truncated_sample_rate = 0;
    if (config.truncated_sample_rate > 100000)
        config.truncated_sample_rate = 100000;

    if (config.slow_query_us < 1000)
        config.slow_query_us = 1000;
    if (config.cache_miss_slow_us < 1000)
        config.cache_miss_slow_us = 1000;
    if (config.slow_ipc_us < 100)
        config.slow_ipc_us = 100;
    if (config.slow_window_us < 100)
        config.slow_window_us = 100;
    if (config.slow_total_us < 1000)
        config.slow_total_us = 1000;
}

void load_int(const nlohmann::json& obj, const char* key, int& value) {
    if (obj.contains(key) && obj[key].is_number_integer())
        value = obj[key].get<int>();
}

void load_size(const nlohmann::json& obj, const char* key, std::size_t& value) {
    if (obj.contains(key) && obj[key].is_number_unsigned()) {
        value = obj[key].get<std::size_t>();
    } else if (obj.contains(key) && obj[key].is_number_integer()) {
        int raw = obj[key].get<int>();
        if (raw > 0)
            value = static_cast<std::size_t>(raw);
    }
}

bool apply_json(const nlohmann::json& j, DiagnosticsConfig* config) {
    if (!config) {
        return false;
    }
    if (!j.contains("diagnostics") || !j["diagnostics"].is_object()) {
        return true;
    }

    const auto& d = j["diagnostics"];
    if (d.contains("trace_mode") && d["trace_mode"].is_string()) {
        config->trace_mode = parse_diagnostic_trace_mode(d["trace_mode"].get<std::string>());
    }

    load_size(d, "log_max_size", config->log_max_size);
    load_int(d, "log_max_files", config->log_max_files);
    load_int(d, "normal_sample_rate", config->normal_sample_rate);
    load_int(d, "truncated_sample_rate", config->truncated_sample_rate);
    load_int(d, "slow_query_us", config->slow_query_us);
    load_int(d, "cache_miss_slow_us", config->cache_miss_slow_us);
    load_int(d, "slow_ipc_us", config->slow_ipc_us);
    load_int(d, "slow_window_us", config->slow_window_us);
    load_int(d, "slow_total_us", config->slow_total_us);
    clamp(*config);
    return true;
}

} // namespace

const char* diagnostic_trace_mode_name(DiagnosticTraceMode mode) {
    switch (mode) {
        case DiagnosticTraceMode::kOff:
            return "off";
        case DiagnosticTraceMode::kError:
            return "error";
        case DiagnosticTraceMode::kNormal:
            return "normal";
        case DiagnosticTraceMode::kVerbose:
            return "verbose";
    }
    return "off";
}

DiagnosticTraceMode parse_diagnostic_trace_mode(const std::string& mode) {
    std::string value = lower_ascii(mode);
    if (value == "off")
        return DiagnosticTraceMode::kOff;
    if (value == "error" || value == "errors")
        return DiagnosticTraceMode::kError;
    if (value == "normal")
        return DiagnosticTraceMode::kNormal;
    if (value == "verbose")
        return DiagnosticTraceMode::kVerbose;
    return DiagnosticTraceMode::kOff;
}

DiagnosticsConfig diagnostics_config() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    return g_config;
}

void set_diagnostics_config(const DiagnosticsConfig& config) {
    DiagnosticsConfig copy = config;
    clamp(copy);
    std::lock_guard<std::mutex> lock(g_config_mutex);
    g_config = copy;
}

void reset_diagnostics_config() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    g_config = default_diagnostics_config();
}

bool load_diagnostics_config(const std::string& path, DiagnosticsConfig* config) {
    if (path.empty() || !config)
        return false;

    std::ifstream file(path);
    if (!file.is_open())
        return false;

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        return apply_json(j, config);
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool load_diagnostics_config_json(const std::string& json_text, DiagnosticsConfig* config) {
    if (json_text.empty() || !config) {
        return false;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(json_text);
        return apply_json(j, config);
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

} // namespace cxxime
