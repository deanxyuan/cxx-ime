// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_BACKGROUND_CONFIG_H_
#define CXXIME_BACKGROUND_CONFIG_H_

#include <cmath>
#include <initializer_list>
#include <string>

namespace cxxime {

constexpr double kDefaultBackgroundStrength = 0.15;

struct BackgroundConfig {
    bool enabled = false;
    std::string resource;
    std::string name;
    std::string fit = "cover";
    double zoom = 1.0;
    double x = 0.5;
    double y = 0.5;
    // Absolute image opacity; transparent source pixels retain their own alpha.
    double strength = kDefaultBackgroundStrength;
};

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

inline void normalize_background(BackgroundConfig& value) {
    if (!valid_background_resource(value.resource)) {
        value.resource.clear();
    }
    if (value.name.size() > 512) {
        value.name.clear();
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
    if (!std::isfinite(value.strength) || value.strength < 0.0 || value.strength > 1.0) {
        value.strength = kDefaultBackgroundStrength;
    }
}

} // namespace cxxime
#endif
