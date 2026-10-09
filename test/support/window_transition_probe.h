// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_WINDOW_TRANSITION_PROBE_H_
#define CXXIME_TEST_WINDOW_TRANSITION_PROBE_H_

#include <mutex>

#include <windows.h>

#include "testutil.h"

namespace test {

// Observe applied native window operations before the window procedure can repair
// them. Works on the controller's UI thread without changing the product window.
class WindowTransitionProbe {
public:
    explicit WindowTransitionProbe(HWND window, HWND upper = nullptr)
        : window_(window)
        , upper_(upper) {
        ASSERT_TRUE(read_rect(window_, &initial_));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ASSERT_TRUE(active_ == nullptr);
            active_ = this;
        }
        hook_ = SetWindowsHookExW(WH_CALLWNDPROC, observe, nullptr,
                                  GetWindowThreadProcessId(window_, nullptr));
        ASSERT_TRUE(hook_ != nullptr);
    }

    ~WindowTransitionProbe() {
        ASSERT_TRUE(UnhookWindowsHookEx(hook_));
        // A callback already executing must finish before this object's destruction.
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = nullptr;
    }

    WindowTransitionProbe(const WindowTransitionProbe&) = delete;
    WindowTransitionProbe& operator=(const WindowTransitionProbe&) = delete;

    void expect(int shows, int hides, int moves = 0) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ASSERT_EQ(shows_, shows);
        ASSERT_EQ(hides_, hides);
        ASSERT_EQ(moves_, moves) << " initial=" << initial_.left << ',' << initial_.top << ','
                                 << initial_.right << ',' << initial_.bottom
                                 << " first_changed=" << first_changed_.left << ','
                                 << first_changed_.top << ',' << first_changed_.right << ','
                                 << first_changed_.bottom << " flags=" << first_change_.flags
                                 << " windowpos=" << first_change_.x << ',' << first_change_.y
                                 << ',' << first_change_.cx << ',' << first_change_.cy
                                 << " dpi=" << first_change_dpi_
                                 << " thread_context=" << first_thread_context_
                                 << " window_context=" << first_window_context_
                                 << " shows=" << shows_ << " hides=" << hides_;
        ASSERT_EQ(raises_, 0);
        ASSERT_EQ(read_errors_, 0);
    }

private:
    static bool read_rect(HWND window, RECT* rect) {
        // Constructor and hook can run on different threads or DPI contexts.
        // Compare physical coordinates on both sides, never virtualized rectangles.
        const auto previous =
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (!previous) {
            return false;
        }
        const bool read = GetWindowRect(window, rect) != FALSE;
        const bool restored = SetThreadDpiAwarenessContext(previous) != nullptr;
        return read && restored;
    }

    static LRESULT CALLBACK observe(int code, WPARAM wp, LPARAM lp) {
        if (code >= 0) {
            const auto& message = *reinterpret_cast<CWPSTRUCT*>(lp);
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ && message.hwnd == active_->window_ &&
                message.message == WM_WINDOWPOSCHANGED) {
                active_->record(*reinterpret_cast<const WINDOWPOS*>(message.lParam));
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    void record(const WINDOWPOS& position) {
        shows_ += (position.flags & SWP_SHOWWINDOW) != 0;
        hides_ += (position.flags & SWP_HIDEWINDOW) != 0;
        RECT rect = {};
        if (!read_rect(window_, &rect)) {
            ++read_errors_;
            return;
        }
        if (!EqualRect(&rect, &initial_)) {
            if (moves_ == 0) {
                first_changed_ = rect;
                first_change_ = position;
                first_change_dpi_ = GetDpiForWindow(window_);
                first_thread_context_ = GetThreadDpiAwarenessContext();
                first_window_context_ = GetWindowDpiAwarenessContext(window_);
            }
            ++moves_;
        }
        if (upper_ && IsWindowVisible(window_) && IsWindowVisible(upper_)) {
            for (HWND item = GetTopWindow(nullptr); item; item = GetWindow(item, GW_HWNDNEXT)) {
                if (item == upper_) {
                    break;
                }
                if (item == window_) {
                    ++raises_;
                    break;
                }
            }
        }
    }

    inline static std::mutex mutex_;
    inline static WindowTransitionProbe* active_ = nullptr;
    HWND window_;
    HWND upper_;
    HHOOK hook_ = nullptr;
    RECT initial_ = {};
    RECT first_changed_ = {};
    WINDOWPOS first_change_ = {};
    UINT first_change_dpi_ = 0;
    DPI_AWARENESS_CONTEXT first_thread_context_ = nullptr;
    DPI_AWARENESS_CONTEXT first_window_context_ = nullptr;
    int shows_ = 0;
    int hides_ = 0;
    int moves_ = 0;
    int raises_ = 0;
    int read_errors_ = 0;
};

} // namespace test

#endif // CXXIME_TEST_WINDOW_TRANSITION_PROBE_H_
