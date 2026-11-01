// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_CANDIDATE_GEOMETRY_PROBE_H_
#define CXXIME_TEST_CANDIDATE_GEOMETRY_PROBE_H_

#include <mutex>
#include <vector>

#include <windows.h>

#include "dpi_testutil.h"
#include "testutil.h"

namespace test {

// Observe the exact test-owned HWND, including synchronous paints before the
// window procedure can correct its position. The controller uses another thread.
class CandidateGeometryProbe {
public:
    struct Event {
        UINT message = 0;
        UINT flags = 0;
        bool visible = false;
        RECT rect = {};
    };

    explicit CandidateGeometryProbe(HWND window)
        : window_(window) {
        DWORD process = 0;
        const DWORD thread = GetWindowThreadProcessId(window_, &process);
        ASSERT_EQ(process, GetCurrentProcessId());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ASSERT_TRUE(active_ == nullptr);
            active_ = this;
        }
        hook_ = SetWindowsHookExW(WH_CALLWNDPROC, observe, nullptr, thread);
        ASSERT_TRUE(hook_ != nullptr);
    }

    ~CandidateGeometryProbe() {
        ASSERT_TRUE(UnhookWindowsHookEx(hook_));
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = nullptr;
    }

    CandidateGeometryProbe(const CandidateGeometryProbe&) = delete;
    CandidateGeometryProbe& operator=(const CandidateGeometryProbe&) = delete;

    std::vector<Event> events() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    void assert_visible_geometry(const RECT& expected) const {
        bool painted = false;
        for (const auto& event : events()) {
            if (!event.visible) {
                continue;
            }
            ASSERT_TRUE(EqualRect(&event.rect, &expected))
                << "message=" << event.message << " flags=" << event.flags
                << " rect=" << event.rect.left << ',' << event.rect.top << ',' << event.rect.right
                << ',' << event.rect.bottom << " expected=" << expected.left << ',' << expected.top
                << ',' << expected.right << ',' << expected.bottom;
            painted = painted || event.message == WM_PAINT;
        }
        ASSERT_TRUE(painted);
    }

    void assert_hidden() const {
        for (const auto& event : events()) {
            ASSERT_TRUE(!event.visible) << "message=" << event.message << " flags=" << event.flags;
        }
    }

    void assert_no_paint() const {
        for (const auto& event : events()) {
            ASSERT_NE(event.message, static_cast<UINT>(WM_PAINT));
        }
    }

private:
    static LRESULT CALLBACK observe(int code, WPARAM wp, LPARAM lp) {
        if (code >= 0) {
            const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lp);
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ && message.hwnd == active_->window_ &&
                (message.message == WM_WINDOWPOSCHANGED || message.message == WM_PAINT)) {
                Event event;
                event.message = message.message;
                if (message.message == WM_WINDOWPOSCHANGED) {
                    event.flags = reinterpret_cast<const WINDOWPOS*>(message.lParam)->flags;
                }
                event.visible = IsWindowVisible(message.hwnd) != FALSE;
                ScopedDpiAwarenessContext dpi;
                ASSERT_TRUE(GetWindowRect(message.hwnd, &event.rect));
                active_->events_.push_back(event);
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    inline static std::mutex mutex_;
    inline static CandidateGeometryProbe* active_ = nullptr;
    HWND window_ = nullptr;
    HHOOK hook_ = nullptr;
    std::vector<Event> events_;
};

} // namespace test

#endif // CXXIME_TEST_CANDIDATE_GEOMETRY_PROBE_H_
