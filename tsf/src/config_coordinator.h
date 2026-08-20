// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_CONFIG_COORDINATOR_H_
#define CXXIME_TSF_CONFIG_COORDINATOR_H_

#include <cstdint>
#include <memory>

#include <windows.h>

#include <cxxime/config.h>
#include <cxxime/control_protocol.h>

namespace cxxime_tsf {

constexpr UINT WM_CXXIME_CONFIG_CHANGED = WM_APP + 0x314;
constexpr UINT WM_CXXIME_UI_COMMAND = WM_APP + 0x315;

struct ConfigSnapshot {
    cxxime::ConfigGeneration generation;
    std::shared_ptr<const cxxime::Config> config;
};

std::uint32_t allocate_config_subscription_id();
ConfigSnapshot subscribe_config_updates(HWND window, std::uint32_t subscription_id);
void unsubscribe_config_updates(HWND window, std::uint32_t subscription_id);
void shutdown_tsf_log_writer_if_no_config_subscribers();
ConfigSnapshot current_config_snapshot();
void set_status_window_enabled(bool enabled);

} // namespace cxxime_tsf

#endif // CXXIME_TSF_CONFIG_COORDINATOR_H_
