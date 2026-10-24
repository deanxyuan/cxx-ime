// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <cmath>
#include <limits>

#include <cxxime/background_layout.h>
#include <cxxime/config.h>

#include "support/testutil.h"

namespace {
void expect_near(double actual, double expected) {
    ASSERT_LT(std::abs(actual - expected), 0.00001);
}

} // namespace

TEST(BackgroundLayout, presets_and_independent_axis_bounds) {
    cxxime::BackgroundConfig config;
    const cxxime::BackgroundRect target{10, 20, 300, 100};
    for (double width : {1.0, 100.0, 4000.0}) {
        for (double height : {1.0, 100.0, 4000.0}) {
            for (const char* fit : {"cover", "contain"}) {
                config.fit = fit;
                config.zoom = 1;
                auto image = cxxime::background_placement(width, height, target, config);
                expect_near(image.width / image.height, width / height);
                if (config.fit == "cover") {
                    ASSERT_GE(image.width + 0.00001, target.width);
                    ASSERT_GE(image.height + 0.00001, target.height);
                } else {
                    ASSERT_LE(image.width, target.width + 0.00001);
                    ASSERT_LE(image.height, target.height + 0.00001);
                }
                for (double zoom : {0.25, 1.0, 4.0}) {
                    config.zoom = zoom;
                    config.x = config.y = 0.5;
                    image = cxxime::background_placement(width, height, target, config);
                    cxxime::pan_background(config, target, image,
                                           2 * (std::max)(target.width, image.width),
                                           -2 * (std::max)(target.height, image.height));
                    const auto panned = cxxime::background_placement(width, height, target, config);
                    expect_near(panned.x, target.x + (std::max)(0.0, target.width - image.width));
                    expect_near(panned.y, target.y + (std::min)(0.0, target.height - image.height));
                }
            }
        }
    }
}

TEST(BackgroundLayout, zoom_anchor_equal_axis_and_scene_round_trip) {
    cxxime::BackgroundConfig config;
    config.x = 0.3;
    config.y = 0.7;
    const cxxime::BackgroundRect target{0, 0, 400, 200};
    const auto before = cxxime::background_placement(400, 200, target, config);
    cxxime::pan_background(config, target, before, 99, 99);
    expect_near(config.x, 0.3);
    expect_near(config.y, 0.7);
    cxxime::zoom_background(config, 2, 400, 200, target, 100, 50);
    const auto after = cxxime::background_placement(400, 200, target, config);
    expect_near((100 - after.x) / after.width, (100 - before.x) / before.width);
    expect_near((50 - after.y) / after.height, (50 - before.y) / before.height);
    const auto saved = config;
    for (int i = 0; i < 30; ++i) {
        cxxime::background_placement(400, 200, {8, 8, 120, 500}, config);
        const auto returned = cxxime::background_placement(400, 200, target, config);
        expect_near(returned.x, after.x);
        expect_near(returned.y, after.y);
        expect_near(config.zoom, saved.zoom);
    }
    cxxime::zoom_background(config, 1000, 400, 200, target, 0, 0);
    expect_near(config.zoom, 4);
    cxxime::zoom_background(config, -1, 400, 200, target, 0, 0);
    expect_near(config.zoom, 0.25);
    config.x = 0.2550690257394217;
    const auto unchanged = config;
    cxxime::zoom_background(config, 0, 725.3, 200, target, 156, 75);
    ASSERT_EQ(config.x, unchanged.x);
    ASSERT_EQ(config.y, unchanged.y);
    ASSERT_EQ(config.zoom, unchanged.zoom);
}

TEST(BackgroundLayout, contain_safe_rectangle_scales_with_dpi) {
    cxxime::BackgroundConfig config;
    config.fit = "contain";
    for (double scale : {1.0, 1.25, 1.5, 2.0}) {
        const auto target = cxxime::background_target(300 * scale, 100 * scale, 8 * scale, config);
        const auto image = cxxime::background_placement(100, 100, target, config);
        ASSERT_GE(image.x, target.x);
        ASSERT_GE(image.y, target.y);
        ASSERT_LE(image.x + image.width, 292 * scale + 0.00001);
        ASSERT_LE(image.y + image.height, 92 * scale + 0.00001);
    }
    const auto rounded = cxxime::background_target(300, 40, 30, config);
    ASSERT_GT(rounded.width, 0);
    ASSERT_GT(rounded.height, 0);
}

TEST(BackgroundConfig, defaults_validation_and_both_serialization_paths) {
    cxxime::Config config;
    ASSERT_TRUE(config.load_json(R"({"theme":"azure"})"));
    ASSERT_TRUE(!config.background.enabled);
    expect_near(config.background.strength, 0.15);
    config.background.enabled = true;
    config.background.resource = std::string(64, 'a');
    config.background.name = u8"风景.png";
    config.background.fit = "contain";
    config.background.zoom = 1.75;
    config.background.x = 0.1;
    config.background.y = 0.9;
    config.background.strength = 0.6;
    ASSERT_TRUE(config.load_themes(std::string(CXXIME_DATA_DIR) + "themes.json"));
    for (bool runtime : {false, true}) {
        cxxime::Config loaded;
        ASSERT_TRUE(runtime ? loaded.load_runtime_json(config.to_runtime_json())
                            : loaded.load_user_json(config.to_user_json()));
        ASSERT_EQ(loaded.background.resource, config.background.resource);
        ASSERT_EQ(loaded.background.name, config.background.name);
        ASSERT_EQ(loaded.background.fit, "contain");
        expect_near(loaded.background.zoom, 1.75);
        expect_near(loaded.background.x, 0.1);
        expect_near(loaded.background.y, 0.9);
        expect_near(loaded.background.strength, 0.6);
    }
    ASSERT_TRUE(config.load_json(R"({"background":{"zoom":0,"x":-1,"y":2,
        "strength":"nan","resource":"../../default.json","fit":"stretch"}})"));
    expect_near(config.background.zoom, 1);
    expect_near(config.background.x, 0.5);
    expect_near(config.background.y, 0.5);
    expect_near(config.background.strength, 0.15);
    ASSERT_TRUE(config.background.resource.empty());
    ASSERT_EQ(config.background.fit, "cover");
    config.background.zoom = std::numeric_limits<double>::infinity();
    config.background.x = std::numeric_limits<double>::quiet_NaN();
    config.background.strength = std::numeric_limits<double>::quiet_NaN();
    cxxime::normalize_background(config.background);
    expect_near(config.background.zoom, 1);
    expect_near(config.background.x, 0.5);
    expect_near(config.background.strength, 0.15);
}

RUN_ALL_TESTS();
