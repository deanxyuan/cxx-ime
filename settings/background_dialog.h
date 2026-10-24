// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SETTINGS_BACKGROUND_DIALOG_H_
#define CXXIME_SETTINGS_BACKGROUND_DIALOG_H_

#include <optional>
#include <string>

#include <windows.h>

#include <cxxime/background_image.h>
#include <cxxime/config.h>

namespace cxxime {
namespace settings {

struct BackgroundEdit {
    BackgroundConfig config;
    BackgroundImport imported;
};

std::optional<BackgroundEdit> edit_background(HWND owner, const Config& config,
                                              const std::wstring& source = {});
} // namespace settings
} // namespace cxxime
#endif // CXXIME_SETTINGS_BACKGROUND_DIALOG_H_
