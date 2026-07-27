// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include <shlobj.h>

#include <cxxime/diagnostics_config.h>

#include "tsf_stage.h"

namespace {

// Async queue configuration
constexpr int kTsfQueueCapacity = 128;
constexpr int kTsfBatchSize = 16;
constexpr auto kTsfFlushInterval = std::chrono::milliseconds(200);

// Async trace queue (bounded, single writer thread)

struct TsfTraceEntry {
    char json[512];
    int len = 0;
};

TsfTraceEntry g_tsf_queue[kTsfQueueCapacity];
std::atomic<int> g_tsf_head{0};
std::atomic<int> g_tsf_tail{0};
std::atomic<int> g_tsf_dropped{0};

std::thread g_tsf_writer_thread;
std::mutex g_tsf_shutdown_mutex;
std::condition_variable g_tsf_shutdown_cv;
std::atomic<bool> g_tsf_shutdown{false};
std::atomic<bool> g_tsf_writer_started{false};

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

const char* bool_json(bool value) {
    return value ? "true" : "false";
}

bool tsf_should_log_event(bool important) {
    cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kOff)
        return false;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kError)
        return important;
    return true;
}

void foreground_class_utf8(char* out, int out_size) {
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';

    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return;

    wchar_t class_name[64] = {};
    if (!GetClassNameW(foreground, class_name, ARRAYSIZE(class_name)))
        return;

    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, out, out_size, nullptr, nullptr);
    out[out_size - 1] = '\0';
}

void current_process_utf8(char* out, int out_size) {
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';

    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path)))
        return;

    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    WideCharToMultiByte(CP_UTF8, 0, base, -1, out, out_size, nullptr, nullptr);
    out[out_size - 1] = '\0';
}

int tsf_queue_pop_batch(TsfTraceEntry* batch, int max) {
    int count = 0;
    while (count < max) {
        int tail = g_tsf_tail.load(std::memory_order_relaxed);
        if (tail == g_tsf_head.load(std::memory_order_acquire))
            break;
        batch[count++] = g_tsf_queue[tail];
        g_tsf_tail.store((tail + 1) % kTsfQueueCapacity, std::memory_order_release);
    }
    return count;
}

std::string tsf_get_log_dir() {
    wchar_t buf[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, buf)))
        return {};
    std::wstring dir = std::wstring(buf) + L"\\cxxime\\logs";
    CreateDirectoryW((std::wstring(buf) + L"\\cxxime").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

void tsf_rotate_log(FILE*& file, size_t& file_size, const std::string& path,
                           const cxxime::DiagnosticsConfig& config) {
    if (file) { fclose(file); file = nullptr; }
    DeleteFileA((path + "." + std::to_string(config.log_max_files)).c_str());
    for (int i = config.log_max_files - 1; i >= 1; --i) {
        MoveFileA((path + "." + std::to_string(i)).c_str(),
                  (path + "." + std::to_string(i + 1)).c_str());
    }
    MoveFileA(path.c_str(), (path + ".1").c_str());
    file_size = 0;
}

void tsf_writer_thread_func() {
    std::string dir = tsf_get_log_dir();
    if (dir.empty()) return;

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
    auto last_flush = std::chrono::steady_clock::now();

    while (!g_tsf_shutdown.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lock(g_tsf_shutdown_mutex);
            g_tsf_shutdown_cv.wait_for(lock, kTsfFlushInterval, [] {
                return g_tsf_shutdown.load(std::memory_order_relaxed);
            });
        }

        int count = tsf_queue_pop_batch(batch, kTsfBatchSize);
        if (count == 0) {
            auto now = std::chrono::steady_clock::now();
            if (file && (now - last_flush) >= kTsfFlushInterval) {
                fflush(file);
                last_flush = now;
            }
            continue;
        }

        if (!file) {
            file = fopen(path.c_str(), "a");
            if (!file) continue;
            fseek(file, 0, SEEK_END);
            file_size = ftell(file);
        }

        for (int i = 0; i < count; ++i) {
            cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
            if (file_size + batch[i].len + 1 > config.log_max_size) {
                tsf_rotate_log(file, file_size, path, config);
                file = fopen(path.c_str(), "a");
                if (!file) break;
            }
            fwrite(batch[i].json, 1, batch[i].len, file);
            fputc('\n', file);
            file_size += batch[i].len + 1;
        }

        if (file) {
            fflush(file);
            last_flush = std::chrono::steady_clock::now();
        }
    }

    // Final drain on shutdown
    if (file) {
        TsfTraceEntry entry;
        while (tsf_queue_pop_batch(&entry, 1) == 1) {
            cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
            if (file_size + entry.len + 1 > config.log_max_size) {
                tsf_rotate_log(file, file_size, path, config);
                file = fopen(path.c_str(), "a");
                if (!file) break;
            }
            fwrite(entry.json, 1, entry.len, file);
            fputc('\n', file);
            file_size += entry.len + 1;
        }
        fclose(file);
    }
}

void tsf_ensure_writer_started() {
    if (g_tsf_writer_started.exchange(true)) return;
    g_tsf_writer_thread = std::thread(tsf_writer_thread_func);
}

}  // namespace

const char* TextService::TsfTrace::result_string() const {
    switch (result) {
    case TsfResult::IPC_FAILED: return "ipc_failed";
    case TsfResult::COMMITTED:  return "committed";
    case TsfResult::PREEDIT:    return "preedit";
    case TsfResult::CLEARED:    return "cleared";
    case TsfResult::REJECTED:   return "rejected";
    default: return "unknown";
    }
}

int TextService::TsfTrace::to_json(char* buf, int size) const {
    return snprintf(buf, size,
        "{\"vk\":%u,\"mod\":%u,\"result\":\"%s\",\"cands\":%u,\"preedit_len\":%u,"
        "\"total_us\":%lld,\"ipc_us\":%lld,\"window_us\":%lld,\"slow\":%s}",
        vk, modifiers, result_string(),
        candidate_count, preedit_len,
        (long long)total_us, (long long)ipc_us, (long long)window_us,
        slow ? "true" : "false");
}

bool TextService::TsfTrace::should_log() const {
    cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kOff)
        return false;
    if (result == TsfResult::IPC_FAILED) return true;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kError)
        return false;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kVerbose)
        return true;
    if (slow) return true;
    return false;
}

void TextService::_enqueue_trace(const TsfTrace& trace) {
    if (!trace.should_log()) return;

    TsfTraceEntry entry;
    entry.len = trace.to_json(entry.json, sizeof(entry.json));
    if (entry.len <= 0) return;

    tsf_ensure_writer_started();
    tsf_queue_try_push(entry);  // Drop if full; never block hot path.
}

void TextService::_enqueue_event_trace(const char* event, const char* detail, bool important) {
    if (!tsf_should_log_event(important))
        return;

    char foreground_class[96] = {};
    foreground_class_utf8(foreground_class, sizeof(foreground_class));
    char process_name[MAX_PATH] = {};
    current_process_utf8(process_name, sizeof(process_name));

    TsfTraceEntry entry;
    entry.len = snprintf(entry.json, sizeof(entry.json),
                         "{\"event\":\"%s\",\"detail\":\"%s\",\"session\":%u,"
                         "\"focused\":%s,\"chinese\":%s,\"caps\":%s,"
                         "\"proc\":\"%s\",\"fg\":\"%s\"}",
                         event ? event : "", detail ? detail : "", _sessionId,
                         bool_json(_inputFocused), bool_json(_chinese_mode),
                         bool_json(_caps_lock), process_name, foreground_class);
    if (entry.len <= 0 || entry.len >= static_cast<int>(sizeof(entry.json)))
        return;

    tsf_ensure_writer_started();
    tsf_queue_try_push(entry);
}

// Called from DllMain(DLL_PROCESS_DETACH) via globals.cpp
void TextService::shutdown_trace() {
    if (!g_tsf_writer_started.exchange(false))
        return;
    g_tsf_shutdown.store(true, std::memory_order_relaxed);
    g_tsf_shutdown_cv.notify_all();
    if (g_tsf_writer_thread.joinable())
        g_tsf_writer_thread.join();
}

void TextService::_trace_input_decision(const char* block_reason) {
    if (!block_reason) {
        if (!_lastInputBlockReason.empty()) {
            _lastInputBlockReason.clear();
            _enqueue_event_trace("input_context", "allowed");
        }
        return;
    }

    if (_lastInputBlockReason == block_reason)
        return;
    _lastInputBlockReason = block_reason;
    _enqueue_event_trace("input_context", block_reason);
}

void TextService::_reset_stage_composition(const char* reason) {
    cxxime_tsf::trace_stage_composition_end(stage_input_id(), stage_composition_id(), reason);
    _stageTraceSession.reset_composition();
}

void TextService::trace_ui_element_method(const char* element, const char* method, bool important) {
    char detail[96] = {};
    snprintf(detail, sizeof(detail), "%s.%s",
             element ? element : "unknown", method ? method : "unknown");
    _enqueue_event_trace("ui_element_call", detail, important);
}

uint64_t TextService::ensure_stage_composition_id() {
    return _stageTraceSession.ensure_composition();
}

void TextService::trace_caret_event(const char* action,
                                    const char* source,
                                    bool resolved,
                                    const RECT* rect,
                                    HRESULT hr,
                                    bool important) {
    char detail[192] = {};
    if (rect) {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d rc=%ld,%ld,%ld,%ld hr=0x%08lx composing=%d visible=%d",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, rect->left, rect->top, rect->right, rect->bottom,
                 static_cast<unsigned long>(hr), _composing ? 1 : 0,
                 _candidateWindow.is_visible() ? 1 : 0);
    } else {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d hr=0x%08lx composing=%d visible=%d",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, static_cast<unsigned long>(hr),
                 _composing ? 1 : 0, _candidateWindow.is_visible() ? 1 : 0);
    }
    _enqueue_event_trace("caret_position", detail, important);
}
