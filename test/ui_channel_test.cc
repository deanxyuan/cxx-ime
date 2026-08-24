// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <cxxime/ui_channel.h>
#include <cxxime/ui_protocol.h>

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
    return L"\\\\.\\pipe\\CxxIME-UI-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(sequence.fetch_add(1));
}

cxxime::UiPresentationSnapshot make_snapshot(std::uint64_t generation) {
    cxxime::UiPresentationSnapshot snapshot;
    snapshot.session_id = 17;
    snapshot.session_generation = 3;
    snapshot.target_generation = generation;
    snapshot.composition_generation = generation + 10;
    snapshot.presentation_generation = generation + 20;
    snapshot.ownership = cxxime::UiOwnership::kExternal;
    snapshot.flags = cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kComposing) |
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kCandidateVisible) |
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kHasCaret) |
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kHasPreedit) |
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kHasCandidates) |
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kImmersiveMode) |
                     cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kTsfLocalCandidate);
    snapshot.caret = {100, 120, 102, 144};
    std::memcpy(snapshot.preedit, "ni", 2);
    snapshot.preedit_length = 2;
    snapshot.preedit_cursor = 2;
    snapshot.candidate_page.count = 1;
    snapshot.candidate_page.total = 1;
    snapshot.candidate_page.candidates[0].text_length = 9;
    std::memcpy(snapshot.candidate_page.candidates[0].text, "candidate", 9);
    return snapshot;
}

} // namespace

TEST(UiChannel, protocol_round_trip) {
    const cxxime::UiPresentationSnapshot expected = make_snapshot(41);
    std::vector<std::uint8_t> packet;
    ASSERT_TRUE(cxxime::build_ui_snapshot_packet(expected, 9, &packet));
    ASSERT_EQ(packet.size(), sizeof(cxxime::UiPacketHeader) + sizeof(expected));

    cxxime::UiPresentationSnapshot actual;
    ASSERT_TRUE(cxxime::parse_ui_snapshot_packet(packet.data(), packet.size(), &actual));
    ASSERT_EQ(actual.session_id, expected.session_id);
    ASSERT_EQ(actual.target_generation, expected.target_generation);
    ASSERT_EQ(actual.flags, expected.flags);
    ASSERT_EQ(actual.preedit_length, static_cast<std::uint32_t>(2));
    ASSERT_EQ(actual.candidate_page.count, static_cast<std::uint32_t>(1));

    cxxime::UiCommand expected_command;
    expected_command.session_id = expected.session_id;
    expected_command.session_generation = expected.session_generation;
    expected_command.target_generation = expected.target_generation;
    expected_command.composition_generation = expected.composition_generation;
    expected_command.presentation_generation = expected.presentation_generation;
    expected_command.type = cxxime::UiCommandType::kSelectCandidate;
    expected_command.candidate_index = 0;
    ASSERT_TRUE(cxxime::build_ui_command_packet(expected_command, 10, &packet));

    cxxime::UiCommand actual_command;
    ASSERT_TRUE(cxxime::parse_ui_command_packet(packet.data(), packet.size(), &actual_command));
    ASSERT_EQ(actual_command.type, cxxime::UiCommandType::kSelectCandidate);
    ASSERT_EQ(actual_command.composition_generation, expected.composition_generation);
    ASSERT_EQ(actual_command.presentation_generation, expected.presentation_generation);
}

TEST(UiChannel, protocol_rejects_invalid_payloads) {
    cxxime::UiPresentationSnapshot snapshot = make_snapshot(1);
    snapshot.candidate_page.count = static_cast<std::uint32_t>(cxxime::kCandidateCapacity + 1);
    std::vector<std::uint8_t> packet;
    ASSERT_TRUE(!cxxime::build_ui_snapshot_packet(snapshot, 1, &packet));

    snapshot = make_snapshot(1);
    snapshot.preedit_cursor = snapshot.preedit_length + 1;
    ASSERT_TRUE(!cxxime::build_ui_snapshot_packet(snapshot, 1, &packet));

    snapshot = make_snapshot(1);
    ASSERT_TRUE(cxxime::build_ui_snapshot_packet(snapshot, 1, &packet));
    cxxime::UiPacketHeader header;
    std::memcpy(&header, packet.data(), sizeof(header));
    header.protocol_version++;
    std::memcpy(packet.data(), &header, sizeof(header));
    ASSERT_TRUE(!cxxime::parse_ui_snapshot_packet(packet.data(), packet.size(), &snapshot));

    cxxime::UiCommand command;
    command.session_id = 17;
    command.session_generation = 3;
    command.presentation_generation = 1;
    command.type = cxxime::UiCommandType::kSelectCandidate;
    command.candidate_index = cxxime::kCandidateCapacity;
    ASSERT_TRUE(!cxxime::build_ui_command_packet(command, 1, &packet));

    command.type = cxxime::UiCommandType::kRefreshInputIndicator;
    command.candidate_index = 0;
    command.target_generation = 1;
    command.value = 0;
    ASSERT_TRUE(!cxxime::build_ui_command_packet(command, 1, &packet));
    command.target_generation = 0;
    command.presentation_generation = 0;
    ASSERT_TRUE(cxxime::build_ui_command_packet(command, 1, &packet));
    cxxime::UiCommand parsed;
    ASSERT_TRUE(cxxime::parse_ui_command_packet(packet.data(), packet.size(), &parsed));
    ASSERT_EQ(parsed.type, cxxime::UiCommandType::kRefreshInputIndicator);
    command.value = 1;
    ASSERT_TRUE(!cxxime::build_ui_command_packet(command, 1, &packet));
}

TEST(UiChannel, queued_snapshots_coalesce_before_connect) {
    const std::wstring pipe_name = test_pipe_name();
    cxxime::UiChannelClient client;
    ASSERT_TRUE(client.start({}, pipe_name));
    for (std::uint64_t generation = 1; generation <= 20; ++generation) {
        ASSERT_TRUE(client.publish_latest(make_snapshot(generation)));
    }

    std::atomic<int> snapshot_count{0};
    std::atomic<std::uint64_t> received_generation{0};
    cxxime::UiChannelServer server;
    ASSERT_TRUE(server.start(
        [&](cxxime::UiEndpointId, const cxxime::UiPresentationSnapshot& snapshot) {
            received_generation.store(snapshot.target_generation);
            snapshot_count.fetch_add(1);
        },
        {}, pipe_name));

    ASSERT_TRUE(wait_for([&]() { return received_generation.load() == 20; }, 5000));
    ASSERT_EQ(snapshot_count.load(), 1);
    client.stop();
    server.stop();
}

TEST(UiChannel, publish_latest_never_waits_for_a_pipe_connection) {
    const std::wstring pipe_name = test_pipe_name();
    cxxime::UiChannelClient client;
    ASSERT_TRUE(client.start({}, pipe_name));

    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t generation = 1; generation <= 1000; ++generation) {
        ASSERT_TRUE(client.publish_latest(make_snapshot(generation)));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    ASSERT_TRUE(elapsed.count() < 100);
    ASSERT_TRUE(!client.is_connected());
    client.stop();
}

TEST(UiChannel, server_routes_command_to_the_originating_endpoint) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<cxxime::UiEndpointId> endpoint{0};
    cxxime::UiChannelServer server;
    ASSERT_TRUE(server.start(
        [&](cxxime::UiEndpointId value, const cxxime::UiPresentationSnapshot&) {
            endpoint.store(value);
        },
        {}, pipe_name));

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
    ASSERT_TRUE(client.publish_latest(make_snapshot(7)));
    ASSERT_TRUE(wait_for([&]() { return endpoint.load() != 0; }));

    cxxime::UiCommand command;
    command.session_id = 17;
    command.session_generation = 3;
    command.target_generation = 7;
    command.composition_generation = 17;
    command.presentation_generation = 18;
    command.type = cxxime::UiCommandType::kPageNext;
    ASSERT_TRUE(server.send_command(endpoint.load(), command));
    ASSERT_TRUE(wait_for([&]() { return command_count.load() == 1; }));
    {
        std::lock_guard<std::mutex> lock(received_mutex);
        ASSERT_EQ(received.type, cxxime::UiCommandType::kPageNext);
        ASSERT_EQ(received.target_generation, static_cast<std::uint64_t>(7));
    }

    client.stop();
    server.stop();
}

TEST(UiChannel, client_republishes_latest_snapshot_after_server_restart) {
    const std::wstring pipe_name = test_pipe_name();
    cxxime::UiChannelClient client;
    ASSERT_TRUE(client.start({}, pipe_name));
    ASSERT_TRUE(client.publish_latest(make_snapshot(55)));

    {
        std::atomic<std::uint64_t> received{0};
        cxxime::UiChannelServer server;
        ASSERT_TRUE(server.start(
            [&](cxxime::UiEndpointId, const cxxime::UiPresentationSnapshot& snapshot) {
                received.store(snapshot.target_generation);
            },
            {}, pipe_name));
        ASSERT_TRUE(wait_for([&]() { return received.load() == 55; }, 5000));
        server.stop();
    }
    ASSERT_TRUE(wait_for([&]() { return !client.is_connected(); }));
    {
        std::atomic<std::uint64_t> received{0};
        cxxime::UiChannelServer server;
        ASSERT_TRUE(server.start(
            [&](cxxime::UiEndpointId, const cxxime::UiPresentationSnapshot& snapshot) {
                received.store(snapshot.target_generation);
            },
            {}, pipe_name));
        ASSERT_TRUE(wait_for([&]() { return received.load() == 55; }, 5000));
        server.stop();
    }

    client.stop();
}

TEST(UiChannel, disconnect_callback_releases_the_endpoint) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<int> disconnect_count{0};
    cxxime::UiChannelServer server;
    ASSERT_TRUE(
        server.start({}, [&](cxxime::UiEndpointId) { disconnect_count.fetch_add(1); }, pipe_name));

    cxxime::UiChannelClient client;
    ASSERT_TRUE(client.start({}, pipe_name));
    ASSERT_TRUE(wait_for([&]() { return server.endpoint_count() == 1; }));

    client.stop();
    ASSERT_TRUE(wait_for([&]() { return disconnect_count.load() == 1; }));
    ASSERT_EQ(server.endpoint_count(), static_cast<std::size_t>(0));
    server.stop();
}

TEST(UiChannel, server_stop_cancels_active_endpoint_without_waiting_for_a_client) {
    const std::wstring pipe_name = test_pipe_name();
    cxxime::UiChannelServer server;
    ASSERT_TRUE(server.start({}, {}, pipe_name));

    cxxime::UiChannelClient client;
    ASSERT_TRUE(client.start({}, pipe_name));
    ASSERT_TRUE(wait_for([&]() { return server.endpoint_count() == 1; }));

    const auto start = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    ASSERT_TRUE(elapsed.count() < 1000);
    ASSERT_TRUE(!server.is_running());
    client.stop();
}

RUN_ALL_TESTS()
