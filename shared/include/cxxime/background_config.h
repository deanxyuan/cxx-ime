// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_BACKGROUND_CONFIG_H_
#define CXXIME_BACKGROUND_CONFIG_H_

#include <cmath>
#include <initializer_list>
#include <string>

namespace cxxime {

constexpr double kDefaultBackgroundStrength = 0.15;
constexpr int kBackgroundGeneration = 1;
constexpr unsigned kMaxBackgroundSourcePixels = 32000000;

struct BackgroundView {
    std::string resource;
    int generation = 0;
    std::string fit = "cover";
    double zoom = 1.0;
    double x = 0.5;
    double y = 0.5;
    // Pixel bounds in the oriented source, including filtering margins.
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

struct BackgroundConfig {
    bool enabled = false;
    std::string source;
    std::string name;
    int source_width = 0;
    int source_height = 0;
    BackgroundView horizontal;
    BackgroundView vertical;
    // Absolute image opacity; transparent source pixels retain their own alpha.
    double strength = kDefaultBackgroundStrength;
};

inline bool same_background_framing(const BackgroundView& a, const BackgroundView& b) {
    return a.fit == b.fit && a.zoom == b.zoom && a.x == b.x && a.y == b.y;
}

inline bool valid_background_resource(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

inline void normalize_background_view(BackgroundView& value, int width, int height) {
    if (!valid_background_resource(value.resource) || value.generation != kBackgroundGeneration) {
        value.resource.clear();
    }
    if (value.fit != "cover" && value.fit != "contain") {
        value.fit = "cover";
    }
    if (!std::isfinite(value.zoom) || value.zoom < 0.25 || value.zoom > 4.0) {
        value.zoom = 1.0;
    }
    for (double* position : {&value.x, &value.y}) {
        if (!std::isfinite(*position) || *position < 0.0 || *position > 1.0) {
            *position = 0.5;
        }
    }
    if (value.left < 0 || value.top < 0 || value.width <= 0 || value.height <= 0 ||
        value.left > width || value.top > height || value.width > width - value.left ||
        value.height > height - value.top) {
        value.resource.clear();
        value.left = value.top = value.width = value.height = 0;
    }
}

inline void normalize_background(BackgroundConfig& value) {
    if (!valid_background_resource(value.source) || value.source_width <= 0 ||
        value.source_height <= 0 ||
        static_cast<unsigned>(value.source_width) >
            kMaxBackgroundSourcePixels / value.source_height) {
        value = {};
        return;
    }
    if (value.name.size() > 512) {
        value.name.clear();
    }
    normalize_background_view(value.horizontal, value.source_width, value.source_height);
    normalize_background_view(value.vertical, value.source_width, value.source_height);
    if (!std::isfinite(value.strength) || value.strength < 0.0 || value.strength > 1.0) {
        value.strength = kDefaultBackgroundStrength;
    }
}

} // namespace cxxime
#endif // CXXIME_BACKGROUND_CONFIG_H_
