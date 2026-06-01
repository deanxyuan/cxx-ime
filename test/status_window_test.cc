// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <cxxime/status_window.h>

// ============================================================
// Create / Destroy
// ============================================================

TEST(StatusWindow, CreateAndDestroy) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(!window.is_created());

    ASSERT_TRUE(window.create(GetDesktopWindow()));
    ASSERT_TRUE(window.is_created());

    window.destroy();
    ASSERT_TRUE(!window.is_created());
}

TEST(StatusWindow, CreateTwice) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));
    ASSERT_TRUE(window.is_created());

    // Second create should return true without crash
    ASSERT_TRUE(window.create(GetDesktopWindow()));
    ASSERT_TRUE(window.is_created());

    window.destroy();
}

TEST(StatusWindow, DestroyWithoutCreate) {
    cxxime::StatusWindow window;
    // Should not crash
    window.destroy();
    ASSERT_TRUE(!window.is_created());
}

// ============================================================
// Show / Hide
// ============================================================

TEST(StatusWindow, ShowHide) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    window.show();
    ASSERT_TRUE(window.is_visible());

    window.hide();
    ASSERT_TRUE(!window.is_visible());

    window.destroy();
}

TEST(StatusWindow, ShowWithoutCreate) {
    cxxime::StatusWindow window;
    // Should be no-op, no crash
    window.show();
    window.hide();
    ASSERT_TRUE(!window.is_created());
}

// ============================================================
// State
// ============================================================

TEST(StatusWindow, UpdateState) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    cxxime::ButtonState state;
    state.chinese_mode = false;
    state.full_shape = true;
    state.chinese_punct = false;
    state.is_pinyin = false;

    // Should not crash
    window.update_state(state);

    window.destroy();
}

TEST(StatusWindow, SetEnabled) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    window.set_enabled(false);
    window.set_enabled(true);

    window.destroy();
}

// ============================================================
// Position
// ============================================================

TEST(StatusWindow, PositionMemory) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    window.set_position(200, 300);
    int x = 0, y = 0;
    window.get_position(x, y);
    ASSERT_EQ(x, 200);
    ASSERT_EQ(y, 300);

    window.set_position(50, 100);
    window.get_position(x, y);
    ASSERT_EQ(x, 50);
    ASSERT_EQ(y, 100);

    window.destroy();
}

TEST(StatusWindow, PositionWithoutCreate) {
    cxxime::StatusWindow window;
    int x = -1, y = -1;
    window.get_position(x, y);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
}

// ============================================================
// Callbacks
// ============================================================

TEST(StatusWindow, ClickCallback) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    int click_count = 0;
    cxxime::StatusButton last_button = cxxime::StatusButton::SETTINGS;
    window.set_click_callback([&](cxxime::StatusButton btn) {
        click_count++;
        last_button = btn;
    });

    window.show();

    // Simulate click on button 0 (中/英): press then release at same position
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(16, 16));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(16, 16));

    ASSERT_EQ(click_count, 1);
    ASSERT_TRUE(last_button == cxxime::StatusButton::CHINESE_MODE);

    window.destroy();
}

TEST(StatusWindow, ClickWhenDisabled) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    int click_count = 0;
    window.set_click_callback([&](cxxime::StatusButton) { click_count++; });

    window.set_enabled(false);

    // Simulate click on button 0
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(16, 16));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(16, 16));

    ASSERT_EQ(click_count, 0);

    window.destroy();
}

TEST(StatusWindow, DragVsClick) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    int click_count = 0;
    int drag_count = 0;
    window.set_click_callback([&](cxxime::StatusButton) { click_count++; });
    window.set_position_callback([&](int, int) { drag_count++; });

    // Simulate drag: press at (16,16), move well past threshold (4px), release
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(16, 16));
    SendMessageW(window.hwnd_for_test(), WM_MOUSEMOVE, 0, MAKELPARAM(116, 16));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(116, 16));

    ASSERT_EQ(click_count, 0);
    ASSERT_EQ(drag_count, 1);

    window.destroy();
}

TEST(StatusWindow, PositionCallback) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    int pos_x = -1, pos_y = -1;
    window.set_position_callback([&](int x, int y) {
        pos_x = x;
        pos_y = y;
    });

    window.destroy();
}

TEST(StatusWindow, ConfigActionCallback) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(GetDesktopWindow()));

    std::string last_action;
    window.set_config_action_callback([&](const std::string& action) {
        last_action = action;
    });

    window.destroy();
}

// ============================================================
// Multiple create/destroy cycles
// ============================================================

TEST(StatusWindow, CreateDestroyCycle) {
    for (int i = 0; i < 3; ++i) {
        cxxime::StatusWindow window;
        ASSERT_TRUE(window.create(GetDesktopWindow()));
        ASSERT_TRUE(window.is_created());
        window.show();
        ASSERT_TRUE(window.is_visible());
        window.hide();
        window.destroy();
        ASSERT_TRUE(!window.is_created());
    }
}

RUN_ALL_TESTS()
