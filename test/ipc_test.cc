// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <windows.h>
#include <cxxime/ipc_server.h>
#include <cxxime/ipc_client.h>
#include <cxxime/ipc_protocol.h>

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

static cxxime::IPCResponse make_response(uint32_t status) {
    cxxime::IPCResponse resp = {};
    resp.status = status;
    return resp;
}

// ============================================================
// Protocol Tests
// ============================================================

TEST(Protocol, pipe_name) {
    ASSERT_TRUE(wcscmp(cxxime::IPC_PIPE_NAME, L"\\\\.\\pipe\\CxxIME") == 0);
}

TEST(Protocol, request_struct_size) {
    ASSERT_GE(sizeof(cxxime::IPCRequest), 20u);
}

TEST(Protocol, response_struct_size) {
    ASSERT_GE(sizeof(cxxime::IPCResponse), 4u + 256 + 256 + 4 + 10 * 64 + 4);
}

TEST(Protocol, response_zero_init) {
    cxxime::IPCResponse resp = {};
    ASSERT_EQ(resp.status, (uint32_t)0);
    ASSERT_EQ(resp.commit_text[0], '\0');
    ASSERT_EQ(resp.preedit[0], '\0');
    ASSERT_EQ(resp.candidate_count, (uint32_t)0);
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
    ASSERT_TRUE(!client.connect(cxxime::IPC_PIPE_NAME, 300));
    ASSERT_TRUE(!client.is_connected());
}

TEST(Client, connect_with_server) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse { return {}; }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    ASSERT_TRUE(client.is_connected());
    client.disconnect();
    ASSERT_TRUE(!client.is_connected());
}

TEST(Client, disconnect_idempotent) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse { return {}; }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
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
        if (req.command == cxxime::IPCCommand::START_SESSION) resp.status = 42;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    uint32_t sid = 0;
    ASSERT_TRUE(client.start_session(sid));
    ASSERT_EQ(sid, (uint32_t)42);
}

TEST(IPC, end_session) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::END_SESSION) resp.status = 0;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    ASSERT_TRUE(client.end_session(1));
}

TEST(IPC, process_key_preedit) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::PROCESS_KEY) {
            resp.status = 0;
            strncpy_s(resp.preedit, "ni", sizeof(resp.preedit) - 1);
            resp.candidate_count = 2;
            strncpy_s(resp.candidates[0], "\xe4\xbd\xa0", sizeof(resp.candidates[0]) - 1);
            strncpy_s(resp.candidates[1], "\xe5\xb0\xbc", sizeof(resp.candidates[1]) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.process_key(1, 'N', 0, resp));
    ASSERT_EQ(resp.status, (uint32_t)0);
    ASSERT_EQ(strcmp(resp.preedit, "ni"), 0);
    ASSERT_EQ(resp.candidate_count, (uint32_t)2);
}

TEST(IPC, process_key_commit) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::PROCESS_KEY) {
            resp.status = 0;
            strncpy_s(resp.commit_text, "\xe4\xbd\xa0", sizeof(resp.commit_text) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.process_key(1, '1', 0, resp));
    ASSERT_EQ(resp.status, (uint32_t)0);
    ASSERT_EQ(strcmp(resp.commit_text, "\xe4\xbd\xa0"), 0);
}

TEST(IPC, process_key_rejected) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::PROCESS_KEY) resp.status = 1;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.process_key(1, VK_RETURN, 0, resp));
    ASSERT_EQ(resp.status, (uint32_t)1);
}

TEST(IPC, select_candidate) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::SELECT_CANDIDATE) {
            resp.status = 0;
            strncpy_s(resp.commit_text, "\xe4\xbd\xa0\xe5\xa5\xbd", sizeof(resp.commit_text) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.select_candidate(1, 0, resp));
    ASSERT_EQ(resp.status, (uint32_t)0);
}

TEST(IPC, commit_composition) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::COMMIT_COMPOSITION) {
            resp.status = 0;
            strncpy_s(resp.commit_text, "\xe6\xb5\x8b\xe8\xaf\x95", sizeof(resp.commit_text) - 1);
        }
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.commit_composition(1, resp));
    ASSERT_EQ(resp.status, (uint32_t)0);
}

TEST(IPC, focus_in_out) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
        return make_response(0);
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
    ASSERT_TRUE(client.focus_in(1));
    ASSERT_TRUE(client.focus_out(1));
}

TEST(IPC, send_request) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        if (req.command == cxxime::IPCCommand::CLEAR_COMPOSITION) resp.status = 99;
        return resp;
    }));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));

    cxxime::IPCRequest req = {};
    req.command = cxxime::IPCCommand::CLEAR_COMPOSITION;
    req.session_id = 1;
    cxxime::IPCResponse resp = {};
    ASSERT_TRUE(client.send_request(req, resp));
    ASSERT_EQ(resp.status, (uint32_t)99);
}

// ============================================================
// Multi-Client Tests
// ============================================================

TEST(MultiClient, two_clients_simultaneous) {
    TestServer ts;
    ASSERT_TRUE(ts.start([](const cxxime::IPCRequest& req) -> cxxime::IPCResponse {
        cxxime::IPCResponse resp = {};
        resp.status = req.session_id * 10;
        return resp;
    }));

    cxxime::IpcClient client1;
    cxxime::IpcClient client2;
    ASSERT_TRUE(client1.connect(cxxime::IPC_PIPE_NAME, 2000));
    ASSERT_TRUE(client2.connect(cxxime::IPC_PIPE_NAME, 2000));

    cxxime::IPCResponse resp1 = {};
    cxxime::IPCResponse resp2 = {};
    ASSERT_TRUE(client1.process_key(1, 'A', 0, resp1));
    ASSERT_TRUE(client2.process_key(2, 'B', 0, resp2));
    ASSERT_EQ(resp1.status, (uint32_t)10);
    ASSERT_EQ(resp2.status, (uint32_t)20);

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
            resp.status = (uint32_t)call_count;
        } else {
            resp.status = 0;
        }
        return resp;
    });
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cxxime::IpcClient client;
    ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));

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
            return make_response(1);
        }));
        cxxime::IpcClient client;
        ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
        cxxime::IPCResponse resp = {};
        ASSERT_TRUE(client.process_key(1, 'A', 0, resp));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        TestServer ts;
        ASSERT_TRUE(ts.start([](const cxxime::IPCRequest&) -> cxxime::IPCResponse {
            return make_response(1);
        }));
        cxxime::IpcClient client;
        ASSERT_TRUE(client.connect(cxxime::IPC_PIPE_NAME, 2000));
        cxxime::IPCResponse resp = {};
        ASSERT_TRUE(client.process_key(1, 'A', 0, resp));
    }
}

RUN_ALL_TESTS()
