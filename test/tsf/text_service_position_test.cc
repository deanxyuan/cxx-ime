// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

#include <cxxime/candidate_window.h>

#include "edit_session.h"
#include "support/candidate_geometry_probe.h"
#include "support/dpi_testutil.h"
#include "support/tsf_presentation_probe.h"

using namespace tsf_test;

namespace {

class PositionView : public HostView {
public:
    PositionView() { text_result = S_OK; }

    std::deque<RECT> positions;
    RECT screen = {};
    int queries = 0;
    std::function<void()> on_text;
    std::function<void()> on_screen;

    STDMETHODIMP GetTextExt(TfEditCookie, ITfRange*, RECT* rect, BOOL* clipped) override {
        ++queries;
        *rect = positions.empty() ? text_rect : positions.front();
        if (positions.size() > 1) {
            positions.pop_front();
        }
        *clipped = FALSE;
        if (on_text) {
            auto callback = std::move(on_text);
            callback();
        }
        return text_result;
    }
    STDMETHODIMP GetScreenExt(RECT* rect) override {
        *rect = screen;
        if (on_screen) {
            auto callback = std::move(on_screen);
            callback();
        }
        return S_OK;
    }
};

class PositionHost {
public:
    PositionHost() {
        window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"CxxIME position test",
                                 WS_OVERLAPPEDWINDOW, 50, 50, 400, 200, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(window != nullptr);
        MONITORINFO monitor = {sizeof(monitor)};
        ASSERT_TRUE(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor));
        work = monitor.rcWork;
    }
    ~PositionHost() { ASSERT_TRUE(DestroyWindow(window)); }

    HWND window = nullptr;
    RECT work = {};
};

} // namespace

TEST(TextServicePosition, first_popup_never_publishes_transient_viewport_projection) {
    test::ScopedDpiAwarenessContext dpi;
    for (bool local : {false, true}) {
        // These are the two log sequences' horizontal displacements. Fit them to the
        // actual monitor: neither DPI, foreground application nor screen count is a fixture.
        for (LONG displacement : {478L, 408L}) {
            PositionHost owner;
            PositionView view;
            Fixture fixture;
            PresentationProbe probe(fixture);
            if (local) {
                TextServiceTestPeer::local_candidate_host(fixture.service, owner.window);
            }
            const LONG x = owner.work.left + 40;
            const LONG y = owner.work.top + (owner.work.bottom - owner.work.top) / 2;
            displacement = (std::min)(displacement, (owner.work.right - x - 80) / 2);
            const RECT real = {x, y, x + 1, y + 20};
            const RECT outside = {x + displacement, y - 30, x + displacement + 1, y - 5};
            view.screen = {x - 10, y - 3, x + displacement + 60, y + 40};

            // Warm the real local window, then hide it as after a previous composition.
            // Attach the probe before the next first show, not after its final position.
            BOOL eaten = FALSE;
            ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
            fixture.service.update_candidate_position({real, {}, cxxime_tsf::CaretSource::Native},
                                                      &fixture.host,
                                                      fixture.service.caret_request());
            const auto warm = probe.wait(true);
            ASSERT_TRUE(fixture.apply(fixture.key(VK_ESCAPE), &eaten));
            probe.wait(false);
            std::unique_ptr<test::CandidateGeometryProbe> geometry;
            if (local) {
                geometry = std::make_unique<test::CandidateGeometryProbe>(
                    reinterpret_cast<HWND>(warm.local_candidate_window));
            }
            const auto first = probe.snapshot_count();
            fixture.host.active_view = &view;
            view.positions = {real, outside, outside};
            ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
            ASSERT_TRUE(eaten != FALSE);
            probe.wait(false);
            ASSERT_EQ(view.queries, 3);
            if (geometry) {
                geometry->assert_hidden();
            }

            view.positions = {real};
            fixture.service.OnLayoutChange(&fixture.host, TF_LC_CHANGE, &view);
            // Settle at a controlled deadline rather than sleeping to win a scheduling race.
            const auto before = probe.snapshot_count();
            TextServiceTestPeer::settle_caret(fixture.service);
            const auto shown = probe.wait(true, before, &real);
            ASSERT_EQ(shown.caret.left, real.left);
            ASSERT_EQ(shown.caret.top, real.top);
            ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
            if (geometry) {
                RECT final_rect = {};
                const HWND candidate = reinterpret_cast<HWND>(shown.local_candidate_window);
                ASSERT_TRUE(IsWindowVisible(candidate));
                ASSERT_TRUE(GetWindowRect(candidate, &final_rect));
                geometry->assert_visible_geometry(final_rect);
            }
            ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
            probe.wait(true);
            probe.assert_visible_positions_since(first, real);
            ASSERT_TRUE(fixture.apply(fixture.key(VK_SPACE), &eaten));
            probe.wait(false);
            ASSERT_TRUE(!fixture.host.range.text.empty());
            geometry.reset();
            fixture.host.active_view = &fixture.view;
        }
    }
}

TEST(TextServicePosition, candidates_returning_in_the_same_composition_start_a_position_budget) {
    test::ScopedDpiAwarenessContext dpi;
    PositionHost owner;
    PositionView view;
    Fixture fixture;
    PresentationProbe probe(fixture);
    TextServiceTestPeer::bind(fixture.service, &fixture.host); // Inline preedit, no popup code.
    fixture.host.active_view = &view;
    const LONG x = owner.work.left + 40;
    const LONG y = owner.work.top + 200;
    view.screen = {x - 10, y - 10, x + 500, y + 80};
    view.text_rect = {x, y, x + 1, y + 20};
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    probe.wait(false);
    ASSERT_EQ(fixture.host.starts, 1);
    ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));

    auto response = fixture.key('I');
    response.candidate_count = 1;
    strcpy_s(response.candidates[0], "candidate");
    for (LONG offset : {10L, 20L, 30L, 40L, 50L, 60L}) {
        view.positions.push_back({x + offset, y, x + offset + 1, y + 20});
    }
    ASSERT_TRUE(fixture.apply(response, &eaten));
    probe.wait(false);
    ASSERT_EQ(fixture.host.starts, 1);
    ASSERT_TRUE(TextServiceTestPeer::caret_poll_pending(fixture.service));
    RECT latest = {};
    for (LONG offset : {100L, 200L}) {
        latest = {x + offset, y, x + offset + 1, y + 20};
        view.positions = {latest};
        fixture.service.OnLayoutChange(&fixture.host, TF_LC_CHANGE, &view);
    }
    // Changing observations cannot require a stable pair forever. The same budget settles
    // the latest real sample, even though the host never started another composition.
    const auto before = probe.snapshot_count();
    TextServiceTestPeer::settle_caret(fixture.service);
    const auto shown = probe.wait(true, before, &latest);
    ASSERT_TRUE(EqualRect(&shown.caret, &latest));
    ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
    ASSERT_EQ(fixture.host.starts, 1);
    ASSERT_TRUE(fixture.apply(fixture.key(VK_ESCAPE), &eaten));
    probe.wait(false);
    fixture.host.active_view = &fixture.view;
}

TEST(TextServicePosition, commit_restart_without_caret_recovers_from_a_window_fallback) {
    test::ScopedDpiAwarenessContext dpi;
    for (bool recover_by_layout : {false, true}) {
        PositionHost owner;
        PositionView view;
        view.text_result = TF_E_NOLAYOUT;
        Fixture fixture;
        PresentationProbe probe(fixture);
        fixture.host.active_view = &view;
        BOOL eaten = FALSE;
        ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
        probe.wait(false);
        fixture.host.defer_write = true;
        auto response = fixture.key('I');
        strcpy_s(response.commit_text, "committed");
        ASSERT_TRUE(fixture.apply(response, &eaten));
        probe.wait(false);
        ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
        ASSERT_EQ(fixture.host.complete_pending(), S_OK);
        fixture.host.defer_write = false;
        ASSERT_TRUE(TextServiceTestPeer::caret_poll_pending(fixture.service));
        ASSERT_EQ(fixture.host.starts, 2);
        // Arrange the post-write deadline without waiting on machine scheduling.
        TextServiceTestPeer::age_completed_restart(fixture.service);
        const LONG x = owner.work.left + 40;
        const LONG y = owner.work.top + 200;
        view.screen = {x, y, x + 300, y + 80};
        const auto sample = TextServiceTestPeer::caret_sample(fixture.service);
        TextServiceTestPeer::poll_without_status(fixture.service);
        const auto shown = probe.wait(true);
        // A foreground fallback may precede the view fallback. Its exact coordinates
        // depend on the desktop; it must not be counted as a new real caret sample.
        ASSERT_TRUE(cxxime_tsf::is_valid_caret_rect(shown.caret));
        ASSERT_EQ(TextServiceTestPeer::caret_sample(fixture.service), sample);
        ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
        const int reads = fixture.host.read_requests;
        const int views = fixture.host.view_requests;
        TextServiceTestPeer::poll_without_status(fixture.service);
        ASSERT_EQ(fixture.host.read_requests, reads);
        ASSERT_EQ(fixture.host.view_requests, views);

        view.text_result = S_OK;
        view.text_rect = {x + 80, y + 10, x + 81, y + 30};
        if (recover_by_layout) {
            // Layout can recover after the bounded polling window, without any new key.
            fixture.service.OnLayoutChange(&fixture.host, TF_LC_CHANGE, &view);
        } else {
            ASSERT_TRUE(fixture.apply(fixture.key('H'), &eaten));
        }
        const auto before = probe.snapshot_count();
        TextServiceTestPeer::settle_caret(fixture.service);
        const auto recovered = probe.wait(true, before, &view.text_rect);
        ASSERT_TRUE(EqualRect(&recovered.caret, &view.text_rect));
        ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
        ASSERT_TRUE(fixture.apply(fixture.key(VK_SPACE), &eaten));
        probe.wait(false);
        fixture.host.active_view = &fixture.view;
    }
}

TEST(TextServicePosition, obsolete_layout_callback_cannot_poison_the_next_viewport_fallback) {
    test::ScopedDpiAwarenessContext dpi;
    for (bool change_target : {false, true}) {
        PositionHost owner;
        PositionView view;
        Fixture fixture;
        PresentationProbe probe(fixture);
        BOOL eaten = FALSE;
        ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
        probe.wait(false);
        fixture.host.active_view = &view;
        const LONG x = owner.work.left + 40;
        const LONG y = owner.work.top + 200;
        view.screen = {x - 10, y - 3, x + 500, y + 40};
        view.text_rect = {x + 300, y, x + 301, y + 20};
        auto* old = new EditSession(&fixture.service, &fixture.host);
        old->set_action(EditSession::Action::UPDATE_CANDIDATE_POSITION);

        if (change_target) {
            TextServiceTestPeer::change_target_generation(fixture.service);
        } else {
            TextServiceTestPeer::replace_presentation(fixture.service);
        }
        const RECT current = {x, y, x + 1, y + 20};
        fixture.service.record_caret_position(
            {current, view.screen, cxxime_tsf::CaretSource::TextExtent});
        const auto serial = TextServiceTestPeer::caret_sample(fixture.service);
        const auto snapshots = probe.snapshot_count();
        ASSERT_EQ(old->DoEditSession(1), S_OK);
        old->Release();
        ASSERT_EQ(view.queries, 0);
        ASSERT_EQ(TextServiceTestPeer::caret_sample(fixture.service), serial);
        ASSERT_EQ(probe.snapshot_count(), snapshots);
        RECT retained = {};
        ASSERT_EQ(fixture.service.resolve_viewport_caret(nullptr, nullptr, false, &retained),
                  cxxime_tsf::CaretSource::Anchor);
        ASSERT_TRUE(EqualRect(&retained, &current));
        fixture.host.active_view = &fixture.view;
    }
}

TEST(TextServicePosition, reentrant_host_query_cannot_write_old_cache_sample_or_snapshot) {
    test::ScopedDpiAwarenessContext dpi;
    for (bool during_screen : {false, true}) {
        for (bool change_target : {false, true}) {
            PositionHost owner;
            PositionView view;
            Fixture fixture;
            PresentationProbe probe(fixture);
            BOOL eaten = FALSE;
            ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
            probe.wait(false);
            fixture.host.active_view = &view;
            const LONG x = owner.work.left + 40;
            const LONG y = owner.work.top + 200;
            view.screen = {x - 10, y - 3, x + 500, y + 40};
            view.text_rect = {x + 300, y, x + 301, y + 20};
            const RECT current = {x, y, x + 1, y + 20};
            std::uint64_t current_serial = 0;
            auto reenter = [&]() {
                if (change_target) {
                    TextServiceTestPeer::change_target_generation(fixture.service);
                } else {
                    TextServiceTestPeer::replace_presentation(fixture.service);
                }
                fixture.service.record_caret_position(
                    {current, view.screen, cxxime_tsf::CaretSource::TextExtent});
                current_serial = TextServiceTestPeer::caret_sample(fixture.service);
            };
            if (during_screen) {
                view.on_screen = reenter;
            } else {
                view.on_text = reenter;
            }
            const auto snapshots = probe.snapshot_count();
            fixture.service.OnLayoutChange(&fixture.host, TF_LC_CHANGE, &view);
            ASSERT_EQ(view.queries, 1);
            ASSERT_GT(current_serial, 0u);
            ASSERT_EQ(TextServiceTestPeer::caret_sample(fixture.service), current_serial);
            ASSERT_EQ(probe.snapshot_count(), snapshots);
            RECT retained = {};
            ASSERT_EQ(fixture.service.resolve_viewport_caret(nullptr, nullptr, false, &retained),
                      cxxime_tsf::CaretSource::Anchor);
            ASSERT_TRUE(EqualRect(&retained, &current));
            fixture.host.active_view = &fixture.view;
        }
    }
}
