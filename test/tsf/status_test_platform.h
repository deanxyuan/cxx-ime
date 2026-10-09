// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_TSF_STATUS_TEST_PLATFORM_H_
#define CXXIME_TEST_TSF_STATUS_TEST_PLATFORM_H_

// Forced into this integration target only, after the unmodified Windows declarations.
// Product targets and the native TextService tests do not use these input substitutes.
#include "pch.h"

namespace status_test {

class Desktop {
public:
    explicit Desktop(HWND existing = nullptr);
    ~Desktop();
    Desktop(const Desktop&) = delete;
    Desktop& operator=(const Desktop&) = delete;

private:
    HWND window_;
    bool owns_window_;
};

HWND foreground_window();
BOOL read_keyboard(PBYTE state);
BOOL write_keyboard(LPBYTE state);
SHORT read_key(int key);

} // namespace status_test

#define GetForegroundWindow status_test::foreground_window
#define GetKeyboardState    status_test::read_keyboard
#define SetKeyboardState    status_test::write_keyboard
#define GetKeyState         status_test::read_key

#endif // CXXIME_TEST_TSF_STATUS_TEST_PLATFORM_H_
