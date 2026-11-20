// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SETTINGS_LAUNCHER_H_
#define CXXIME_SETTINGS_LAUNCHER_H_

#include <windows.h>

#include <cxxime/settings_route.h>

namespace cxxime {

bool open_settings(SettingsPanel panel = SettingsPanel::kInput);

// Grant foreground permission before posting to an existing settings UI thread.
// Success means the request was queued, not that Windows granted foreground focus.
bool request_settings_activation(HWND window, SettingsPanel panel);

// Called by the settings UI thread after initialization or an explicit open request.
bool activate_settings_window(HWND window);

} // namespace cxxime

#endif // CXXIME_SETTINGS_LAUNCHER_H_
