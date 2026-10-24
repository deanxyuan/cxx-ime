// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/background_layout.h>

#include <algorithm>
#include <cmath>

namespace cxxime {

BackgroundRect background_target(double width, double height, double inset,
                                 const BackgroundConfig& config) {
    if (config.fit != "contain") {
        return {0, 0, width, height};
    }
    // A quarter of the short edge is safely inside even a fully rounded end,
    // while leaving a nonempty preview for short windows with large corner radii.
    inset = (std::min)(inset, (std::min)(width, height) / 4.0);
    return {inset, inset, width - 2 * inset, height - 2 * inset};
}

BackgroundRect background_placement(double image_width, double image_height, BackgroundRect target,
                                    const BackgroundConfig& config) {
    if (image_width <= 0 || image_height <= 0 || target.width <= 0 || target.height <= 0) {
        return {};
    }
    const double sx = target.width / image_width;
    const double sy = target.height / image_height;
    const double scale =
        (config.fit == "contain" ? (std::min)(sx, sy) : (std::max)(sx, sy)) * config.zoom;
    const double width = image_width * scale;
    const double height = image_height * scale;
    return {
        target.x + (target.width - width) * config.x,
        target.y + (target.height - height) * config.y,
        width,
        height,
    };
}

void pan_background(BackgroundConfig& config, BackgroundRect target, BackgroundRect image,
                    double dx, double dy) {
    if (std::abs(target.width - image.width) > 0.000001) {
        config.x = std::clamp(config.x + dx / (target.width - image.width), 0.0, 1.0);
    }
    if (std::abs(target.height - image.height) > 0.000001) {
        config.y = std::clamp(config.y + dy / (target.height - image.height), 0.0, 1.0);
    }
}

void zoom_background(BackgroundConfig& config, double zoom, double image_width, double image_height,
                     BackgroundRect target, double anchor_x, double anchor_y) {
    const auto before = background_placement(image_width, image_height, target, config);
    if (before.width <= 0 || before.height <= 0 || !std::isfinite(zoom)) {
        return;
    }
    zoom = std::clamp(zoom, 0.25, 4.0);
    if (zoom == config.zoom) {
        return;
    }
    config.zoom = zoom;
    const auto after = background_placement(image_width, image_height, target, config);
    const double x = anchor_x - (anchor_x - before.x) * after.width / before.width;
    const double y = anchor_y - (anchor_y - before.y) * after.height / before.height;
    pan_background(config, target, after, x - after.x, y - after.y);
}

} // namespace cxxime
