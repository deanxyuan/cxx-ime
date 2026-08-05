// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/ipc_client.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/ipc_server.h>
#include <cxxime/pipe_names.h>

#include "util/testutil.h"

// ============================================================
// Helper
// ============================================================
struct TestServer {
    cxxime::IpcServer server;
    bool start(cxxime::IpcServer::RequestHandler h) {
        server.set_handler(std::move(h));
        bool ok = server.start();
        if (ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return ok;
    }
    ~TestServer() { server.stop(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
};

static cxxime::IPCResponse make_response(cxxime::IPCStatus status) {
    cxxime::IPCResponse resp = {};
    resp.status = status;
    return resp;
}

// ============================================================
// Protocol Tests
// ============================================================

TEST(Protocol, pipe_name) {
    ASSERT_TRUE(wcscmp(cxxime::IPC_PIPE_BASE_NAME, L"\\\\.\\pipe\\CxxIME") == 0);
    ASSERT_TRUE(wcscmp(cxxime::CONTROL_PIPE_BASE_NAME, L"\\\\.\\pipe\\CxxIME-Control") == 0);
}

TEST(Protocol, user_pipe_name_preserves_endpoint_and_is_idempotent) {
    const std::wstring input = cxxime::make_user_pipe_name(cxxime::IPC_PIPE_BASE_NAME);
    const std::wstring control = cxxime::make_user_pipe_name(cxxime::CONTROL_PIPE_BASE_NAME);

    ASSERT_TRUE(input.size() >= 6 && input.substr(input.size() - 6) == L"CxxIME");
    ASSERT_TRUE(control.size() >= 14 && control.substr(control.size() - 14) == L"CxxIME-Control");
    ASSERT_TRUE(input != control);
    ASSERT_TRUE(cxxime::make_user_pipe_name(input) == input);
    ASSERT_TRUE(cxxime::make_user_pipe_name(L"relative-name") == L"relative-name");
}

TEST(Protocol, request_struct_size) {
    ASSERT_EQ(sizeof(cxxime::IPCRequest), static_cast<size_t>(28));
}

TEST(Protocol, response_struct_size) {
    ASSERT_EQ(sizeof(cxxime::IPCResponse), static_cast<size_t>(3176));
}

TEST(Protocol, response_zero_init) {
    cxxime::IPCResponse resp = {};
    ASSERT_EQ(resp.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(resp.commit_text[0], '\0');
    ASSERT_EQ(resp.preedit[0], '\0');
    ASSERT_EQ(resp.preedit_cursor, (uint32_t)0);
    ASSERT_EQ(resp.candidate_count, (uint32_t)0);
    ASSERT_EQ(resp.key_handled, (uint32_t)0);
}

TEST(Protocol, candidate_text_over_old_capacity_round_trips) {
    const std::string candidate(200, 'x');
    TestServer server;
    ASSERT_TRUE(server.start([&](const cxxime::IPCRequest&) {
        cxxime::IPCResponse response = {};
        response.candidate_count = 1;
        strcpy_s(response.candidates[0], candidate.c_str());
        return response;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    cxxime::IPCRequest request = {};
    request.command = cxxime::IPCCommand::PING;
    cxxime::IPCResponse response = {};
    ASSERT_TRUE(client.send_request(request, response));
    ASSERT_TRUE(std::string(response.candidates[0]) == candidate);
}

TEST(Protocol, ime_status_flags_are_independent) {
    cxxime::ImeStatus status;
    ASSERT_TRUE(status.chinese_mode());
    ASSERT_TRUE(!status.caps_lock());
    ASSERT_TRUE(!status.full_shape());
    ASSERT_TRUE(status.chinese_punct());

    status.set_chinese_mode(false);
    status.set_caps_lock(true);
    status.set_full_shape(true);
    status.set_chinese_punct(false);

    ASSERT_TRUE(!status.chinese_mode());
    ASSERT_TRUE(status.caps_lock());
    ASSERT_TRUE(status.full_shape());
    ASSERT_TRUE(!status.chinese_punct());
    ASSERT_EQ(status.flags,
              cxxime::ime_status_flag(cxxime::ImeStatusFlag::CAPS_LOCK) |
              cxxime::ime_status_flag(cxxime::ImeStatusFlag::FULL_SHAPE));
}

// ============================================================
// Server Lifecycle Tests
// ============================================================

TEST(Server, start_stop) {
    cxxime::IpcServer server;
    server.set_handler([](const cxxime::IPCRequest&) -> cxxime::IPCResponse { return {}; });
    ASSERT_TRUE(server.start());
    server.stop();
}

TEST(Server, double_stop) {
    cxxime::IpcServer server;
    server.set_handler([](const cxxime::IPCRequest&) -> cxxime::IPCResponse { return {}; });
    server.start();
    server.stop();
    server.stop();
}

// ============================================================
// Client Connection Tests
// ============================================================

TEST(Client, connect_no_server) {
    cxxime::IpcClient client;
    ASSERT_TRUE(!client.connect(cxxime::IPC_PIPE_BASE_NAME, 300));
    ASSERT_TRUE(!client.is_connected());
}

TEST(Client, connect_with_server) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse { return {}; }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    ASSERT_TRUE(client.is_connected());
    client.disconnect();
    ASSERT_TRUE(!client.is_connected());
}

TEST(Client, disconnect_idempotent) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse { return {}; }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    client.disconnect();
    client.disconnect();
}

// ============================================================
// IPC Command Tests
// ============================================================

TEST(IPC, start_session) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::START_SESSION) { resp.status = cxxime::IPCStatus::OK; resp.highlighted = 42; }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    uint32_t sid = 0;
    ASSERT_TRUE(client.start_session(sid));
    ASSERT_EQ(sid, (uint32_t)42);
}

TEST(IPC, end_session) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::END_SESSION) resp.status = cxxime::IPCStatus::OK;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    ASSERT_TRUE(client.end_session(1));
}

TEST(IPC, ping) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        resp.status = req.command == cxxime::IPCCommand::PING
            ? cxxime::IPCStatus::OK
            : cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    ASSERT_TRUE(client.ping());
}

TEST(IPC, process_key_preedit) {
    TestServer ts;
    uint32_t visible_candidate_count = 0;
    ASSERT_TRUE(ts.start([&](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::PROCESS_KEY) {
            visible_candidate_count = req.visible_candidate_count;
            resp.status = cxxime::IPCStatus::OK;
            strncpy_s(resp.preedit, "ni", sizeof(resp.preedit) - 1);
            resp.preedit_cursor = 1;
            resp.candidate_count = 2;
            resp.candidate_offset = 4;
            resp.candidate_total = 12;
            strncpy_s(resp.candidates[0], "你", sizeof(resp.candidates[0]) - 1);
            strncpy_s(resp.candidates[1], "尼", sizeof(resp.candidates[1]) - 1);
            strncpy_s(resp.candidate_hints[1], "a", sizeof(resp.candidate_hints[1]) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.process_key(1, 'N', 0, resp, false, 2));
    ASSERT_EQ(resp.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(visible_candidate_count, 2u);
    ASSERT_EQ(strcmp(resp.preedit, "ni"), 0);
    ASSERT_EQ(resp.preedit_cursor, 1u);
    ASSERT_EQ(resp.candidate_count, (uint32_t)2);
    ASSERT_EQ(resp.candidate_offset, 4u);
    ASSERT_EQ(resp.candidate_total, 12u);
    ASSERT_EQ(strcmp(resp.candidate_hints[0], ""), 0);
    ASSERT_EQ(strcmp(resp.candidate_hints[1], "a"), 0);
}

TEST(IPC, request_timeout_disconnects_client) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return make_response(cxxime::IPCStatus::OK);
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 100));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(!client.ping(&resp));
    ASSERT_TRUE(!client.is_connected());
}

TEST(IPC, process_key_commit) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::PROCESS_KEY) {
            resp.status = cxxime::IPCStatus::OK;
            strncpy_s(resp.commit_text, "你", sizeof(resp.commit_text) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.process_key(1, '1', 0, resp));
    ASSERT_EQ(resp.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(strcmp(resp.commit_text, "你"), 0);
}

TEST(IPC, process_key_rejected) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::PROCESS_KEY) resp.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.process_key(1, VK_RETURN, 0, resp));
    ASSERT_EQ(resp.status, cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED);
}

TEST(IPC, select_candidate) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::SELECT_CANDIDATE) {
            resp.status = cxxime::IPCStatus::OK;
            strncpy_s(resp.commit_text, "你好", sizeof(resp.commit_text) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.select_candidate(1, 0, resp));
    ASSERT_EQ(resp.status, cxxime::IPCStatus::OK);
}

TEST(IPC, commit_composition) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::COMMIT_COMPOSITION) {
            resp.status = cxxime::IPCStatus::OK;
            strncpy_s(resp.commit_text, "测试", sizeof(resp.commit_text) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.commit_composition(1, resp));
    ASSERT_EQ(resp.status, cxxime::IPCStatus::OK);
}

TEST(IPC, set_chinese_mode_uses_explicit_target) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        ASSERT_EQ(req.command, cxxime::IPCCommand::SET_CHINESE_MODE);
        ASSERT_EQ(req.session_id, static_cast<uint32_t>(7));
        ASSERT_EQ(req.candidate_index, static_cast<uint32_t>(0));
        resp.status = cxxime::IPCStatus::OK;
        resp.ime_status.set_chinese_mode(false);
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.set_chinese_mode(7, false, resp));
    ASSERT_EQ(resp.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(resp.ime_status.chinese_mode(), false);
}

TEST(IPC, focus_in_out) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
        return make_response(cxxime::IPCStatus::OK);
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    ASSERT_TRUE(client.focus_in(1));
    ASSERT_TRUE(client.focus_out(1));
}

TEST(IPC, send_request) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::CLEAR_COMPOSITION) resp.status = static_cast<cxxime::IPCStatus>(99);
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));

    cxxime::IPCRequest req = {};
    req.command = cxxime::IPCCommand::CLEAR_COMPOSITION;
    req.session_id = 1;
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.send_request(req, resp));
    ASSERT_EQ(resp.status, static_cast<cxxime::IPCStatus>(99));
}

// ============================================================
// Multi-Client Tests
// ============================================================

TEST(MultiClient, two_clients_simultaneous) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        resp.status = static_cast<cxxime::IPCStatus>(req.session_id * 10);
        return resp;
    }));

    cxxime::IpcClient client1;
    cxxime::IpcClient client2;
    ASSERT_TRUE(client1.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
    ASSERT_TRUE(client2.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));

    cxxime::IPCResponse resp1 = {};
    cxxime::IPCResponse resp2 = {};
    ASSERT_TRUE(client1.process_key(1, 'A', 0, resp1));
    ASSERT_TRUE(client2.process_key(2, 'B', 0, resp2));
    ASSERT_EQ(resp1.status, static_cast<cxxime::IPCStatus>(10));
    ASSERT_EQ(resp2.status, static_cast<cxxime::IPCStatus>(20));

    client1.disconnect();
    client2.disconnect();
}

TEST(MultiClient, sequential_sessions) {
    // Ensure previous test's server is fully cleaned up
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    cxxime::IpcServer server;
    int call_count = 0;
    server.set_handler([&](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::START_SESSION) {
            ++call_count;
            resp.status = cxxime::IPCStatus::OK;
            resp.highlighted = static_cast<uint32_t>(call_count);
        } else {
            resp.status = cxxime::IPCStatus::OK;
        }
        return resp;
    });
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));

    uint32_t sid1 = 0;
    ASSERT_TRUE(client.start_session(sid1));
    ASSERT_EQ(sid1, (uint32_t)1);
    ASSERT_TRUE(client.end_session(sid1));

    uint32_t sid2 = 0;
    ASSERT_TRUE(client.start_session(sid2));
    ASSERT_EQ(sid2, (uint32_t)2);
    ASSERT_TRUE(client.end_session(sid2));

    client.disconnect();
    server.stop();
}

// ============================================================
// Reconnection Tests
// ============================================================

TEST(Reconnect, server_restart) {
    {
        TestServer ts;
        ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
            return make_response(cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED);
        }));
        cxxime::IpcClient client;
        ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
        cxxime::IPCResponse resp = {};
        ASSERT_TRUE(client.process_key(1, 'A', 0, resp));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        TestServer ts;
        ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
            return make_response(cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED);
        }));
        cxxime::IpcClient client;
        ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));
        cxxime::IPCResponse resp = {};
        ASSERT_TRUE(client.process_key(1, 'A', 0, resp));
    }
}

// ============================================================
// Error Handling Tests
// ============================================================

TEST(Error, unknown_command) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        resp.status = cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));

    cxxime::IPCRequest req = {};
    req.command = static_cast<cxxime::IPCCommand>(255);  // invalid
    req.session_id = 1;

    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.send_request(req, resp));
    ASSERT_EQ(resp.status, cxxime::IPCStatus::ERR_UNKNOWN_COMMAND);
    client.disconnect();
}

TEST(Error, invalid_session) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::PROCESS_KEY && req.session_id == 0) {
            resp.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
        } else {
            resp.status = cxxime::IPCStatus::OK;
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));

    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.process_key(0, 'A', 0, resp));  // session_id=0 is invalid
    ASSERT_EQ(resp.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
    client.disconnect();
}

TEST(Error, engine_not_initialized) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::START_SESSION) {
            resp.status = cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
            resp.highlighted = 0;
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));

    uint32_t sid = 0;
    ASSERT_TRUE(!client.start_session(sid));
    ASSERT_EQ(sid, (uint32_t)0);
    client.disconnect();
}

// ============================================================
// Stress Tests
// ============================================================

TEST(Stress, rapid_requests) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        resp.status = cxxime::IPCStatus::OK;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_BASE_NAME, 2000));

    // Send 200 rapid requests
    for (int i = 0; i < 200; ++i) {
        cxxime::IPCResponse resp = {};
        ASSERT_TRUE(client.process_key(1, 'A', 0, resp));
        ASSERT_EQ(resp.status, cxxime::IPCStatus::OK);
    }

    client.disconnect();
}

TEST(Stress, concurrent_clients) {
    TestServer ts;
    std::atomic<int> total_handled{0};
    ASSERT_TRUE(ts.start([&](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        resp.status = cxxime::IPCStatus::OK;
        total_handled.fetch_add(1);
        return resp;
    }));

    std::atomic<int> total_sent{0};
    auto client_func = [&](int id) {
        cxxime::IpcClient client;
        if (!client.connect(cxxime::IPC_PIPE_BASE_NAME, 5000))
            return;
        for (int i = 0; i < 50; ++i) {
            cxxime::IPCResponse resp = {};
            if (client.process_key(static_cast<uint32_t>(id), 'A', 0, resp)) {
                total_sent.fetch_add(1);
            }
        }
        client.disconnect();
    };

    std::thread t1(client_func, 1);
    std::thread t2(client_func, 2);
    std::thread t3(client_func, 3);
    t1.join();
    t2.join();
    t3.join();

    ASSERT_GE(total_sent.load(), 50);  // at least 1 of 3 clients must fully succeed
    ASSERT_GE(total_handled.load(), total_sent.load());
}

RUN_ALL_TESTS()
