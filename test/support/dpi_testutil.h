// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DPI_TEST_UTIL_H_
#define CXXIME_DPI_TEST_UTIL_H_

#include <windows.h>

namespace test {

class ScopedDpiAwarenessContext {
public:
    explicit ScopedDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
        : previous_(SetThreadDpiAwarenessContext(context)) {}

    ~ScopedDpiAwarenessContext() {
        if (previous_) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }

private:
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

} // namespace test

#endif // CXXIME_DPI_TEST_UTIL_H_
