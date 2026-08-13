// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_log_writer.h"

#include <cstdio>
#include <cstring>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>

#include <cxxime/diagnostic_log_maintenance.h>
#include <cxxime/diagnostic_log_path.h>
#include <cxxime/diagnostics_config.h>

namespace {

constexpr int kTsfQueueCapacity = 128;
constexpr int kTsfBatchSize = 16;
constexpr int kTsfLogLineCapacity = 512;

struct TsfTraceEntry {
    char json[kTsfLogLineCapacity];
    int len = 0;
};

TsfTraceEntry g_tsf_queue[kTsfQueueCapacity];
std::atomic<int> g_tsf_head{0};
std::atomic<int> g_tsf_tail{0};
std::atomic<int> g_tsf_dropped{0};

std::thread* g_tsf_writer_thread = nullptr;
std::mutex g_tsf_shutdown_mutex;
std::condition_variable g_tsf_shutdown_cv;
std::atomic<bool> g_tsf_shutdown{false};
std::atomic<bool> g_tsf_accepting{true};
std::atomic<bool> g_tsf_writer_has_thread{false};

enum class TsfWriterState {
    kStopped,
    kStarting,
    kRunning,
    kStopping,
};

std::atomic<TsfWriterState> g_tsf_writer_state{TsfWriterState::kStopped};

bool tsf_queue_try_push(const TsfTraceEntry& entry) {
    int head = g_tsf_head.load(std::memory_order_relaxed);
    int next = (head + 1) % kTsfQueueCapacity;
    if (next == g_tsf_tail.load(std::memory_order_acquire)) {
        g_tsf_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_tsf_queue[head] = entry;
    g_tsf_head.store(next, std::memory_order_release);
    return true;
}

int tsf_queue_pop_batch(TsfTraceEntry* batch, int max) {
    int count = 0;
    while (count < max) {
        int tail = g_tsf_tail.load(std::memory_order_relaxed);
        if (tail == g_tsf_head.load(std::memory_order_acquire)) {
            break;
        }
        batch[count++] = g_tsf_queue[tail];
        g_tsf_tail.store((tail + 1) % kTsfQueueCapacity, std::memory_order_release);
    }
    return count;
}

bool tsf_queue_empty() {
    return g_tsf_tail.load(std::memory_order_relaxed) == g_tsf_head.load(std::memory_order_acquire);
}

std::string tsf_get_log_dir() {
    const std::wstring directory = cxxime::diagnostic_log_directory();
    if (directory.empty()) {
        return {};
    }

    const int required =
        WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string utf8(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1, &utf8[0], required, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}

void tsf_rotate_log(FILE*& file, size_t& file_size, const std::string& path,
                    const cxxime::DiagnosticsConfig& config) {
    if (file) {
        fclose(file);
        file = nullptr;
    }
    DeleteFileA((path + "." + std::to_string(config.log_max_files)).c_str());
    for (int i = config.log_max_files - 1; i >= 1; --i) {
        MoveFileA((path + "." + std::to_string(i)).c_str(),
                  (path + "." + std::to_string(i + 1)).c_str());
    }
    MoveFileA(path.c_str(), (path + ".1").c_str());
    file_size = 0;
}

void tsf_writer_thread_func(std::string dir) {
    if (g_tsf_writer_state.load(std::memory_order_relaxed) == TsfWriterState::kStarting) {
        g_tsf_writer_state.store(TsfWriterState::kRunning, std::memory_order_release);
    }

    if (dir.empty()) {
        g_tsf_writer_state.store(TsfWriterState::kStopped, std::memory_order_release);
        return;
    }

    const std::wstring log_directory = cxxime::diagnostic_log_directory();
    const bool maintains_directory = cxxime::diagnostic_log_directory_is_packaged();
    if (maintains_directory) {
        cxxime::cleanup_diagnostic_log_directory(
            log_directory, cxxime::diagnostic_log_retention_options(cxxime::diagnostics_config()));
    }

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "tsf-%d-trace.jsonl", (int)GetCurrentProcessId());
    std::string path = dir + "\\" + pid_str;

    FILE* file = nullptr;
    size_t file_size = 0;

    file = fopen(path.c_str(), "a");
    if (file) {
        fseek(file, 0, SEEK_END);
        file_size = ftell(file);
    }

    TsfTraceEntry batch[kTsfBatchSize];
    auto next_maintenance = std::chrono::steady_clock::now() + std::chrono::hours(6);

    while (!g_tsf_shutdown.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lock(g_tsf_shutdown_mutex);
            const auto wake_condition = [] {
                return g_tsf_shutdown.load(std::memory_order_relaxed) || !tsf_queue_empty();
            };
            if (maintains_directory) {
                g_tsf_shutdown_cv.wait_until(lock, next_maintenance, wake_condition);
            } else {
                g_tsf_shutdown_cv.wait(lock, wake_condition);
            }
        }

        int count = tsf_queue_pop_batch(batch, kTsfBatchSize);
        if (count == 0) {
            auto now = std::chrono::steady_clock::now();
            if (maintains_directory && now >= next_maintenance) {
                cxxime::cleanup_diagnostic_log_directory(
                    log_directory,
                    cxxime::diagnostic_log_retention_options(cxxime::diagnostics_config()));
                next_maintenance = now + std::chrono::hours(6);
            }
            continue;
        }

        if (!file) {
            file = fopen(path.c_str(), "a");
            if (!file) {
                continue;
            }
            fseek(file, 0, SEEK_END);
            file_size = ftell(file);
        }

        for (int i = 0; i < count; ++i) {
            cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
            if (file_size + batch[i].len + 1 > config.log_max_size) {
                tsf_rotate_log(file, file_size, path, config);
                file = fopen(path.c_str(), "a");
                if (!file) {
                    break;
                }
            }
            fwrite(batch[i].json, 1, batch[i].len, file);
            fputc('\n', file);
            file_size += batch[i].len + 1;
        }

        if (file) {
            fflush(file);
        }

        auto now = std::chrono::steady_clock::now();
        if (maintains_directory && now >= next_maintenance) {
            cxxime::cleanup_diagnostic_log_directory(
                log_directory,
                cxxime::diagnostic_log_retention_options(cxxime::diagnostics_config()));
            next_maintenance = now + std::chrono::hours(6);
        }
    }

    TsfTraceEntry entry;
    while (tsf_queue_pop_batch(&entry, 1) == 1) {
        if (file) {
            cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
            if (file_size + entry.len + 1 > config.log_max_size) {
                tsf_rotate_log(file, file_size, path, config);
                file = fopen(path.c_str(), "a");
                if (!file) {
                    continue;
                }
            }
            fwrite(entry.json, 1, entry.len, file);
            fputc('\n', file);
            file_size += entry.len + 1;
        }
    }
    if (file) {
        fclose(file);
    }

    g_tsf_writer_state.store(TsfWriterState::kStopped, std::memory_order_release);
}

bool tsf_ensure_writer_started_locked() {
    TsfWriterState state = g_tsf_writer_state.load(std::memory_order_acquire);
    if (state == TsfWriterState::kRunning || state == TsfWriterState::kStarting) {
        return true;
    }
    if (state == TsfWriterState::kStopping) {
        return false;
    }

    if (!g_tsf_accepting.load(std::memory_order_relaxed)) {
        return false;
    }
    if (g_tsf_writer_thread) {
        if (g_tsf_writer_thread->joinable()) {
            g_tsf_writer_thread->join();
        }
        delete g_tsf_writer_thread;
        g_tsf_writer_thread = nullptr;
        g_tsf_writer_has_thread.store(false, std::memory_order_release);
    }
    std::string log_dir = tsf_get_log_dir();
    if (log_dir.empty()) {
        return false;
    }
    g_tsf_shutdown.store(false, std::memory_order_relaxed);
    g_tsf_writer_state.store(TsfWriterState::kStarting, std::memory_order_release);
    g_tsf_writer_has_thread.store(true, std::memory_order_release);
    try {
        g_tsf_writer_thread = new std::thread(tsf_writer_thread_func, log_dir);
    } catch (...) {
        delete g_tsf_writer_thread;
        g_tsf_writer_thread = nullptr;
        g_tsf_writer_has_thread.store(false, std::memory_order_release);
        g_tsf_writer_state.store(TsfWriterState::kStopped, std::memory_order_release);
        return false;
    }
    return true;
}

} // namespace

void cxxime_tsf::enqueue_tsf_log_line(const char* json, int length) {
    if (!json || length <= 0 || length >= kTsfLogLineCapacity) {
        return;
    }

    TsfTraceEntry entry;
    memcpy(entry.json, json, static_cast<size_t>(length));
    entry.len = length;

    std::lock_guard<std::mutex> lock(g_tsf_shutdown_mutex);
    if (!g_tsf_accepting.load(std::memory_order_relaxed) || !tsf_ensure_writer_started_locked()) {
        return;
    }
    if (tsf_queue_try_push(entry)) {
        g_tsf_shutdown_cv.notify_one();
    }
}

void cxxime_tsf::set_tsf_log_writer_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_tsf_shutdown_mutex);
    g_tsf_accepting.store(enabled, std::memory_order_release);
    if (enabled) {
        return;
    }

    const TsfWriterState state = g_tsf_writer_state.load(std::memory_order_relaxed);
    if (state == TsfWriterState::kRunning || state == TsfWriterState::kStarting) {
        g_tsf_writer_state.store(TsfWriterState::kStopping, std::memory_order_release);
        g_tsf_shutdown.store(true, std::memory_order_relaxed);
        g_tsf_shutdown_cv.notify_all();
    }
}

void cxxime_tsf::request_tsf_log_writer_stop() {
    g_tsf_accepting.store(false, std::memory_order_release);
    g_tsf_shutdown.store(true, std::memory_order_relaxed);
    const TsfWriterState state = g_tsf_writer_state.load(std::memory_order_relaxed);
    if (state == TsfWriterState::kRunning || state == TsfWriterState::kStarting) {
        g_tsf_writer_state.store(TsfWriterState::kStopping, std::memory_order_release);
    }
    g_tsf_shutdown_cv.notify_all();
}

void cxxime_tsf::shutdown_tsf_log_writer() {
    set_tsf_log_writer_enabled(false);
    std::thread* writer = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_tsf_shutdown_mutex);
        writer = g_tsf_writer_thread;
        g_tsf_writer_thread = nullptr;
    }
    if (writer) {
        if (writer->joinable()) {
            writer->join();
        }
        delete writer;
    }
    g_tsf_writer_has_thread.store(false, std::memory_order_release);
}

bool cxxime_tsf::tsf_log_writer_has_thread() {
    return g_tsf_writer_has_thread.load(std::memory_order_acquire);
}
