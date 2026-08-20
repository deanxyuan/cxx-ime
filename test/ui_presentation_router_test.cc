// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>

#include <cxxime/ui_channel.h>

#include "ui_presentation_router.h"
#include "util/testutil.h"

namespace {

bool wait_for(const std::function<bool()>& condition, int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

std::wstring test_pipe_name() {
    static std::atomic<unsigned long> sequence{0};
    return L"\\\\.\\pipe\\CxxIME-UI-Router-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(sequence.fetch_add(1));
}

cxxime::UiPresentationSnapshot make_snapshot(std::uint64_t target_generation, bool visible = true) {
    cxxime::UiPresentationSnapshot snapshot;
    snapshot.session_id = 12;
    snapshot.session_generation = 4;
    snapshot.target_generation = target_generation;
    snapshot.composition_generation = 9;
    snapshot.ownership = cxxime::UiOwnership::kExternal;
    snapshot.flags = cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kComposing) |
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kHasCaret);
    if (visible) {
        snapshot.flags |= cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kCandidateVisible);
    }
    snapshot.caret = {20, 40, 21, 60};
    return snapshot;
}

cxxime::UiCommand make_command(const cxxime::UiPresentationSnapshot& snapshot,
                               cxxime::UiCommandType type, std::uint32_t candidate_index = 0) {
    cxxime::UiCommand command;
    command.session_id = snapshot.session_id;
    command.session_generation = snapshot.session_generation;
    command.target_generation = snapshot.target_generation;
    command.composition_generation = snapshot.composition_generation;
    command.type = type;
    command.candidate_index = candidate_index;
    return command;
}

} // namespace

TEST(UiPresentationRouter, ignores_stale_snapshots_and_routes_bound_commands) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<int> presentation_count{0};
    std::atomic<int> clear_count{0};
    std::atomic<cxxime::UiEndpointId> presented_endpoint{0};
    std::atomic<std::uint64_t> presented_generation{0};
    UiPresentationRouter router;
    ASSERT_TRUE(router.start(
        [&](cxxime::UiEndpointId endpoint, const cxxime::UiPresentationSnapshot* snapshot) {
            if (snapshot) {
                presented_endpoint.store(endpoint);
                presented_generation.store(snapshot->target_generation);
                presentation_count.fetch_add(1);
            } else {
                clear_count.fetch_add(1);
            }
        },
        pipe_name));

    std::atomic<int> command_count{0};
    cxxime::UiCommand received;
    std::mutex received_mutex;
    cxxime::UiChannelClient client;
    ASSERT_TRUE(client.start(
        [&](const cxxime::UiCommand& command) {
            std::lock_guard<std::mutex> lock(received_mutex);
            received = command;
            command_count.fetch_add(1);
        },
        pipe_name));

    const cxxime::UiPresentationSnapshot first = make_snapshot(7);
    ASSERT_TRUE(client.publish_latest(first));
    ASSERT_TRUE(wait_for([&]() { return presented_generation.load() == 7; }));
    ASSERT_TRUE(
        router.send_command(presented_endpoint.load(),
                            make_command(first, cxxime::UiCommandType::kSelectCandidate, 2)));
    ASSERT_TRUE(wait_for([&]() { return command_count.load() == 1; }));
    {
        std::lock_guard<std::mutex> lock(received_mutex);
        ASSERT_EQ(received.type, cxxime::UiCommandType::kSelectCandidate);
        ASSERT_EQ(received.candidate_index, static_cast<std::uint32_t>(2));
        ASSERT_EQ(received.target_generation, static_cast<std::uint64_t>(7));
    }

    ASSERT_TRUE(client.publish_latest(make_snapshot(6)));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(presentation_count.load(), 1);
    ASSERT_EQ(presented_generation.load(), static_cast<std::uint64_t>(7));

    const cxxime::UiPresentationSnapshot replacement = make_snapshot(8);
    ASSERT_TRUE(client.publish_latest(replacement));
    ASSERT_TRUE(wait_for([&]() { return presented_generation.load() == 8; }));
    ASSERT_TRUE(!router.send_command(presented_endpoint.load(),
                                     make_command(first, cxxime::UiCommandType::kPageNext)));
    ASSERT_TRUE(router.send_command(presented_endpoint.load(),
                                    make_command(replacement, cxxime::UiCommandType::kPageNext)));
    ASSERT_TRUE(wait_for([&]() { return command_count.load() == 2; }));

    const cxxime::UiPresentationSnapshot hidden = make_snapshot(9, false);
    ASSERT_TRUE(client.publish_latest(hidden));
    ASSERT_TRUE(wait_for([&]() { return clear_count.load() == 1; }));
    ASSERT_TRUE(!router.send_command(presented_endpoint.load(),
                                     make_command(first, cxxime::UiCommandType::kPageNext)));
    ASSERT_TRUE(!router.send_command(presented_endpoint.load(),
                                     make_command(hidden, cxxime::UiCommandType::kPageNext)));

    client.stop();
    router.stop();
}

TEST(UiPresentationRouter, disconnect_clears_only_the_active_endpoint) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<int> clear_count{0};
    std::atomic<cxxime::UiEndpointId> endpoint{0};
    UiPresentationRouter router;
    ASSERT_TRUE(router.start(
        [&](cxxime::UiEndpointId published_endpoint,
            const cxxime::UiPresentationSnapshot* published_snapshot) {
            if (published_snapshot) {
                endpoint.store(published_endpoint);
            } else {
                clear_count.fetch_add(1);
            }
        },
        pipe_name));

    cxxime::UiChannelClient client;
    ASSERT_TRUE(client.start({}, pipe_name));
    ASSERT_TRUE(client.publish_latest(make_snapshot(3)));
    ASSERT_TRUE(wait_for([&]() { return endpoint.load() != 0; }));
    ASSERT_TRUE(router.send_command(
        endpoint.load(), make_command(make_snapshot(3), cxxime::UiCommandType::kPageNext)));
    client.stop();
    ASSERT_TRUE(wait_for([&]() { return clear_count.load() == 1; }));
    router.stop();
}

RUN_ALL_TESTS()
