// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <cxxime/ui_channel.h>

#include "text_service_test_support.h"

using namespace tsf_test;

namespace {

// Observe the real UI transport, including intermediate hide snapshots. This does
// not create the server's candidate window or validate host-provided coordinates.
class PresentationProbe {
public:
    explicit PresentationProbe(Fixture& fixture)
        : service_(fixture.service) {
        const std::wstring pipe = fixture.server.pipe + L"_ui";
        ASSERT_TRUE(server_.start(
            [this](cxxime::UiEndpointId, const cxxime::UiPresentationSnapshot& snapshot) {
                std::lock_guard<std::mutex> lock(mutex_);
                latest_ = snapshot;
                snapshots_.push_back(snapshot);
                changed_.notify_all();
            },
            {}, pipe));
        TextServiceTestPeer::start_ui(service_, pipe);
        TextServiceTestPeer::popup_only(service_);
        fixture.host.active_view = &fixture.view;
    }
    ~PresentationProbe() {
        TextServiceTestPeer::stop_ui(service_);
        server_.stop();
    }
    cxxime::UiPresentationSnapshot wait(bool visible) {
        const auto generation = TextServiceTestPeer::generation(service_);
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(changed_.wait_for(lock, std::chrono::seconds(3), [&]() {
            const bool shown =
                (latest_.flags &
                 cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kCandidateVisible)) != 0;
            return latest_.composition_generation == generation && shown == visible;
        }));
        return latest_;
    }
    void assert_visible_since(uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& snapshot : snapshots_) {
            if (snapshot.composition_generation >= generation) {
                ASSERT_TRUE((snapshot.flags & cxxime::ui_snapshot_flag(
                                                  cxxime::UiSnapshotFlag::kCandidateVisible)) != 0);
            }
        }
    }

private:
    TextService& service_;
    std::mutex mutex_;
    std::condition_variable changed_;
    cxxime::UiPresentationSnapshot latest_;
    std::vector<cxxime::UiPresentationSnapshot> snapshots_;
    cxxime::UiChannelServer server_;
};

} // namespace

TEST(TextServicePresentation, unavailable_layout_keeps_displayed_anchor_across_responses) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    BOOL eaten = FALSE;
    auto response = fixture.key('N');
    ASSERT_TRUE(fixture.apply(response, &eaten));
    probe.wait(false);
    ASSERT_TRUE(fixture.service.empty_composition_placeholder_active());

    const RECT anchor = {100, 100, 101, 120};
    fixture.service.update_candidate_position(
        anchor, &fixture.host, false, TextServiceTestPeer::generation(fixture.service), true);
    probe.wait(true);
    const auto sample = TextServiceTestPeer::caret_sample(fixture.service);
    const int reads = fixture.host.read_requests;

    // Key-up returns the same presentation; the next key changes its content.
    response.key_handled = false;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    const auto retained_generation = TextServiceTestPeer::generation(fixture.service);
    auto shown = probe.wait(true);
    ASSERT_EQ(shown.caret.left, anchor.left);
    ASSERT_EQ(TextServiceTestPeer::caret_sample(fixture.service), sample);
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    shown = probe.wait(true);
    ASSERT_EQ(shown.preedit, "ni");
    ASSERT_EQ(shown.caret.top, anchor.top);
    ASSERT_EQ(TextServiceTestPeer::caret_sample(fixture.service), sample);
    ASSERT_GT(fixture.host.read_requests, reads);
    ASSERT_EQ(fixture.host.starts, 1);

    // A subsequent layout event must still move the window to a fresh extent.
    fixture.view.text_rect = {105, 100, 106, 120};
    fixture.view.text_result = S_OK;
    fixture.service.OnLayoutChange(&fixture.host, TF_LC_CHANGE, &fixture.view);
    ASSERT_GT(TextServiceTestPeer::caret_sample(fixture.service), sample);
    ASSERT_TRUE(fixture.apply(fixture.key('H'), &eaten));
    shown = probe.wait(true);
    ASSERT_EQ(shown.caret.left, fixture.view.text_rect.left);
    probe.assert_visible_since(retained_generation);

    ASSERT_TRUE(fixture.apply(fixture.key(VK_ESCAPE), &eaten));
    probe.wait(false);
    fixture.view.text_result = TF_E_NOLAYOUT;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    probe.wait(false);
}

TEST(TextServicePresentation, undisplayed_sample_does_not_skip_initial_layout_wait) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    const RECT old = {100, 100, 101, 120};
    fixture.service.set_caret_rect(old);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    probe.wait(false);
    fixture.service.set_caret_rect(old);
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    probe.wait(false);
}

TEST(TextServicePresentation, changed_target_does_not_retain_displayed_anchor) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    const RECT anchor = {100, 100, 101, 120};
    fixture.service.update_candidate_position(
        anchor, &fixture.host, false, TextServiceTestPeer::generation(fixture.service), true);
    probe.wait(true);
    TextServiceTestPeer::change_target_generation(fixture.service);
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    const auto hidden = probe.wait(false);
    ASSERT_EQ(hidden.caret.left, 0);
}

TEST(TextServicePresentation, commit_restart_does_not_retain_previous_anchor) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    const RECT anchor = {100, 100, 101, 120};
    fixture.service.update_candidate_position(
        anchor, &fixture.host, false, TextServiceTestPeer::generation(fixture.service), true);
    probe.wait(true);
    fixture.host.defer_write = true;
    auto response = fixture.key('I');
    strcpy_s(response.commit_text, "committed");
    ASSERT_TRUE(fixture.apply(response, &eaten));
    probe.wait(false);
    ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_TRUE(TextServiceTestPeer::caret_poll_pending(fixture.service));
    probe.wait(false);
    ASSERT_EQ(fixture.host.starts, 2);
    fixture.host.defer_write = false;
}

TEST(TextServicePresentation, expired_wait_stops_timer_queries_and_recovers_on_layout) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    // Seed an old unresolved wait before the response, so repeated content cannot restart it.
    TextServiceTestPeer::age_caret_wait(fixture.service);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    probe.wait(false);
    TextServiceTestPeer::expire_caret_wait(fixture.service);
    const int reads = fixture.host.read_requests;
    const int views = fixture.host.view_requests;
    for (int tick = 0; tick < 3; ++tick) {
        ASSERT_EQ(TextServiceTestPeer::poll_without_status(fixture.service), 1500u);
    }
    ASSERT_EQ(fixture.host.read_requests, reads);
    ASSERT_EQ(fixture.host.view_requests, views);

    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    probe.wait(false);
    ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
    const int event_reads = fixture.host.read_requests;
    TextServiceTestPeer::poll_without_status(fixture.service);
    ASSERT_EQ(fixture.host.read_requests, event_reads);

    fixture.view.text_rect = {100, 100, 101, 120};
    fixture.view.text_result = S_OK;
    fixture.service.OnLayoutChange(&fixture.host, TF_LC_CHANGE, &fixture.view);
    probe.wait(true);
    ASSERT_GT(fixture.host.read_requests, event_reads);
}

TEST(TextServicePresentation, initial_deadline_settles_provisional_position_and_stops_queries) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    probe.wait(false);
    const RECT provisional = {100, 100, 101, 120};
    TextServiceTestPeer::age_initial_layout(fixture.service, provisional);
    TextServiceTestPeer::poll_without_status(fixture.service);
    const auto shown = probe.wait(true);
    ASSERT_EQ(shown.caret.left, provisional.left);
    ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
    const int views = fixture.host.view_requests;
    TextServiceTestPeer::poll_without_status(fixture.service);
    ASSERT_EQ(fixture.host.view_requests, views);
}

TEST(TextServicePresentation, restart_deadline_settles_saved_extent_without_new_sample) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    probe.wait(false);
    TextServiceTestPeer::age_completed_restart(fixture.service);
    const RECT saved = {100, 100, 101, 120};
    fixture.service.set_caret_rect(saved);
    const auto sample = TextServiceTestPeer::caret_sample(fixture.service);
    TextServiceTestPeer::poll_without_status(fixture.service);
    probe.wait(true);
    ASSERT_EQ(TextServiceTestPeer::caret_sample(fixture.service), sample);
    ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
}

TEST(TextServicePresentation, deadline_without_extent_stops_queries_but_later_input_recovers) {
    Fixture fixture;
    PresentationProbe probe(fixture);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    probe.wait(false);
    TextServiceTestPeer::age_completed_restart(fixture.service);
    TextServiceTestPeer::poll_without_status(fixture.service);
    probe.wait(false);
    ASSERT_TRUE(!TextServiceTestPeer::caret_poll_pending(fixture.service));
    const int reads = fixture.host.read_requests;
    const int views = fixture.host.view_requests;
    TextServiceTestPeer::poll_without_status(fixture.service);
    ASSERT_EQ(fixture.host.read_requests, reads);
    ASSERT_EQ(fixture.host.view_requests, views);

    fixture.view.text_rect = {100, 100, 101, 120};
    fixture.view.text_result = S_OK;
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    probe.wait(true);
}
