// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_WINDOW_TRANSITION_PROBE_H_
#define CXXIME_TEST_WINDOW_TRANSITION_PROBE_H_

#include <chrono>
#include <condition_variable>
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
        if (drain_hook_) {
            ASSERT_TRUE(UnhookWindowsHookEx(drain_hook_));
        }
        // A callback already executing must finish before this object's destruction.
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = nullptr;
    }

    WindowTransitionProbe(const WindowTransitionProbe&) = delete;
    WindowTransitionProbe& operator=(const WindowTransitionProbe&) = delete;

    // After the operation has started, wait for the window thread to return to
    // its posted-message loop. This also works while the observed window is hidden.
    void drain() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!drain_hook_) {
            drain_hook_ = SetWindowsHookExW(WH_GETMESSAGE, observe_queued, nullptr,
                                            GetWindowThreadProcessId(window_, nullptr));
            ASSERT_TRUE(drain_hook_ != nullptr);
        }
        drained_ = false;
        ASSERT_TRUE(PostMessageW(window_, WM_NULL, 0, reinterpret_cast<LPARAM>(this)));
        ASSERT_TRUE(
            drained_cv_.wait_for(lock, std::chrono::seconds(3), [&]() { return drained_; }));
    }

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

    void expect_topmost() const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
        const HWND foreground = GetForegroundWindow();
        DWORD foreground_pid = 0;
        GetWindowThreadProcessId(foreground, &foreground_pid);
        ASSERT_TRUE((style & WS_EX_TOPMOST) != 0)
            << " hwnd=" << window_ << " style=" << style
            << " owner=" << GetWindow(window_, GW_OWNER) << " foreground=" << foreground
            << " foreground_pid=" << foreground_pid << " test_pid=" << GetCurrentProcessId()
            << " changing_count=" << changing_count_
            << " changing_after=" << last_changing_.hwndInsertAfter
            << " changing_flags=" << last_changing_.flags
            << " changing_style=" << last_changing_style_ << " changed_count=" << changed_count_
            << " changed_after=" << last_changed_.hwndInsertAfter
            << " changed_flags=" << last_changed_.flags << " changed_style=" << last_changed_style_
            << " dpi=" << GetDpiForWindow(window_)
            << " thread_context=" << GetThreadDpiAwarenessContext()
            << " window_context=" << GetWindowDpiAwarenessContext(window_);
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
            if (active_ && message.hwnd == active_->window_) {
                if (message.message == WM_WINDOWPOSCHANGING) {
                    active_->last_changing_ = *reinterpret_cast<const WINDOWPOS*>(message.lParam);
                    active_->last_changing_style_ = GetWindowLongPtrW(message.hwnd, GWL_EXSTYLE);
                    ++active_->changing_count_;
                } else if (message.message == WM_WINDOWPOSCHANGED) {
                    active_->last_changed_ = *reinterpret_cast<const WINDOWPOS*>(message.lParam);
                    active_->last_changed_style_ = GetWindowLongPtrW(message.hwnd, GWL_EXSTYLE);
                    ++active_->changed_count_;
                    active_->record(active_->last_changed_);
                }
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    static LRESULT CALLBACK observe_queued(int code, WPARAM wp, LPARAM lp) {
        if (code >= 0 && wp == PM_REMOVE) {
            const auto& message = *reinterpret_cast<MSG*>(lp);
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ && message.hwnd == active_->window_ && message.message == WM_NULL &&
                message.lParam == reinterpret_cast<LPARAM>(active_)) {
                active_->drained_ = true;
                active_->drained_cv_.notify_all();
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
    HHOOK drain_hook_ = nullptr;
    RECT initial_ = {};
    RECT first_changed_ = {};
    WINDOWPOS first_change_ = {};
    WINDOWPOS last_changing_ = {};
    WINDOWPOS last_changed_ = {};
    LONG_PTR last_changing_style_ = 0;
    LONG_PTR last_changed_style_ = 0;
    int changing_count_ = 0;
    int changed_count_ = 0;
    UINT first_change_dpi_ = 0;
    DPI_AWARENESS_CONTEXT first_thread_context_ = nullptr;
    DPI_AWARENESS_CONTEXT first_window_context_ = nullptr;
    int shows_ = 0;
    int hides_ = 0;
    int moves_ = 0;
    int raises_ = 0;
    int read_errors_ = 0;
    bool drained_ = false;
    std::condition_variable drained_cv_;
};

} // namespace test

#endif // CXXIME_TEST_WINDOW_TRANSITION_PROBE_H_
