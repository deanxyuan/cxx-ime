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

#include <cxxime/control_client.h>
#include <cxxime/control_protocol.h>
#include <cxxime/control_server.h>
#include <cxxime/ipc_protocol.h>

#include "util/testutil.h"

namespace {

bool wait_for(const std::function<bool()>& condition, int timeout_ms = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
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
    return L"\\\\.\\pipe\\CxxIME-Control-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(sequence.fetch_add(1));
}

} // namespace

TEST(ControlChannel, protocol_round_trip) {
    const std::string payload = R"({"theme":"azure"})";
    cxxime::ConfigGeneration generation{123, 456};
    std::vector<std::uint8_t> packet;
    ASSERT_TRUE(cxxime::build_control_packet(cxxime::ControlMessageType::kConfigSnapshot,
                                              generation, payload.data(), payload.size(), &packet));

    cxxime::ControlMessage message;
    ASSERT_TRUE(cxxime::parse_control_packet(packet.data(), packet.size(), &message));
    ASSERT_EQ(message.type, cxxime::ControlMessageType::kConfigSnapshot);
    ASSERT_EQ(message.generation.server_epoch, 123ULL);
    ASSERT_EQ(message.generation.revision, 456ULL);
    ASSERT_TRUE(message.payload == payload);
}

TEST(ControlChannel, protocol_rejects_invalid_header_and_oversized_payload) {
    std::vector<std::uint8_t> packet(sizeof(cxxime::ControlHeader), 0);
    cxxime::ControlMessage message;
    ASSERT_TRUE(!cxxime::parse_control_packet(packet.data(), packet.size(), &message));

    std::vector<std::uint8_t> payload(cxxime::CONTROL_MAX_PAYLOAD + 1, 0);
    ASSERT_TRUE(!cxxime::build_control_packet(cxxime::ControlMessageType::kConfigSnapshot, {},
                                               payload.data(), payload.size(), &packet));
}

TEST(ControlChannel, publishes_initial_and_replaced_snapshots) {
    const std::wstring pipe_name = test_pipe_name();
    const std::string initial = R"({"theme":"azure"})";
    const std::string reloaded = R"({"theme":"dark"})";

    cxxime::ControlServer server;
    ASSERT_TRUE(server.start(
        initial,
        [&](cxxime::UserConfigMutationKind kind, const std::string& payload,
            std::string* config_json, unsigned long* error_code) {
            ASSERT_EQ(kind, cxxime::UserConfigMutationKind::kReplace);
            ASSERT_TRUE(payload == reloaded);
            *config_json = reloaded;
            *error_code = ERROR_SUCCESS;
            return true;
        },
        pipe_name));

    std::atomic<int> callback_count{0};
    std::mutex result_mutex;
    cxxime::ConfigGeneration last_generation;
    std::string last_json;
    cxxime::ControlClient client;
    ASSERT_TRUE(client.start(
        [&](cxxime::ConfigGeneration generation, const std::string& config_json) {
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                last_generation = generation;
                last_json = config_json;
            }
            callback_count.fetch_add(1);
        },
        pipe_name));

    ASSERT_TRUE(wait_for([&]() { return callback_count.load() >= 1; }));
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        ASSERT_EQ(last_generation.revision, 1ULL);
        ASSERT_TRUE(last_json == initial);
    }

    cxxime::ConfigGeneration replaced_generation;
    unsigned long error_code = ERROR_SUCCESS;
    ASSERT_TRUE(
        cxxime::replace_user_config(reloaded, &replaced_generation, &error_code, 3000, pipe_name));
    ASSERT_EQ(error_code, static_cast<unsigned long>(ERROR_SUCCESS));
    ASSERT_EQ(replaced_generation.revision, 2ULL);
    ASSERT_TRUE(wait_for([&]() { return callback_count.load() >= 2; }));
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        ASSERT_TRUE(last_generation == replaced_generation);
        ASSERT_TRUE(last_json == reloaded);
    }

    client.stop();
    server.stop();
}

TEST(ControlChannel, failed_mutation_keeps_current_generation) {
    const std::wstring pipe_name = test_pipe_name();
    cxxime::ControlServer server;
    ASSERT_TRUE(server.start(
        R"({"theme":"azure"})",
        [](cxxime::UserConfigMutationKind, const std::string&, std::string*,
           unsigned long* error_code) {
            *error_code = ERROR_INVALID_DATA;
            return false;
        },
        pipe_name));

    cxxime::ConfigGeneration generation;
    unsigned long error_code = ERROR_SUCCESS;
    ASSERT_TRUE(!cxxime::replace_user_config(R"({"theme":"dark"})", &generation, &error_code, 3000,
                                             pipe_name));
    ASSERT_EQ(error_code, static_cast<unsigned long>(ERROR_INVALID_DATA));
    ASSERT_EQ(generation.revision, 1ULL);
    ASSERT_TRUE(generation == server.generation());
    server.stop();
}

TEST(ControlChannel, persistent_client_can_send_merge_patch) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<int> callback_count{0};

    cxxime::ControlServer server;
    ASSERT_TRUE(server.start(
        R"({"theme":"azure"})",
        [](cxxime::UserConfigMutationKind kind, const std::string& payload,
           std::string* config_json, unsigned long* error_code) {
            ASSERT_EQ(kind, cxxime::UserConfigMutationKind::kMergePatch);
            ASSERT_TRUE(payload == R"({"theme":"dark"})");
            *config_json = R"({"theme":"dark"})";
            *error_code = ERROR_SUCCESS;
            return true;
        },
        pipe_name));

    cxxime::ControlClient client;
    ASSERT_TRUE(client.start(
        [&](cxxime::ConfigGeneration, const std::string&) { callback_count.fetch_add(1); },
        pipe_name));
    ASSERT_TRUE(wait_for([&]() { return callback_count.load() >= 1; }));

    client.patch_user_config(R"({"theme":"dark"})");
    ASSERT_TRUE(wait_for([&]() { return callback_count.load() >= 2; }));
    ASSERT_EQ(server.generation().revision, 2ULL);

    client.stop();
    server.stop();
}

TEST(ControlChannel, multiple_subscribers_receive_the_same_generation) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<int> first_count{0};
    std::atomic<int> second_count{0};
    std::mutex generation_mutex;
    cxxime::ConfigGeneration first_generation;
    cxxime::ConfigGeneration second_generation;

    cxxime::ControlServer server;
    ASSERT_TRUE(server.start(
        R"({"theme":"azure"})",
        [](cxxime::UserConfigMutationKind kind, const std::string&, std::string* config_json,
           unsigned long* error_code) {
            ASSERT_EQ(kind, cxxime::UserConfigMutationKind::kReplace);
            *config_json = R"({"theme":"dark"})";
            *error_code = ERROR_SUCCESS;
            return true;
        },
        pipe_name));

    cxxime::ControlClient first;
    cxxime::ControlClient second;
    ASSERT_TRUE(first.start(
        [&](cxxime::ConfigGeneration generation, const std::string&) {
            std::lock_guard<std::mutex> lock(generation_mutex);
            first_generation = generation;
            first_count.fetch_add(1);
        },
        pipe_name));
    ASSERT_TRUE(second.start(
        [&](cxxime::ConfigGeneration generation, const std::string&) {
            std::lock_guard<std::mutex> lock(generation_mutex);
            second_generation = generation;
            second_count.fetch_add(1);
        },
        pipe_name));
    ASSERT_TRUE(wait_for([&]() { return server.subscriber_count() == 2; }));

    ASSERT_TRUE(
        cxxime::replace_user_config(R"({"theme":"dark"})", nullptr, nullptr, 3000, pipe_name));
    ASSERT_TRUE(wait_for([&]() { return first_count.load() >= 2 && second_count.load() >= 2; }));
    {
        std::lock_guard<std::mutex> lock(generation_mutex);
        ASSERT_TRUE(first_generation == second_generation);
        ASSERT_TRUE(first_generation == server.generation());
    }

    first.stop();
    second.stop();
    server.stop();
}

TEST(ControlChannel, client_stops_promptly_without_server) {
    cxxime::ControlClient client;
    ASSERT_TRUE(client.start({}, test_pipe_name()));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto start = std::chrono::steady_clock::now();
    client.stop();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    ASSERT_TRUE(elapsed.count() < 1000);
}

TEST(ControlChannel, queued_patch_is_sent_after_server_connects) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<int> mutation_count{0};
    std::mutex snapshot_mutex;
    std::string last_snapshot;

    cxxime::ControlClient client;
    ASSERT_TRUE(client.start(
        [&](cxxime::ConfigGeneration, const std::string& config_json) {
            std::lock_guard<std::mutex> lock(snapshot_mutex);
            last_snapshot = config_json;
        },
        pipe_name));
    client.patch_user_config(R"({"theme":"dark"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    cxxime::ControlServer server;
    ASSERT_TRUE(server.start(
        R"({"theme":"azure"})",
        [&](cxxime::UserConfigMutationKind kind, const std::string&, std::string* config_json,
            unsigned long* error_code) {
            ASSERT_EQ(kind, cxxime::UserConfigMutationKind::kMergePatch);
            mutation_count.fetch_add(1);
            *config_json = R"({"theme":"dark"})";
            *error_code = ERROR_SUCCESS;
            return true;
        },
        pipe_name));

    ASSERT_TRUE(wait_for([&]() { return mutation_count.load() == 1; }, 5000));
    ASSERT_TRUE(wait_for(
        [&]() {
            std::lock_guard<std::mutex> lock(snapshot_mutex);
            return last_snapshot == R"({"theme":"dark"})";
        },
        5000));
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        ASSERT_TRUE(last_snapshot == R"({"theme":"dark"})");
    }

    client.stop();
    server.stop();
}

TEST(ControlChannel, server_restart_changes_epoch) {
    const std::wstring pipe_name = test_pipe_name();
    std::atomic<int> callback_count{0};
    std::mutex generation_mutex;
    cxxime::ConfigGeneration first_generation;
    cxxime::ConfigGeneration latest_generation;

    cxxime::ControlClient client;
    ASSERT_TRUE(client.start(
        [&](cxxime::ConfigGeneration generation, const std::string&) {
            std::lock_guard<std::mutex> lock(generation_mutex);
            if (callback_count.load() == 0) {
                first_generation = generation;
            }
            latest_generation = generation;
            callback_count.fetch_add(1);
        },
        pipe_name));

    {
        cxxime::ControlServer server;
        ASSERT_TRUE(server.start(R"({"theme":"azure"})", {}, pipe_name));
        ASSERT_TRUE(wait_for([&]() { return callback_count.load() >= 1; }));
        server.stop();
    }
    {
        cxxime::ControlServer server;
        ASSERT_TRUE(server.start(R"({"theme":"dark"})", {}, pipe_name));
        ASSERT_TRUE(wait_for([&]() { return callback_count.load() >= 2; }, 5000));
        server.stop();
    }

    client.stop();
    std::lock_guard<std::mutex> lock(generation_mutex);
    ASSERT_TRUE(first_generation.server_epoch != latest_generation.server_epoch);
    ASSERT_EQ(latest_generation.revision, 1ULL);
}

TEST(ControlChannel, input_protocol_size_matches_packed_layout) {
    ASSERT_EQ(sizeof(cxxime::IPCRequest), static_cast<std::size_t>(217));
    ASSERT_EQ(sizeof(cxxime::IPCResponse), static_cast<std::size_t>(4446));
}

RUN_ALL_TESTS()
