// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_FOREGROUND_TEST_H_
#define CXXIME_TEST_FOREGROUND_TEST_H_

#include <atomic>

#include <windows.h>

namespace test {

// Forced into selected server tests only. HWND ownership and geometry remain
// native; changing this sample reproduces focus races without stealing focus.
inline std::atomic<HWND> foreground_sample{nullptr};
inline std::atomic<DWORD> foreground_observer_thread{0};
inline std::atomic<unsigned int> foreground_reads{0};

inline HWND foreground_window() {
    if (GetCurrentThreadId() == foreground_observer_thread.load()) {
        ++foreground_reads;
    }
    return foreground_sample.load();
}

} // namespace test

#define GetForegroundWindow test::foreground_window

#endif // CXXIME_TEST_FOREGROUND_TEST_H_
