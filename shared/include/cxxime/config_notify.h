// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONFIG_NOTIFY_H_
#define CXXIME_CONFIG_NOTIFY_H_

#include <cstdint>

namespace cxxime {

// Shared memory layout for config change notification.
// Created by TSF DLL, written by Settings, read by all TSF/Server instances.
struct ConfigSharedData {
    volatile LONG config_version;  // Monotonically incremented by Settings via InterlockedIncrement
    LONG reserved;                 // Alignment padding
};

// Called by Settings after saving config.
// Increments config_version in shared memory and signals the Event.
void notify_config_changed();

} // namespace cxxime

#endif // CXXIME_CONFIG_NOTIFY_H_
