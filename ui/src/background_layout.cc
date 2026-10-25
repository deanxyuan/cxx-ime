// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/background_layout.h>

#include <algorithm>
#include <cmath>

namespace cxxime {

BackgroundRect background_target(double width, double height, double inset,
                                 const BackgroundView& config) {
    if (config.fit != "contain") {
        return {0, 0, width, height};
    }
    // A quarter of the short edge is safely inside even a fully rounded end,
    // while leaving a nonempty preview for short windows with large corner radii.
    inset = (std::min)(inset, (std::min)(width, height) / 4.0);
    return {inset, inset, width - 2 * inset, height - 2 * inset};
}

BackgroundRect background_placement(double image_width, double image_height, BackgroundRect target,
                                    const BackgroundView& config) {
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
        target.x + (width <= target.width ? (target.width - width) / 2.0
                                          : std::clamp(target.width / 2.0 - width * config.x,
                                                       target.width - width, 0.0)),
        target.y + (height <= target.height ? (target.height - height) / 2.0
                                            : std::clamp(target.height / 2.0 - height * config.y,
                                                         target.height - height, 0.0)),
        width,
        height,
    };
}

BackgroundRect background_retained_region(int width, int height, const BackgroundView& view) {
    const double extent = view.fit == "cover" && view.zoom > 1 ? 1.0 / view.zoom : 1.0;
    // Two source pixels protect bilinear samples at the outward-rounded boundary.
    const double left =
        (std::max)(0.0, std::floor(std::clamp(view.x - extent / 2, 0.0, 1.0 - extent) * width) - 2);
    const double top =
        (std::max)(0.0,
                   std::floor(std::clamp(view.y - extent / 2, 0.0, 1.0 - extent) * height) - 2);
    const double right =
        (std::min)(static_cast<double>(width),
                   std::ceil((std::clamp(view.x - extent / 2, 0.0, 1.0 - extent) + extent) *
                             width) +
                       2);
    const double bottom =
        (std::min)(static_cast<double>(height),
                   std::ceil((std::clamp(view.y - extent / 2, 0.0, 1.0 - extent) + extent) *
                             height) +
                       2);
    return {left, top, right - left, bottom - top};
}

BackgroundRect background_derivative_placement(const BackgroundConfig& config,
                                               const BackgroundView& view, BackgroundRect target) {
    const auto full = background_placement(config.source_width, config.source_height, target, view);
    if (config.source_width <= 0 || config.source_height <= 0) {
        return {};
    }
    const double scale = full.width / config.source_width;
    return {full.x + view.left * scale, full.y + view.top * scale, view.width * scale,
            view.height * scale};
}

void pan_background(BackgroundView& config, BackgroundRect target, BackgroundRect image, double dx,
                    double dy) {
    if (dx != 0 && image.width > target.width) {
        const double x = std::clamp(image.x + dx, target.x + target.width - image.width, target.x);
        if (x != image.x) {
            config.x = (target.x + target.width / 2.0 - x) / image.width;
        }
    }
    if (dy != 0 && image.height > target.height) {
        const double y =
            std::clamp(image.y + dy, target.y + target.height - image.height, target.y);
        if (y != image.y) {
            config.y = (target.y + target.height / 2.0 - y) / image.height;
        }
    }
}

void zoom_background(BackgroundView& config, double zoom, double image_width, double image_height,
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
    // Letterboxed axes retain their latent focus when zoomed back into view.
    const double dx = before.width > target.width && after.width > target.width ? x - after.x : 0;
    const double dy =
        before.height > target.height && after.height > target.height ? y - after.y : 0;
    pan_background(config, target, after, dx, dy);
}

} // namespace cxxime
