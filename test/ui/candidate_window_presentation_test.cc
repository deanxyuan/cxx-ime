// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <windows.h>

#include <cxxime/candidate_window.h>

#include "support/candidate_geometry_probe.h"
#include "support/dpi_testutil.h"
#include "support/testutil.h"

namespace {

cxxime::CandidatePage make_page(const char* text) {
    cxxime::CandidatePage page;
    page.candidates.push_back(cxxime::Candidate{text, ""});
    return page;
}

struct Fixture {
    cxxime::Config config;
    cxxime::CandidateWindow window;
    MONITORINFO monitor = {sizeof(monitor)};
    RECT caret = {};

    Fixture(const char* backend, const char* layout) {
        config.render_backend = backend;
        config.layout = layout;
        ASSERT_TRUE(window.create(nullptr, config));
        ASSERT_TRUE(GetMonitorInfoW(
            MonitorFromWindow(window.native_handle(), MONITOR_DEFAULTTONEAREST), &monitor));
        const LONG x =
            monitor.rcMonitor.left + (monitor.rcMonitor.right - monitor.rcMonitor.left) / 3;
        const LONG y =
            monitor.rcMonitor.top + (monitor.rcMonitor.bottom - monitor.rcMonitor.top) / 3;
        caret = {x, y, x + 1, y + 20};
    }

    void show(const char* text) {
        window.set_preedit("n");
        window.update(make_page(text), caret);
        window.show();
        ASSERT_TRUE(window.is_visible());
    }

    RECT rect() const {
        RECT result = {};
        ASSERT_TRUE(window.get_window_rect(&result));
        return result;
    }
};

// Model the synchronous DPI notification delivered while SetWindowPos crosses a monitor.
// This controls callback ordering without requiring a particular monitor topology.
class NestedDpiChange {
public:
    NestedDpiChange(HWND hwnd, UINT dpi, const RECT& suggested, UINT return_dpi = 0)
        : hwnd_(hwnd)
        , dpi_(dpi)
        , return_dpi_(return_dpi)
        , suggested_(suggested) {
        ASSERT_TRUE(active_ == nullptr);
        active_ = this;
        hook_ = SetWindowsHookExW(WH_CALLWNDPROC, observe, nullptr, GetCurrentThreadId());
        ASSERT_TRUE(hook_ != nullptr);
    }

    ~NestedDpiChange() {
        ASSERT_TRUE(UnhookWindowsHookEx(hook_));
        active_ = nullptr;
    }

    bool delivered() const { return delivered_; }

private:
    static LRESULT CALLBACK observe(int code, WPARAM wp, LPARAM lp) {
        if (code >= 0 && active_ && !active_->delivered_) {
            const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lp);
            if (message.hwnd == active_->hwnd_ && message.message == WM_WINDOWPOSCHANGING) {
                active_->delivered_ = true;
                SendMessageW(message.hwnd, WM_DPICHANGED, MAKELPARAM(active_->dpi_, active_->dpi_),
                             reinterpret_cast<LPARAM>(&active_->suggested_));
                if (active_->return_dpi_) {
                    SendMessageW(message.hwnd, WM_DPICHANGED,
                                 MAKELPARAM(active_->return_dpi_, active_->return_dpi_),
                                 reinterpret_cast<LPARAM>(&active_->suggested_));
                }
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    inline static NestedDpiChange* active_ = nullptr;
    HWND hwnd_;
    UINT dpi_;
    UINT return_dpi_;
    RECT suggested_;
    HHOOK hook_ = nullptr;
    bool delivered_ = false;
};

} // namespace

TEST(CandidateWindowPresentation, preparing_an_owner_never_shows_or_paints_the_previous_page) {
    test::ScopedDpiAwarenessContext dpi;
    for (const char* backend : {"gdi", "d2d"}) {
        Fixture fixture(backend, "horizontal");
        fixture.show("previous input");
        {
            test::CandidateGeometryProbe probe(fixture.window.native_handle());
            ASSERT_TRUE(fixture.window.ensure_created(nullptr));
            probe.assert_no_paint();
            ASSERT_TRUE(fixture.window.is_visible());
        }
        const HWND owner =
            CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP, 0, 0, 100, 100, nullptr,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(owner != nullptr);
        {
            test::CandidateGeometryProbe probe(fixture.window.native_handle());
            ASSERT_TRUE(fixture.window.ensure_created(owner));
            ASSERT_TRUE(!fixture.window.is_visible());
            probe.assert_hidden();
        }
        {
            test::CandidateGeometryProbe probe(fixture.window.native_handle());
            OffsetRect(&fixture.caret, 30, 40);
            fixture.show("current input");
            probe.assert_visible_geometry(fixture.rect());
        }
        fixture.window.destroy();
        ASSERT_TRUE(DestroyWindow(owner));
    }
}

TEST(CandidateWindowPresentation, new_content_and_caret_share_one_visible_geometry) {
    test::ScopedDpiAwarenessContext dpi;
    for (const char* backend : {"gdi", "d2d"}) {
        for (const char* layout : {"horizontal", "vertical"}) {
            Fixture fixture(backend, layout);
            fixture.caret.left = fixture.monitor.rcMonitor.right - 20;
            fixture.caret.right = fixture.caret.left + 1;
            fixture.caret.top = fixture.monitor.rcMonitor.bottom - 30;
            fixture.caret.bottom = fixture.caret.top + 20;
            {
                test::CandidateGeometryProbe probe(fixture.window.native_handle());
                fixture.show("WWWWWWWWWWWWWWWW");
                probe.assert_visible_geometry(fixture.rect());
            }
            const RECT wide = fixture.rect();
            {
                test::CandidateGeometryProbe probe(fixture.window.native_handle());
                OffsetRect(&fixture.caret, 0, -40);
                fixture.show("n");
                const RECT narrow = fixture.rect();
                ASSERT_LT(narrow.right - narrow.left, wide.right - wide.left);
                probe.assert_visible_geometry(narrow);
            }
            // A new composition can replace a still-visible page when hidden snapshots coalesce.
            {
                test::CandidateGeometryProbe probe(fixture.window.native_handle());
                fixture.window.reset_placement();
                fixture.caret.top = fixture.monitor.rcMonitor.top + 40;
                fixture.caret.bottom = fixture.caret.top + 20;
                fixture.show("new composition");
                probe.assert_visible_geometry(fixture.rect());
            }
        }
    }
}

TEST(CandidateWindowPresentation, dpi_change_paints_only_the_final_caret_anchored_layout) {
    test::ScopedDpiAwarenessContext dpi;
    for (const char* backend : {"gdi", "d2d"}) {
        for (const int sequence : {0, 1, 2}) {
            Fixture fixture(backend, "horizontal");
            fixture.show("candidate");
            const UINT next_dpi = fixture.window.dpi() == 96 ? 120 : 96;
            const UINT final_dpi = sequence == 2 ? fixture.window.dpi() : next_dpi;
            RECT suggested = fixture.rect();
            OffsetRect(&suggested, fixture.monitor.rcMonitor.left + 8 - suggested.left,
                       fixture.monitor.rcMonitor.top + 8 - suggested.top);
            int callbacks = 0;
            RECT callback_rect = {};
            fixture.window.set_layout_changed_callback([&]() {
                ++callbacks;
                callback_rect = fixture.rect();
                ASSERT_EQ(fixture.window.dpi(), final_dpi);
                ASSERT_EQ(fixture.window.visible_candidate_count(), 1);
            });
            test::CandidateGeometryProbe probe(fixture.window.native_handle());
            if (sequence != 0) {
                NestedDpiChange change(fixture.window.native_handle(), next_dpi, suggested,
                                       sequence == 2 ? final_dpi : 0);
                OffsetRect(&fixture.caret, 0, 40);
                fixture.window.update(make_page("new candidate"), fixture.caret);
                ASSERT_TRUE(change.delivered());
            } else {
                SendMessageW(fixture.window.native_handle(), WM_DPICHANGED,
                             MAKELPARAM(next_dpi, next_dpi), reinterpret_cast<LPARAM>(&suggested));
            }
            const RECT final_rect = fixture.rect();
            ASSERT_EQ(callbacks, 1);
            ASSERT_TRUE(EqualRect(&callback_rect, &final_rect));
            ASSERT_EQ(final_rect.left, fixture.caret.left);
            ASSERT_EQ(final_rect.top, fixture.caret.bottom + 4);
            bool painted = false;
            for (const auto& event : probe.events()) {
                if (!event.visible) {
                    continue;
                }
                ASSERT_TRUE(event.rect.left != suggested.left || event.rect.top != suggested.top);
                if (event.message == WM_PAINT) {
                    ASSERT_TRUE(EqualRect(&event.rect, &final_rect));
                    painted = true;
                } else if (!EqualRect(&event.rect, &final_rect)) {
                    // A real monitor crossing may notify geometry before the new DPI arrives.
                    // It must not copy or draw the old layout at that intermediate geometry.
                    ASSERT_TRUE((event.flags & SWP_NOREDRAW) != 0);
                    ASSERT_TRUE((event.flags & SWP_NOCOPYBITS) != 0);
                }
            }
            ASSERT_TRUE(painted);
        }
    }
}

TEST(CandidateWindowPresentation, preview_without_caret_uses_dpi_suggested_origin) {
    test::ScopedDpiAwarenessContext dpi;
    Fixture fixture("gdi", "horizontal");
    fixture.window.update(make_page("preview"));
    fixture.window.show();
    RECT suggested = fixture.rect();
    OffsetRect(&suggested, fixture.caret.left - suggested.left, fixture.caret.top - suggested.top);
    const UINT next_dpi = fixture.window.dpi() == 96 ? 120 : 96;
    test::CandidateGeometryProbe probe(fixture.window.native_handle());
    SendMessageW(fixture.window.native_handle(), WM_DPICHANGED, MAKELPARAM(next_dpi, next_dpi),
                 reinterpret_cast<LPARAM>(&suggested));
    const RECT final_rect = fixture.rect();
    ASSERT_EQ(final_rect.left, suggested.left);
    ASSERT_EQ(final_rect.top, suggested.top);
    probe.assert_visible_geometry(final_rect);
}
