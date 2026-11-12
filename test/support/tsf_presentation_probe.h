// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_SUPPORT_TSF_PRESENTATION_PROBE_H_
#define CXXIME_TEST_SUPPORT_TSF_PRESENTATION_PROBE_H_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <cxxime/ui_channel.h>

#include "../tsf/text_service_test_support.h"

namespace tsf_test {

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
    cxxime::UiPresentationSnapshot wait(bool visible, std::size_t after = 0,
                                        const RECT* expected_caret = nullptr) {
        const auto generation = TextServiceTestPeer::generation(service_);
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(changed_.wait_for(
            lock, std::chrono::seconds(3),
            [&]() {
                const bool shown =
                    (latest_.flags &
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kCandidateVisible)) != 0;
                return snapshots_.size() > after && latest_.composition_generation == generation &&
                       shown == visible &&
                       (!expected_caret || EqualRect(&latest_.caret, expected_caret));
            }))
            << "expected_visible=" << visible << " expected_generation=" << generation
            << " received_generation=" << latest_.composition_generation
            << " flags=" << latest_.flags << " snapshots=" << snapshots_.size()
            << " after=" << after;
        return latest_;
    }
    std::size_t snapshot_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshots_.size();
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

    void assert_visible_positions_since(std::size_t begin, const RECT& expected) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool observed_visible = false;
        for (std::size_t i = begin; i < snapshots_.size(); ++i) {
            const auto& snapshot = snapshots_[i];
            if ((snapshot.flags &
                 cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kCandidateVisible)) != 0) {
                observed_visible = true;
                ASSERT_TRUE(EqualRect(&snapshot.caret, &expected));
            }
        }
        ASSERT_TRUE(observed_visible);
    }

private:
    TextService& service_;
    std::mutex mutex_;
    std::condition_variable changed_;
    cxxime::UiPresentationSnapshot latest_;
    std::vector<cxxime::UiPresentationSnapshot> snapshots_;
    cxxime::UiChannelServer server_;
};

} // namespace tsf_test

#endif // CXXIME_TEST_SUPPORT_TSF_PRESENTATION_PROBE_H_
