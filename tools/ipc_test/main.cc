// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// IPC test tool — interactive command-line utility for testing the IPC layer.
// Usage: ipc_test [--pipe <name>]

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <windows.h>
#include <cxxime/ipc_client.h>
#include <cxxime/ipc_protocol.h>

namespace {

void set_console_utf8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void print_usage() {
    printf("Commands:\n");
    printf("  connect [pipe_name]     — connect to IPC server\n");
    printf("  disconnect              — disconnect from server\n");
    printf("  session start           — start a new session\n");
    printf("  session end <id>        — end a session\n");
    printf("  key <vk> [mods] [up]    — send key event (vk in hex/dec)\n");
    printf("  select <index>          — select candidate\n");
    printf("  commit                  — commit composition\n");
    printf("  focus in|out            — send focus event\n");
    printf("  ping                    — measure RTT\n");
    printf("  stress <n> [conc]       — send n requests with c concurrent clients\n");
    printf("  bench <n>               — benchmark n round-trips\n");
    printf("  status                  — show connection status\n");
    printf("  help                    — show this help\n");
    printf("  quit                    — exit\n");
    printf("\n");
    printf("Key codes: A=41, B=42, ..., Z=5A, Space=20, Enter=0D, Backspace=08, Esc=1B\n");
    printf("Modifiers: Shift=1, Ctrl=2, Alt=4 (additive)\n");
}

class IpcTool {
public:
    void run() {
        char line[1024];
        printf("IPC Test Tool\n");
        printf("Type 'help' for commands, 'quit' to exit.\n\n");

        while (true) {
            printf("ipc> ");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin))
                break;

            process_line(line);
        }
    }

private:
    cxxime::IpcClient client_;
    uint32_t session_id_ = 0;
    bool connected_ = false;
    bool has_session_ = false;

    void process_line(const char* line) {
        char cmd[64] = {};
        sscanf(line, "%63s", cmd);

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            exit(0);
        } else if (strcmp(cmd, "help") == 0) {
            print_usage();
        } else if (strcmp(cmd, "connect") == 0) {
            cmd_connect(line);
        } else if (strcmp(cmd, "disconnect") == 0) {
            cmd_disconnect();
        } else if (strcmp(cmd, "session") == 0) {
            cmd_session(line);
        } else if (strcmp(cmd, "key") == 0) {
            cmd_key(line);
        } else if (strcmp(cmd, "select") == 0) {
            cmd_select(line);
        } else if (strcmp(cmd, "commit") == 0) {
            cmd_commit();
        } else if (strcmp(cmd, "focus") == 0) {
            cmd_focus(line);
        } else if (strcmp(cmd, "stress") == 0) {
            cmd_stress(line);
        } else if (strcmp(cmd, "bench") == 0) {
            cmd_bench(line);
        } else if (strcmp(cmd, "status") == 0) {
            cmd_status();
        } else if (cmd[0] != '\0') {
            printf("Unknown command: %s\n", cmd);
        }
    }

    void ensure_connected() {
        if (!connected_) {
            printf("Not connected. Use 'connect' first.\n");
            return;
        }
    }

    void cmd_connect(const char* line) {
        char pipe_buf[256] = {};
        sscanf(line, "%*s %255s", pipe_buf);

        if (client_.is_connected()) {
            client_.disconnect();
        }

        std::wstring pipe_name = cxxime::IPC_PIPE_BASE_NAME;
        if (pipe_buf[0] != '\0') {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, pipe_buf, -1, nullptr, 0);
            pipe_name.resize(wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, pipe_buf, -1, &pipe_name[0], wlen);
        }

        printf("Connecting to %ls...\n", pipe_name.c_str());
        if (client_.connect(pipe_name, 3000)) {
            connected_ = true;
            printf("Connected.\n");
        } else {
            printf("Connection failed (server not running?).\n");
        }
    }

    void cmd_disconnect() {
        if (has_session_) {
            client_.end_session(session_id_);
            has_session_ = false;
            session_id_ = 0;
        }
        client_.disconnect();
        connected_ = false;
        printf("Disconnected.\n");
    }

    void cmd_session(const char* line) {
        char sub[16] = {};
        sscanf(line, "%*s %15s", sub);

        if (strcmp(sub, "start") == 0) {
            if (!connected_) { printf("Not connected.\n"); return; }
            if (has_session_) { printf("Already have session %u.\n", session_id_); return; }

            uint32_t sid = 0;
            if (client_.start_session(sid)) {
                session_id_ = sid;
                has_session_ = true;
                printf("Session started: id=%u\n", sid);
            } else {
                printf("Session start failed.\n");
            }
        } else if (strcmp(sub, "end") == 0) {
            uint32_t sid = session_id_;
            sscanf(line, "%*s %*s %u", &sid);
            if (!has_session_ && sid == 0) {
                printf("No active session.\n");
                return;
            }
            if (client_.end_session(sid)) {
                if (sid == session_id_) { has_session_ = false; session_id_ = 0; }
                printf("Session %u ended.\n", sid);
            } else {
                printf("End session failed.\n");
            }
        } else {
            printf("Usage: session start|end [id]\n");
        }
    }

    void cmd_key(const char* line) {
        if (!connected_ || !has_session_) {
            printf("Not connected or no session. Use 'connect' and 'session start' first.\n");
            return;
        }

        unsigned int vk = 0, mods = 0, up = 0;
        int n = sscanf(line, "%*s %x %x %u", &vk, &mods, &up);
        if (n < 1) { printf("Usage: key <vk_hex> [modifiers] [key_up]\n"); return; }

        cxxime::IPCResponse resp = {};
        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = client_.process_key(session_id_, vk, mods, resp, up != 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        if (!ok) {
            printf("Request failed.\n");
            return;
        }

        printf("status=%u rtt=%lldus ascii=%d composing=%d\n",
               (uint32_t)resp.status, us,
               resp.ascii_mode, resp.composing);

        if (resp.commit_text[0])
            printf("  commit: %s\n", resp.commit_text);
        if (resp.preedit[0])
            printf("  preedit: %s\n", resp.preedit);
        if (resp.candidate_count > 0) {
            printf("  candidates (%u):\n", resp.candidate_count);
            for (uint32_t i = 0; i < resp.candidate_count; ++i) {
                printf("    %u. %s%s\n", i + 1, resp.candidates[i],
                       i == resp.highlighted ? " <===" : "");
            }
        }
    }

    void cmd_select(const char* line) {
        if (!connected_ || !has_session_) {
            printf("Not connected or no session.\n"); return;
        }

        int index = 0;
        if (sscanf(line, "%*s %d", &index) < 1) {
            printf("Usage: select <index>\n"); return;
        }

        cxxime::IPCResponse resp = {};
        if (client_.select_candidate(session_id_, index, resp)) {
            printf("Selected. commit=%s\n", resp.commit_text);
        } else {
            printf("Select failed.\n");
        }
    }

    void cmd_commit() {
        if (!connected_ || !has_session_) {
            printf("Not connected or no session.\n"); return;
        }

        cxxime::IPCResponse resp = {};
        if (client_.commit_composition(session_id_, resp)) {
            printf("Committed. text=%s\n", resp.commit_text);
        } else {
            printf("Commit failed.\n");
        }
    }

    void cmd_focus(const char* line) {
        if (!connected_) { printf("Not connected.\n"); return; }
        if (!has_session_) { printf("No session. Start one first.\n"); return; }

        char sub[16] = {};
        sscanf(line, "%*s %15s", sub);
        if (strcmp(sub, "in") == 0) {
            client_.focus_in(session_id_);
            printf("Focus in sent.\n");
        } else if (strcmp(sub, "out") == 0) {
            client_.focus_out(session_id_);
            printf("Focus out sent.\n");
        } else {
            printf("Usage: focus in|out\n");
        }
    }

    void cmd_stress(const char* line) {
        int count = 100, concurrency = 1;
        sscanf(line, "%*s %d %d", &count, &concurrency);
        if (concurrency < 1) concurrency = 1;

        printf("Stress test: %d requests, %d concurrent clients...\n", count, concurrency);

        std::atomic<long long> total_us{0};
        std::atomic<int> successes{0};
        std::atomic<int> failures{0};

        auto worker = [&]() {
            cxxime::IpcClient cli;
            if (!cli.connect(cxxime::IPC_PIPE_BASE_NAME, 2000)) {
                failures.fetch_add(count / concurrency);
                return;
            }
            uint32_t sid = 0;
            if (!cli.start_session(sid)) {
                failures.fetch_add(count / concurrency);
                return;
            }
            for (int i = 0; i < count / concurrency; ++i) {
                cxxime::IPCResponse resp = {};
                auto t0 = std::chrono::high_resolution_clock::now();
                if (cli.process_key(sid, 'A', 0, resp)) {
                    auto t1 = std::chrono::high_resolution_clock::now();
                    total_us.fetch_add(
                        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
                    successes.fetch_add(1);
                } else {
                    failures.fetch_add(1);
                }
            }
            cli.end_session(sid);
            cli.disconnect();
        };

        std::vector<std::thread> threads;
        auto t_start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < concurrency; ++i)
            threads.emplace_back(worker);
        for (auto& t : threads)
            t.join();

        auto t_total = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t_start).count();

        printf("Results: %d ok, %d failed, time=%lldms\n",
               successes.load(), failures.load(), t_total);
        if (successes.load() > 0) {
            printf("  Avg latency: %.0f us\n",
                   (double)total_us.load() / successes.load());
            printf("  Throughput: %.0f req/s\n",
                   successes.load() * 1000.0 / t_total);
        }
    }

    void cmd_bench(const char* line) {
        int count = 1000;
        sscanf(line, "%*s %d", &count);

        // Step 1: connect (this may take up to 2s if server is down)
        printf("Connecting...\n");
        fflush(stdout);
        cxxime::IpcClient cli;
        if (!cli.connect(cxxime::IPC_PIPE_BASE_NAME, 2000)) {
            printf("Failed to connect (server not running?).\n");
            return;
        }
        printf("  Connected.\n");

        // Step 2: start session (engine init — may take a moment)
        printf("Starting session...\n");
        fflush(stdout);
        uint32_t sid = 0;
        if (!cli.start_session(sid)) {
            printf("Failed to start session (engine init failed?).\n");
            cli.disconnect();
            return;
        }
        printf("  Session %u started.\n", sid);

        // Step 3: benchmark
        printf("Benchmark: %d round-trips...\n", count);
        fflush(stdout);

        long long total_us = 0;
        long long min_us = LLONG_MAX;
        long long max_us = 0;
        int ok = 0;

        int progress_step = (count > 50) ? count / 10 : 1;
        for (int i = 0; i < count; ++i) {
            cxxime::IPCResponse resp = {};
            auto t0 = std::chrono::high_resolution_clock::now();
            if (cli.process_key(sid, 'A', 0, resp)) {
                auto t1 = std::chrono::high_resolution_clock::now();
                long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                total_us += us;
                if (us < min_us) min_us = us;
                if (us > max_us) max_us = us;
                ++ok;
            } else if (resp.status != cxxime::IPCStatus::OK) {
                printf("  [%d] server error status=%u\n", i, (uint32_t)resp.status);
                break;
            } else {
                printf("  [%d] connection lost\n", i);
                break;
            }
            if ((i + 1) % progress_step == 0) {
                printf("  %d/%d (last rtt: %lld us)\n", i + 1, count,
                       std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::high_resolution_clock::now() - t0).count());
                fflush(stdout);
            }
        }

        cli.end_session(sid);
        cli.disconnect();

        if (ok > 0) {
            printf("Results: %d/%d ok\n", ok, count);
            printf("  Avg:   %.1f us\n", (double)total_us / ok);
            printf("  Min:   %lld us\n", min_us);
            printf("  Max:   %lld us\n", max_us);
            printf("  Throughput: %.0f req/s\n", ok * 1000000.0 / total_us);
        } else {
            printf("All requests failed (server not running or engine error).\n");
        }
    }

    void cmd_status() {
        printf("Connected: %s\n", connected_ ? "yes" : "no");
        printf("Session:   %s (id=%u)\n", has_session_ ? "active" : "none", session_id_);
        printf("Pipe:      %ls\n", cxxime::IPC_PIPE_BASE_NAME);
    }
};

} // namespace

int main() {
    set_console_utf8();

    IpcTool tool;
    tool.run();

    return 0;
}
