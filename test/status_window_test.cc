// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <cxxime/status_window.h>
#include <cxxime/render_context.h>

static HICON load_freedly_icon() {
    static HICON icon = nullptr;
    if (!icon) {
        icon = (HICON)LoadImageW(nullptr,
            CXXIME_PROJECT_DIR L"resource/freedly.ico",
            IMAGE_ICON, 64, 64, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
    }
    return icon;
}

static bool create_test_window(cxxime::StatusWindow& w) {
    bool ok = w.create(GetDesktopWindow(), cxxime::StatusTheme());
    if (ok) w.set_logo_icon(load_freedly_icon());
    return ok;
}

// ============================================================
// Create / Destroy
// ============================================================

TEST(StatusWindow, CreateAndDestroy) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(!window.is_created());

    ASSERT_TRUE(create_test_window(window));
    ASSERT_TRUE(window.is_created());

    window.destroy();
    ASSERT_TRUE(!window.is_created());
}

TEST(StatusWindow, CreateTwice) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));
    ASSERT_TRUE(window.is_created());

    // Second create should return true without crash
    ASSERT_TRUE(create_test_window(window));
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
    ASSERT_TRUE(create_test_window(window));

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
    ASSERT_TRUE(create_test_window(window));

    cxxime::ButtonState state;
    state.chinese_mode = false;
    state.full_shape = true;
    state.chinese_punct = false;

    // Should not crash
    window.update_state(state);

    window.destroy();
}

TEST(StatusWindow, UpdateStateCapsLock) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    cxxime::ButtonState state;
    state.chinese_mode = true;
    state.caps_lock = true;
    state.full_shape = false;
    state.chinese_punct = true;

    // CapsLock display is derived during redraw; this should not crash.
    window.update_state(state);

    window.destroy();
}

TEST(StatusWindow, SetEnabled) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    window.set_enabled(false);
    window.set_enabled(true);

    window.destroy();
}

// ============================================================
// Position
// ============================================================

TEST(StatusWindow, PositionMemory) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

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
    ASSERT_TRUE(create_test_window(window));

    int click_count = 0;
    cxxime::StatusButton last_button = cxxime::StatusButton::SETTINGS;
    window.set_click_callback([&](cxxime::StatusButton btn) {
        click_count++;
        last_button = btn;
    });

    window.show();

    // Button 0 (中/EN) is at x=38, y=6, w=28, h=22 → center at (52, 17)
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(52, 17));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(52, 17));

    ASSERT_EQ(click_count, 1);
    ASSERT_TRUE(last_button == cxxime::StatusButton::CHINESE_MODE);

    window.destroy();
}

TEST(StatusWindow, ClickWhenDisabled) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    int click_count = 0;
    window.set_click_callback([&](cxxime::StatusButton) { click_count++; });

    window.set_enabled(false);

    // Button 0 center at (52, 17)
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(52, 17));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(52, 17));

    ASSERT_EQ(click_count, 0);

    window.destroy();
}

TEST(StatusWindow, DragVsClick) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    int click_count = 0;
    int drag_count = 0;
    window.set_click_callback([&](cxxime::StatusButton) { click_count++; });
    window.set_position_callback([&](int, int) { drag_count++; });

    // Simulate drag: press, move well past threshold (4px), release
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(52, 17));
    SendMessageW(window.hwnd_for_test(), WM_MOUSEMOVE, 0, MAKELPARAM(152, 17));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(152, 17));

    ASSERT_EQ(click_count, 0);
    ASSERT_EQ(drag_count, 1);

    window.destroy();
}

TEST(StatusWindow, PositionCallback) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    int pos_x = -1, pos_y = -1;
    window.set_position_callback([&](int x, int y) {
        pos_x = x;
        pos_y = y;
    });

    window.destroy();
}

TEST(StatusWindow, MenuCommandCallback) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    window.set_menu_command_callback([](cxxime::ImeMenuCommand) {});

    window.destroy();
}

// ============================================================
// Logo and separator — non-interactive (status_window_redesign)
// ============================================================

TEST(StatusWindow, LogoClickIgnored) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    int click_count = 0;
    window.set_click_callback([&](cxxime::StatusButton) { click_count++; });

    window.show();

    // Logo area: x=6, y=6, w=28, h=22 → center at (20, 17)
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(20, 17));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(20, 17));

    ASSERT_EQ(click_count, 0);

    window.destroy();
}

// ============================================================
// Settings button click (index 3, was 4)
// ============================================================

TEST(StatusWindow, SettingsClick) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    cxxime::StatusButton last_button = cxxime::StatusButton::CHINESE_MODE;
    window.set_click_callback([&](cxxime::StatusButton btn) {
        last_button = btn;
    });

    window.show();

    // Settings button (index 3): x after 3 func buttons + separator = ~144
    // x = 6+28+4+84+8+16+1+8 = 155, center at 155+12=167, y=17
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(167, 17));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(167, 17));

    ASSERT_TRUE(last_button == cxxime::StatusButton::SETTINGS);

    window.destroy();
}

// ============================================================
// Multiple create/destroy cycles
// ============================================================

TEST(StatusWindow, CreateDestroyCycle) {
    for (int i = 0; i < 3; ++i) {
        cxxime::StatusWindow window;
        ASSERT_TRUE(create_test_window(window));
        ASSERT_TRUE(window.is_created());
        window.show();
        ASSERT_TRUE(window.is_visible());
        window.hide();
        window.destroy();
        ASSERT_TRUE(!window.is_created());
    }
}

RUN_ALL_TESTS()
