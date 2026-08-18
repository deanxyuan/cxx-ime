// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <cxxime/status_window.h>
#include <cxxime/render_context.h>

static bool create_test_window(cxxime::StatusWindow& w) {
    return w.create(GetDesktopWindow(), cxxime::StatusTheme());
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

TEST(StatusWindow, CreateWithOwnerAndDetachOnHide) {
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(owner != nullptr);

    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(owner, cxxime::StatusTheme()));
    ASSERT_EQ(GetWindow(window.hwnd_for_test(), GW_OWNER), owner);

    window.hide();
    ASSERT_TRUE(GetWindow(window.hwnd_for_test(), GW_OWNER) == nullptr);

    window.destroy();
    DestroyWindow(owner);
}

TEST(StatusWindow, DestroyWithoutCreate) {
    cxxime::StatusWindow window;
    // Should not crash
    window.destroy();
    ASSERT_TRUE(!window.is_created());
}

TEST(StatusWindow, EnsureCreatedRecoversDestroyedWindowAndOwner) {
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(owner != nullptr);

    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(nullptr, cxxime::StatusTheme()));
    ASSERT_TRUE(DestroyWindow(window.hwnd_for_test()) != FALSE);
    ASSERT_TRUE(!window.is_created());

    ASSERT_TRUE(window.ensure_created(owner));
    ASSERT_TRUE(window.is_created());
    ASSERT_TRUE(window.owner_matches(owner));

    window.destroy();
    DestroyWindow(owner);
}

TEST(StatusWindow, EnsureCreatedKeepsVisibleWindowShownWhenOwnerChanges) {
    HWND first_owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                       nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND second_owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                        nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(first_owner != nullptr);
    ASSERT_TRUE(second_owner != nullptr);

    cxxime::StatusWindow window;
    ASSERT_TRUE(window.create(first_owner, cxxime::StatusTheme()));
    window.show();
    ASSERT_TRUE(window.is_visible());

    ASSERT_TRUE(window.ensure_created(second_owner));
    ASSERT_TRUE(window.is_visible());
    ASSERT_TRUE(window.owner_matches(second_owner));

    window.destroy();
    DestroyWindow(second_owner);
    DestroyWindow(first_owner);
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

TEST(StatusWindow, UpdateInputMode) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    cxxime::ButtonState state;
    state.input_mode = cxxime::InputMode::PINYIN;
    window.update_state(state);
    state.input_mode = cxxime::InputMode::WUBI;
    window.update_state(state);
    state.input_mode = cxxime::InputMode::MIXED;
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

TEST(StatusWindow, PositionStaysInsideMonitorWorkArea) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    window.set_position(1000000, 1000000);

    RECT window_rect = {};
    ASSERT_TRUE(GetWindowRect(window.hwnd_for_test(), &window_rect));
    HMONITOR monitor = MonitorFromRect(&window_rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {};
    monitor_info.cbSize = sizeof(monitor_info);
    ASSERT_TRUE(GetMonitorInfoW(monitor, &monitor_info));
    ASSERT_TRUE(window_rect.left >= monitor_info.rcWork.left);
    ASSERT_TRUE(window_rect.top >= monitor_info.rcWork.top);
    ASSERT_TRUE(window_rect.right <= monitor_info.rcWork.right);
    ASSERT_TRUE(window_rect.bottom <= monitor_info.rcWork.bottom);

    SetWindowPos(window.hwnd_for_test(), nullptr, 1000000, 1000000, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(window.hwnd_for_test(), WM_DISPLAYCHANGE, 0, 0);
    ASSERT_TRUE(GetWindowRect(window.hwnd_for_test(), &window_rect));
    monitor = MonitorFromRect(&window_rect, MONITOR_DEFAULTTONEAREST);
    monitor_info = {};
    monitor_info.cbSize = sizeof(monitor_info);
    ASSERT_TRUE(GetMonitorInfoW(monitor, &monitor_info));
    ASSERT_TRUE(window_rect.left >= monitor_info.rcWork.left);
    ASSERT_TRUE(window_rect.top >= monitor_info.rcWork.top);
    ASSERT_TRUE(window_rect.right <= monitor_info.rcWork.right);
    ASSERT_TRUE(window_rect.bottom <= monitor_info.rcWork.bottom);

    window.destroy();
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
// Input mode and separator — non-interactive
// ============================================================

TEST(StatusWindow, InputModeClickIgnored) {
    cxxime::StatusWindow window;
    ASSERT_TRUE(create_test_window(window));

    int click_count = 0;
    window.set_click_callback([&](cxxime::StatusButton) { click_count++; });

    window.show();

    // Input mode area: x=6, y=6, w=28, h=22 → center at (20, 17)
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
