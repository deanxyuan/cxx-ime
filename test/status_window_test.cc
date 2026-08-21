// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <limits>

#include <windows.h>

#include <cxxime/status_window.h>
#include <cxxime/window_position.h>

#include "util/testutil.h"

static bool create_test_window(cxxime::StatusWindow& window) {
    const cxxime::StatusTheme theme;
    return window.create(theme);
}

static int scale_status_metric(HWND hwnd, int metric) {
    return static_cast<int>(metric * (GetDpiForWindow(hwnd) / 96.0f) + 0.5f);
}

static POINT status_button_center(HWND hwnd, int index) {
    int x = scale_status_metric(hwnd, 6);
    x += scale_status_metric(hwnd, 28);
    x += scale_status_metric(hwnd, 4);
    for (int i = 0; i < index; ++i) {
        x += scale_status_metric(hwnd, i < 3 ? 28 : 24);
        x += scale_status_metric(hwnd, 4);
        if (i == 2) {
            x += scale_status_metric(hwnd, 2 * 8 + 1);
        }
    }

    const int width = scale_status_metric(hwnd, index < 3 ? 28 : 24);
    const int center_y = scale_status_metric(hwnd, 6) + scale_status_metric(hwnd, 22) / 2;
    return {x + width / 2, center_y};
}

static POINT input_mode_center(HWND hwnd) {
    return {
        scale_status_metric(hwnd, 6) + scale_status_metric(hwnd, 28) / 2,
        scale_status_metric(hwnd, 6) + scale_status_metric(hwnd, 22) / 2,
    };
}

// ============================================================
// Create / Destroy
// ============================================================

TEST(StatusWindow, CreateAndDestroy) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(!window.is_created());

    ASSERT_TRUE(create_test_window(window));
    ASSERT_TRUE(window.is_created());
    ASSERT_TRUE(GetWindow(window.hwnd_for_test(), GW_OWNER) == nullptr);

    window.destroy();
    ASSERT_TRUE(!window.is_created());
}

TEST(StatusWindow, CreateTwice) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));
    ASSERT_TRUE(window.is_created());
    const HWND first_window = window.hwnd_for_test();

    // Creating an existing window is idempotent.
    ASSERT_TRUE(create_test_window(window));
    ASSERT_TRUE(window.is_created());
    ASSERT_EQ(window.hwnd_for_test(), first_window);

    window.destroy();
}

TEST(StatusWindow, DestroyWithoutCreate) {
    cxxime::StatusWindow window;
    // Destroying an uncreated window is idempotent.
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
    // Showing and hiding an uncreated window are no-ops.
    window.show();
    window.hide();
    ASSERT_TRUE(!window.is_created());
}

TEST(StatusWindow, ShowRestoresTopmostZOrder) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));
    HWND hwnd = window.hwnd_for_test();
    ASSERT_TRUE(SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE);
    ASSERT_TRUE((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);

    window.show();

    ASSERT_TRUE((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    window.destroy();
}

// ============================================================
// Position
// ============================================================

TEST(StatusWindow, ClampPositionToWorkArea) {
    const RECT work_area = {-1600, 120, 1600, 1080};

    POINT position = cxxime::clamp_window_position_to_work_area(-1000, 300, 200, 100, work_area);
    ASSERT_EQ(position.x, -1000);
    ASSERT_EQ(position.y, 300);

    position = cxxime::clamp_window_position_to_work_area(-2000, 0, 200, 100, work_area);
    ASSERT_EQ(position.x, -1600);
    ASSERT_EQ(position.y, 120);

    position = cxxime::clamp_window_position_to_work_area(1500, 1000, 200, 100, work_area);
    ASSERT_EQ(position.x, 1400);
    ASSERT_EQ(position.y, 980);

    position = cxxime::clamp_window_position_to_work_area(300, 500, 4000, 100, work_area);
    ASSERT_EQ(position.x, -1600);
    ASSERT_EQ(position.y, 500);

    position = cxxime::clamp_window_position_to_work_area(-800, 500, 200, 2000, work_area);
    ASSERT_EQ(position.x, -800);
    ASSERT_EQ(position.y, 120);

    position = cxxime::clamp_window_position_to_work_area(-500, 200, 4000, 1000, work_area);
    ASSERT_EQ(position.x, -1600);
    ASSERT_EQ(position.y, 120);

    position = cxxime::clamp_window_position_to_work_area(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 200, 100, work_area);
    ASSERT_EQ(position.x, 1400);
    ASSERT_EQ(position.y, 120);

    position = cxxime::clamp_window_position_to_work_area(1601, 1081, -1, -1, work_area);
    ASSERT_EQ(position.x, 1600);
    ASSERT_EQ(position.y, 1080);
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

    const POINT point = status_button_center(window.hwnd_for_test(), 0);
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(point.x, point.y));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));

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

    const POINT point = status_button_center(window.hwnd_for_test(), 0);
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(point.x, point.y));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));

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

    // Simulate drag: move well past the DPI-scaled threshold before release.
    const POINT point = status_button_center(window.hwnd_for_test(), 0);
    const int drag_x = point.x + scale_status_metric(window.hwnd_for_test(), 100);
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(point.x, point.y));
    SendMessageW(window.hwnd_for_test(), WM_MOUSEMOVE, 0, MAKELPARAM(drag_x, point.y));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(drag_x, point.y));

    ASSERT_EQ(click_count, 0);
    ASSERT_EQ(drag_count, 1);

    window.destroy();
}

// ============================================================
// Input mode is non-interactive
// ============================================================

TEST(StatusWindow, InputModeClickIgnored) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    int click_count = 0;
    window.set_click_callback([&](cxxime::StatusButton) { click_count++; });

    window.show();

    const POINT point = input_mode_center(window.hwnd_for_test());
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(point.x, point.y));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));

    ASSERT_EQ(click_count, 0);

    window.destroy();
}

// ============================================================
// Settings button
// ============================================================

TEST(StatusWindow, SettingsClick) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    cxxime::StatusButton last_button = cxxime::StatusButton::CHINESE_MODE;
    window.set_click_callback([&](cxxime::StatusButton btn) {
        last_button = btn;
    });

    window.show();

    const POINT point = status_button_center(window.hwnd_for_test(), 3);
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0, MAKELPARAM(point.x, point.y));
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));

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
