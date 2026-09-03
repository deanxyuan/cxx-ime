// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <windows.h>

#include <cxxime/window_position.h>

#include "support/testutil.h"

namespace {

class ScopedDpiAwarenessContext {
public:
    ScopedDpiAwarenessContext()
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}

    ~ScopedDpiAwarenessContext() {
        if (previous_) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }

private:
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

} // namespace

TEST(WindowPosition, transforms_caret_with_valid_anchor_and_outside_end) {
    ScopedDpiAwarenessContext dpi_context;
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 100, 100, 200, 200,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(window != nullptr);

    RECT client = {};
    ASSERT_TRUE(GetClientRect(window, &client) != FALSE);
    POINT top_left = {client.left, client.top};
    POINT bottom_right = {client.right, client.bottom};
    ASSERT_TRUE(ClientToScreen(window, &top_left) != FALSE);
    ASSERT_TRUE(ClientToScreen(window, &bottom_right) != FALSE);

    RECT source = {bottom_right.x - 1, bottom_right.y - 1, bottom_right.x, bottom_right.y + 20};
    RECT transformed = {};
    ASSERT_TRUE(cxxime::logical_screen_rect_to_physical(window, source, &transformed));
    ASSERT_EQ(source.left, transformed.left);
    ASSERT_EQ(source.top, transformed.top);
    ASSERT_EQ(source.right, transformed.right);
    ASSERT_EQ(source.bottom, transformed.bottom);

    DestroyWindow(window);
}

TEST(WindowPosition, rejects_invalid_window) {
    RECT source = {10, 10, 11, 30};
    RECT transformed = {};
    ASSERT_TRUE(!cxxime::logical_screen_rect_to_physical(nullptr, source, &transformed));
}

TEST(WindowPosition, transforms_caret_when_only_end_anchor_is_inside) {
    ScopedDpiAwarenessContext dpi_context;
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 100, 100, 200, 200,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(window != nullptr);

    RECT client = {};
    ASSERT_TRUE(GetClientRect(window, &client) != FALSE);
    POINT top_left = {client.left, client.top};
    ASSERT_TRUE(ClientToScreen(window, &top_left) != FALSE);

    RECT source = {top_left.x - 20, top_left.y, top_left.x + 1, top_left.y + 20};
    RECT transformed = {};
    ASSERT_TRUE(cxxime::logical_screen_rect_to_physical(window, source, &transformed));
    ASSERT_EQ(source.left, transformed.left);
    ASSERT_EQ(source.top, transformed.top);
    ASSERT_EQ(source.right, transformed.right);
    ASSERT_EQ(source.bottom, transformed.bottom);

    DestroyWindow(window);
}

TEST(WindowPosition, transforms_rect_intersecting_client_without_inside_corners) {
    ScopedDpiAwarenessContext dpi_context;
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 100, 100, 200, 200,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(window != nullptr);

    RECT client = {};
    ASSERT_TRUE(GetClientRect(window, &client) != FALSE);
    POINT top_left = {client.left, client.top};
    POINT bottom_right = {client.right, client.bottom};
    ASSERT_TRUE(ClientToScreen(window, &top_left) != FALSE);
    ASSERT_TRUE(ClientToScreen(window, &bottom_right) != FALSE);

    const LONG center_y = top_left.y + (bottom_right.y - top_left.y) / 2;
    RECT source = {top_left.x - 20, center_y - 10, bottom_right.x + 20, center_y + 10};
    RECT transformed = {};
    ASSERT_TRUE(cxxime::logical_screen_rect_to_physical(window, source, &transformed));
    ASSERT_EQ(source.left, transformed.left);
    ASSERT_EQ(source.top, transformed.top);
    ASSERT_EQ(source.right, transformed.right);
    ASSERT_EQ(source.bottom, transformed.bottom);

    DestroyWindow(window);
}

RUN_ALL_TESTS()
