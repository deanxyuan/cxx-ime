// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_BACKGROUND_LAYOUT_H_
#define CXXIME_BACKGROUND_LAYOUT_H_

#include <cxxime/background_config.h>

namespace cxxime {

struct BackgroundRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

BackgroundRect background_target(double width, double height, double inset,
                                 const BackgroundConfig& config);
BackgroundRect background_placement(double image_width, double image_height, BackgroundRect target,
                                    const BackgroundConfig& config);
void pan_background(BackgroundConfig& config, BackgroundRect target, BackgroundRect image,
                    double dx, double dy);
void zoom_background(BackgroundConfig& config, double zoom, double image_width, double image_height,
                     BackgroundRect target, double anchor_x, double anchor_y);

} // namespace cxxime

#endif // CXXIME_BACKGROUND_LAYOUT_H_
