// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "status_test_platform.h"

#include <cstring>

#include "support/testutil.h"

namespace status_test {
namespace {

// Set before any fixture starts threads; unchanged until all children/routers stop.
HWND foreground = nullptr;
thread_local BYTE keyboard[256] = {};

} // namespace

Desktop::Desktop(HWND existing)
    : window_(existing)
    , owns_window_(existing == nullptr) {
    if (owns_window_) {
        // A real, hidden HWND validates production ownership checks without taking focus.
        window_ = CreateWindowExW(0, L"STATIC", L"CxxIME status test target", WS_POPUP, 0, 0, 1, 1,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    ASSERT_TRUE(IsWindow(window_));
    foreground = window_;
}

Desktop::~Desktop() {
    foreground = nullptr;
    if (owns_window_) {
        ASSERT_TRUE(DestroyWindow(window_));
    }
}

HWND foreground_window() { return foreground; }

BOOL read_keyboard(PBYTE state) {
    std::memcpy(state, keyboard, sizeof(keyboard));
    return TRUE;
}

BOOL write_keyboard(LPBYTE state) {
    std::memcpy(keyboard, state, sizeof(keyboard));
    return TRUE;
}

SHORT read_key(int key) {
    return key >= 0 && key < 256
               ? static_cast<SHORT>(((keyboard[key] & 0x80) ? 0x8000 : 0) | (keyboard[key] & 1))
               : 0;
}

} // namespace status_test
