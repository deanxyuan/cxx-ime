// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/query_trace.h>

#include <cstdio>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <cxxime/diagnostic_log_path.h>
#include <cxxime/diagnostics_config.h>
#include <cxxime/logging.h>
#include <cxxime/mpscq.h>

namespace cxxime {

// ─── Configuration ───────────────────────────────────────────

// Async queue configuration
static constexpr int kQueueCapacity = 256;                     // Bounded queue size
static constexpr int kBatchSize = 32;                          // Write batch size
static constexpr auto kFlushInterval = std::chrono::milliseconds(100); // Flush interval

// ─── Slow query thresholds ───────────────────────────────────

// ─── Queue entry (fixed size, no heap allocation) ────────────

struct TraceEntry {
    char json[1024];
    int len;
};

// ─── MPSC trace queue (lock-free multi-producer, single-consumer) ──────

struct TraceNode : MPSCQueue::Node {
    TraceEntry entry;
};

class TraceQueue {
public:
    TraceQueue() {
        // Link all pool nodes into the free list
        for (int i = kQueueCapacity - 1; i >= 0; --i) {
            pool_[i].next.Store(nullptr, std::memory_order_relaxed);
            push_free_(&pool_[i]);
        }
    }

    bool try_push(const TraceEntry& entry) {
        // Enforce bounded queue size — drop entries when over capacity
        if (size_.load(std::memory_order_relaxed) >= kQueueCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        auto* node = pop_free_();
        if (!node) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        node->entry = entry;
        queue_.push(node);
        size_.fetch_add(1, std::memory_order_relaxed);
        // Wake writer thread
        {
            std::lock_guard<std::mutex> lock(sig_mu_);
        }
        sig_cv_.notify_one();
        return true;
    }

    int pop_batch(TraceEntry* batch, int max_entries) {
        int count = 0;
        while (count < max_entries) {
            auto* node = static_cast<TraceNode*>(queue_.pop());
            if (!node) break;
            batch[count++] = node->entry;
            push_free_(node);
            size_.fetch_sub(1, std::memory_order_relaxed);
        }
        return count;
    }

    // Wait for entries or timeout (signaling only — queue data is lock-free)
    void wait(std::unique_lock<std::mutex>& lock) {
        sig_cv_.wait_for(lock, kFlushInterval);
    }

    void notify() {
        std::lock_guard<std::mutex> lock(sig_mu_);
        sig_cv_.notify_all();
    }

    size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

    // Mutex for wait/notify signaling only (does NOT protect queue data)
    std::mutex& mutex() { return sig_mu_; }

private:
    // Lock-free free list (atomic stack) for pre-allocated node pool
    TraceNode* pop_free_() {
        TraceNode* head = free_head_.Load(std::memory_order_acquire);
        while (head) {
            TraceNode* next = static_cast<TraceNode*>(head->next.Load(std::memory_order_relaxed));
            if (free_head_.CompareExchangeWeak(&head, next,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                return head;
            }
        }
        return nullptr;
    }

    void push_free_(TraceNode* node) {
        TraceNode* head = free_head_.Load(std::memory_order_acquire);
        do {
            node->next.Store(head, std::memory_order_relaxed);
        } while (!free_head_.CompareExchangeWeak(&head, node,
                    std::memory_order_acq_rel, std::memory_order_acquire));
    }

    TraceNode pool_[kQueueCapacity];
    Atomic<TraceNode*> free_head_{nullptr};
    MPSCQueue queue_;
    std::mutex sig_mu_;
    std::condition_variable sig_cv_;
    std::atomic<int> size_{0};
    std::atomic<uint64_t> dropped_{0};
};

// ─── Async writer ────────────────────────────────────────────

static TraceQueue g_queue;
static std::thread g_writer_thread;
static std::atomic<bool> g_shutdown{false};
static std::atomic<bool> g_accepting{true};
static std::mutex g_writer_mutex;

enum class WriterState {
    kStopped,
    kStarting,
    kRunning,
    kStopping,
};

static std::atomic<WriterState> g_writer_state{WriterState::kStopped};
static std::once_flag g_atexit_once;

static std::string get_log_dir() {
    const std::wstring directory = diagnostic_log_directory();
    if (directory.empty())
        return {};
    const int required =
        WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1)
        return {};
    std::string utf8(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1, &utf8[0], required, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}

static std::string get_trace_path() {
    return get_log_dir() + "\\server-trace.jsonl";
}

static void rotate_log_file(FILE*& file, size_t& file_size, const DiagnosticsConfig& config) {
    if (file) {
        fclose(file);
        file = nullptr;
    }

    std::string base_path = get_trace_path();

    // Delete oldest generation
    std::string oldest = base_path + "." + std::to_string(config.log_max_files);
    DeleteFileA(oldest.c_str());

    // Rotate existing files
    for (int i = config.log_max_files - 1; i >= 1; --i) {
        std::string from = base_path + "." + std::to_string(i);
        std::string to = base_path + "." + std::to_string(i + 1);
        MoveFileA(from.c_str(), to.c_str());
    }

    MoveFileA(base_path.c_str(), (base_path + ".1").c_str());
    file_size = 0;
}

// ─── Writer thread ───────────────────────────────────────────

static void writer_thread_func() {
    {
        std::lock_guard<std::mutex> lock(g_writer_mutex);
        if (g_writer_state.load(std::memory_order_relaxed) == WriterState::kStarting) {
            g_writer_state.store(WriterState::kRunning, std::memory_order_release);
        }
    }

    FILE* file = nullptr;
    size_t file_size = 0;

    // Open initial file
    std::string path = get_trace_path();
    file = fopen(path.c_str(), "a");
    if (file) {
        fseek(file, 0, SEEK_END);
        file_size = ftell(file);
    }

    TraceEntry batch[kBatchSize];
    auto last_flush = std::chrono::steady_clock::now();

    while (!g_shutdown.load(std::memory_order_relaxed)) {
        // Wait for entries or timeout (100ms flush interval)
        {
            std::unique_lock<std::mutex> lock(g_queue.mutex());
            g_queue.wait(lock);
        }

        // Drain queue in batches
        int count = g_queue.pop_batch(batch, kBatchSize);
        if (count == 0) {
            // Check if we need to flush
            auto now = std::chrono::steady_clock::now();
            if (file && (now - last_flush) >= kFlushInterval) {
                fflush(file);
                last_flush = now;
            }
            continue;
        }

        // Open file if needed
        if (!file) {
            file = fopen(path.c_str(), "a");
            if (!file) continue;
            fseek(file, 0, SEEK_END);
            file_size = ftell(file);
        }

        // Write batch
        for (int i = 0; i < count; ++i) {
            const auto& entry = batch[i];
            DiagnosticsConfig config = diagnostics_config();

            // Check rotation
            if (file_size + entry.len + 1 > config.log_max_size) {
                rotate_log_file(file, file_size, config);
                file = fopen(path.c_str(), "a");
                if (!file) break;
                file_size = 0;
            }

            fwrite(entry.json, 1, entry.len, file);
            fputc('\n', file);
            file_size += entry.len + 1;
        }

        // Flush batch
        if (file) {
            fflush(file);
            last_flush = std::chrono::steady_clock::now();
        }

    }

    // Final flush on shutdown
    // Drain remaining entries even when the log file could not be opened.
    int count = g_queue.pop_batch(batch, kBatchSize);
    while (count > 0) {
        if (file) {
            for (int i = 0; i < count; ++i) {
                const auto& entry = batch[i];
                DiagnosticsConfig config = diagnostics_config();
                if (file_size + entry.len + 1 > config.log_max_size) {
                    rotate_log_file(file, file_size, config);
                    file = fopen(path.c_str(), "a");
                    if (!file) break;
                    file_size = 0;
                }
                fwrite(entry.json, 1, entry.len, file);
                fputc('\n', file);
                file_size += entry.len + 1;
            }
        }
        count = g_queue.pop_batch(batch, kBatchSize);
    }
    if (file) {
        fclose(file);
    }

    g_writer_state.store(WriterState::kStopped, std::memory_order_release);
}

static bool ensure_writer_started_locked() {
    WriterState state = g_writer_state.load(std::memory_order_acquire);
    if (state == WriterState::kRunning || state == WriterState::kStarting)
        return true;
    if (state == WriterState::kStopping)
        return false;
    if (!g_accepting.load(std::memory_order_relaxed))
        return false;
    if (g_writer_thread.joinable())
        g_writer_thread.join();

    g_shutdown.store(false, std::memory_order_relaxed);
    g_writer_state.store(WriterState::kStarting, std::memory_order_release);
    try {
        g_writer_thread = std::thread(writer_thread_func);
    } catch (...) {
        g_writer_state.store(WriterState::kStopped, std::memory_order_release);
        return false;
    }
    // Ensure graceful shutdown even if caller forgets to call QueryTrace::shutdown().
    // atexit handlers run before global destructors, so the thread is joined
    // before its std::thread destructor runs (which would call std::terminate).
    std::call_once(g_atexit_once, [] { std::atexit(QueryTrace::shutdown); });
    return true;
}

// ─── Public API ──────────────────────────────────────────────

int QueryTrace::to_json(char* buf, int buf_size) const {
    if (!buf || buf_size < 2) return 0;

    // Escape raw_input for JSON
    char escaped_input[256] = {};
    int j = 0;
    for (int i = 0; raw_input[i] && j < 254; ++i) {
        char c = raw_input[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= 254) break;
            escaped_input[j++] = '\\';
            escaped_input[j++] = c;
        } else if (c >= 32) {
            escaped_input[j++] = c;
        }
    }
    escaped_input[j] = '\0';

    int written = snprintf(buf, buf_size,
        "{\"q\":%llu,\"sid\":%u,\"rev\":%llu,\"input\":\"%s\","
        "\"page\":%d,\"page_size\":%d,"
        "\"paths\":%d,\"live\":%d,\"candidates\":%d,"
        "\"composition_paths\":%u,\"composition_repeat_paths\":%u,"
        "\"span_queries\":%u,\"span_scans\":%u,\"composition_states\":%u,"
        "\"composed_candidates\":%u,\"composition_truncated\":%s,"
        "\"exact_scan\":%u,\"prefix_scan\":%u,\"user_scan\":%u,"
        "\"mixed_scan\":%u,\"mixed_bucket\":%u,\"mixed_hit\":%s,"
        "\"cache\":%s,\"deadline\":%s,\"cancelled\":%s,"
        "\"truncated\":%s,\"scan_trunc\":%s,\"topk_trunc\":%s,\"page_trunc\":%s,"
        "\"proc_us\":%lld,\"trans_us\":%lld,\"lookup_us\":%lld,"
        "\"composition_us\":%lld,\"merge_us\":%lld,\"total_us\":%lld}",
        (unsigned long long)query_id,
        (unsigned)session_id,
        (unsigned long long)revision,
        escaped_input,
        page_index, page_size,
        syllable_path_count, live_path_count, candidate_count,
        composition_path_count, composition_repeated_short_path_count,
        span_query_count, span_entry_scan_count, composition_state_count,
        composed_candidate_count, composition_truncated ? "true" : "false",
        exact_scan_count, prefix_scan_count, user_scan_count,
        mixed_scan_count, mixed_bucket_size, mixed_cache_hit ? "true" : "false",
        cache_hit ? "true" : "false",
        deadline_exceeded ? "true" : "false",
        cancelled ? "true" : "false",
        truncated ? "true" : "false",
        scan_budget_truncated ? "true" : "false",
        topk_truncated ? "true" : "false",
        page_truncated ? "true" : "false",
        (long long)processor_us,
        (long long)translate_us,
        (long long)lookup_us,
        (long long)composition_us,
        (long long)merge_us,
        (long long)total_us);

    if (written < 0 || written >= buf_size) return 0;
    return written;
}

static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

bool QueryTrace::should_sample(uint32_t session_id, uint64_t revision, uint64_t query_id, int rate) {
    if (rate <= 0)
        return false;
    uint64_t h = mix64((uint64_t(session_id) << 32) ^ revision ^ query_id);
    return (h % rate) == 0;
}

bool QueryTrace::should_log() const {
    DiagnosticsConfig config = diagnostics_config();
    if (config.trace_mode == DiagnosticTraceMode::kOff)
        return false;

    if (deadline_exceeded || cancelled)
        return true;

    if (config.trace_mode == DiagnosticTraceMode::kError)
        return false;

    if (total_us >= config.slow_query_us)
        return true;

    if (!cache_hit && total_us >= config.cache_miss_slow_us)
        return true;

    if (config.trace_mode == DiagnosticTraceMode::kVerbose)
        return true;

    if (truncated)
        return should_sample(session_id, revision, query_id, config.truncated_sample_rate);

    return should_sample(session_id, revision, query_id, config.normal_sample_rate);
}

void QueryTrace::log() const {
    if (!should_log())
        return;
    log_unchecked();
}

void QueryTrace::log_unchecked() const {
    char json_buf[1024];
    int len = to_json(json_buf, sizeof(json_buf));
    if (len <= 0) return;

#ifdef _DEBUG
    // Debug: OutputDebugStringW (non-blocking)
    wchar_t wbuf[1024];
    int wlen = MultiByteToWideChar(CP_UTF8, 0, json_buf, len, wbuf, 1023);
    if (wlen > 0) {
        if (wlen >= 1023)
            wlen = 1022;
        wbuf[wlen] = L'\n';
        wbuf[wlen + 1] = L'\0';
        OutputDebugStringW(wbuf);
    }
#endif

    TraceEntry entry;
    std::memcpy(entry.json, json_buf, len);
    entry.json[len] = '\0';
    entry.len = len;

    // Admission and enqueue share the lifecycle lock so shutdown cannot finish its final drain
    // while a producer that already observed the enabled state is still pending.
    std::lock_guard<std::mutex> lock(g_writer_mutex);
    if (!g_accepting.load(std::memory_order_relaxed) || !ensure_writer_started_locked())
        return;
    g_queue.try_push(entry);  // Drop if queue full - never block input processing.
}

// ─── Shutdown (call at process exit) ─────────────────────────

void QueryTrace::shutdown() {
    set_enabled(false);
    std::thread writer;
    {
        std::lock_guard<std::mutex> lock(g_writer_mutex);
        if (g_writer_thread.joinable()) {
            writer = std::move(g_writer_thread);
        }
    }
    if (writer.joinable()) {
        writer.join();
    }
}

void QueryTrace::set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_writer_mutex);
    g_accepting.store(enabled, std::memory_order_release);
    if (enabled)
        return;

    const WriterState state = g_writer_state.load(std::memory_order_relaxed);
    if (state == WriterState::kRunning || state == WriterState::kStarting) {
        g_writer_state.store(WriterState::kStopping, std::memory_order_release);
        g_shutdown.store(true, std::memory_order_relaxed);
        g_queue.notify();
    }
}

uint64_t QueryTrace::dropped_count() {
    return g_queue.dropped();
}

} // namespace cxxime
