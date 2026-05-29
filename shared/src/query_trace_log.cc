// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/query_trace.h>
#include <cxxime/logging.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <atomic>

namespace cxxime {

// ─── Configuration ───────────────────────────────────────────

static constexpr size_t kMaxLogSize = 64 * 1024 * 1024;       // 64 MiB per file
static constexpr int kMaxLogGenerations = 4;                   // Keep 4 generations
static constexpr size_t kMaxTotalLogSize = 512 * 1024 * 1024;  // 512 MiB total
static constexpr size_t kTargetTotalLogSize = 384 * 1024 * 1024; // Clean to 384 MiB

// Async queue configuration
static constexpr int kQueueCapacity = 256;                     // Bounded queue size
static constexpr int kBatchSize = 32;                          // Write batch size
static constexpr auto kFlushInterval = std::chrono::milliseconds(100); // Flush interval

// ─── Slow query thresholds ───────────────────────────────────

static constexpr int64_t kSlowQueryUs = 30000;     // 30ms
static constexpr int64_t kSlowIpcUs = 2000;        // 2ms
static constexpr int64_t kSlowWindowUs = 5000;     // 5ms

// ─── Queue entry (fixed size, no heap allocation) ────────────

struct TraceEntry {
    char json[1024];
    int len;
};

// ─── Bounded SPSC queue (lock-free for single producer/single consumer) ──

class BoundedQueue {
public:
    BoundedQueue() : head_(0), tail_(0), dropped_(0) {}

    // Producer: try to push (non-blocking, returns false if full)
    bool try_push(const TraceEntry& entry) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) % kQueueCapacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;  // Queue full, drop entry
        }
        entries_[head] = entry;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: try to pop (non-blocking, returns false if empty)
    bool try_pop(TraceEntry& entry) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }
        entry = entries_[tail];
        tail_.store((tail + 1) % kQueueCapacity, std::memory_order_release);
        return true;
    }

    // Consumer: pop up to max_entries, returns count popped
    int pop_batch(TraceEntry* batch, int max_entries) {
        int count = 0;
        while (count < max_entries) {
            if (!try_pop(batch[count]))
                break;
            ++count;
        }
        return count;
    }

    size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    void reset_dropped() { dropped_.store(0, std::memory_order_relaxed); }

private:
    TraceEntry entries_[kQueueCapacity];
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
    std::atomic<size_t> dropped_;
};

// ─── Async writer ────────────────────────────────────────────

static BoundedQueue g_queue;
static std::thread g_writer_thread;
static std::mutex g_shutdown_mutex;
static std::condition_variable g_shutdown_cv;
static std::atomic<bool> g_shutdown{false};
static std::atomic<bool> g_writer_started{false};

static std::string get_log_dir() {
    char* local_app_data = nullptr;
    size_t len = 0;
    if (_dupenv_s(&local_app_data, &len, "LOCALAPPDATA") == 0 && local_app_data) {
        std::string dir = std::string(local_app_data) + "\\CxxIME\\logs";
        free(local_app_data);
        return dir;
    }
    return "";
}

static std::string get_trace_path() {
    return get_log_dir() + "\\server-trace.jsonl";
}

static void ensure_log_dir() {
    std::string dir = get_log_dir();
    if (!dir.empty()) {
        CreateDirectoryA((get_log_dir().substr(0, get_log_dir().rfind('\\'))).c_str(), nullptr);
        CreateDirectoryA(dir.c_str(), nullptr);
    }
}

static void rotate_log_file(FILE*& file, size_t& file_size) {
    if (file) {
        fclose(file);
        file = nullptr;
    }

    std::string base_path = get_trace_path();

    // Delete oldest generation
    std::string oldest = base_path + "." + std::to_string(kMaxLogGenerations);
    DeleteFileA(oldest.c_str());

    // Rotate existing files
    for (int i = kMaxLogGenerations - 1; i >= 1; --i) {
        std::string from = base_path + "." + std::to_string(i);
        std::string to = base_path + "." + std::to_string(i + 1);
        MoveFileA(from.c_str(), to.c_str());
    }

    MoveFileA(base_path.c_str(), (base_path + ".1").c_str());
    file_size = 0;
}

static void cleanup_old_tsf_logs() {
    std::string dir = get_log_dir();
    if (dir.empty()) return;

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA((dir + "\\tsf-*").c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return;

    auto now = std::chrono::system_clock::now();
    auto seven_days = std::chrono::hours(24 * 7);

    do {
        std::string filename = dir + "\\" + find_data.cFileName;
        FILETIME ft = find_data.ftLastWriteTime;
        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        auto file_time = std::chrono::system_clock::from_time_t(
            static_cast<time_t>((ull.QuadPart - 116444736000000000ULL) / 10000000ULL));

        if (now - file_time > seven_days) {
            DeleteFileA(filename.c_str());
        }
    } while (FindNextFileA(hFind, &find_data));
    FindClose(hFind);
}

static void cleanup_oversized_logs() {
    std::string dir = get_log_dir();
    if (dir.empty()) return;

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return;

    struct FileInfo {
        std::string path;
        FILETIME last_write;
        ULONGLONG size;
    };
    std::vector<FileInfo> files;
    ULONGLONG total_size = 0;

    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string filename = find_data.cFileName;
            if (filename.find(".log") != std::string::npos ||
                filename.find(".jsonl") != std::string::npos) {
                ULARGE_INTEGER size;
                size.LowPart = find_data.nFileSizeLow;
                size.HighPart = find_data.nFileSizeHigh;
                total_size += size.QuadPart;
                files.push_back({
                    dir + "\\" + filename,
                    find_data.ftLastWriteTime,
                    size.QuadPart
                });
            }
        }
    } while (FindNextFileA(hFind, &find_data));
    FindClose(hFind);

    if (total_size > kMaxTotalLogSize) {
        std::sort(files.begin(), files.end(),
            [](const FileInfo& a, const FileInfo& b) {
                ULARGE_INTEGER a_time, b_time;
                a_time.LowPart = a.last_write.dwLowDateTime;
                a_time.HighPart = a.last_write.dwHighDateTime;
                b_time.LowPart = b.last_write.dwLowDateTime;
                b_time.HighPart = b.last_write.dwHighDateTime;
                return a_time.QuadPart < b_time.QuadPart;
            });

        for (const auto& file : files) {
            if (total_size <= kTargetTotalLogSize) break;
            DeleteFileA(file.path.c_str());
            total_size -= file.size;
        }
    }
}

// ─── Writer thread ───────────────────────────────────────────

static void writer_thread_func() {
    ensure_log_dir();
    cleanup_old_tsf_logs();

    FILE* file = nullptr;
    size_t file_size = 0;
    int write_count = 0;

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
        // Wait for entries or shutdown
        {
            std::unique_lock<std::mutex> lock(g_shutdown_mutex);
            g_shutdown_cv.wait_for(lock, kFlushInterval, [] {
                return g_shutdown.load(std::memory_order_relaxed);
            });
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

            // Check rotation
            if (file_size + entry.len + 1 > kMaxLogSize) {
                rotate_log_file(file, file_size);
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

        // Periodic cleanup
        write_count += count;
        if (write_count >= 1000) {
            cleanup_oversized_logs();
            write_count = 0;
        }
    }

    // Final flush on shutdown
    if (file) {
        // Drain remaining entries
        int count = g_queue.pop_batch(batch, kBatchSize);
        while (count > 0) {
            for (int i = 0; i < count; ++i) {
                const auto& entry = batch[i];
                if (file_size + entry.len + 1 > kMaxLogSize) {
                    rotate_log_file(file, file_size);
                    file = fopen(path.c_str(), "a");
                    if (!file) break;
                    file_size = 0;
                }
                fwrite(entry.json, 1, entry.len, file);
                fputc('\n', file);
                file_size += entry.len + 1;
            }
            count = g_queue.pop_batch(batch, kBatchSize);
        }
        fclose(file);
    }
}

static void ensure_writer_started() {
    if (g_writer_started.exchange(true)) return;
    g_writer_thread = std::thread(writer_thread_func);
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
        "\"exact_scan\":%u,\"prefix_scan\":%u,\"user_scan\":%u,"
        "\"cache\":%s,\"deadline\":%s,\"cancelled\":%s,\"truncated\":%s,"
        "\"proc_us\":%lld,\"trans_us\":%lld,\"lookup_us\":%lld,\"merge_us\":%lld,\"total_us\":%lld}",
        (unsigned long long)query_id,
        (unsigned)session_id,
        (unsigned long long)revision,
        escaped_input,
        page_index, page_size,
        syllable_path_count, live_path_count, candidate_count,
        exact_scan_count, prefix_scan_count, user_scan_count,
        cache_hit ? "true" : "false",
        deadline_exceeded ? "true" : "false",
        cancelled ? "true" : "false",
        truncated ? "true" : "false",
        (long long)processor_us,
        (long long)translate_us,
        (long long)lookup_us,
        (long long)merge_us,
        (long long)total_us);

    if (written < 0 || written >= buf_size) return 0;
    return written;
}

bool QueryTrace::should_log() const {
    // Always log if deadline exceeded or cancelled
    if (deadline_exceeded || cancelled)
        return true;

    // Log slow queries
    if (total_us >= kSlowQueryUs)
        return true;

    // NOTE: truncated is NOT unconditionally logged — page_size truncation
    // is normal and expected. Only ScanBudget/Deadline truncation should
    // trigger logging, which requires truncate_reasons field (future work).

    return false;
}

void QueryTrace::log() const {
    char json_buf[1024];
    int len = to_json(json_buf, sizeof(json_buf));
    if (len <= 0) return;

#ifdef _DEBUG
    // Debug: OutputDebugStringW (non-blocking)
    wchar_t wbuf[1024];
    MultiByteToWideChar(CP_UTF8, 0, json_buf, len, wbuf, 1024);
    wbuf[len] = L'\n';
    wbuf[len + 1] = L'\0';
    OutputDebugStringW(wbuf);
#endif

    // Async: enqueue to bounded queue (non-blocking, drop if full)
    ensure_writer_started();

    TraceEntry entry;
    std::memcpy(entry.json, json_buf, len);
    entry.json[len] = '\0';
    entry.len = len;

    g_queue.try_push(entry);  // Drop if queue full - never block hot path
}

// ─── Shutdown (call at process exit) ─────────────────────────

void QueryTrace::shutdown() {
    if (!g_writer_started.exchange(false))
        return;

    g_shutdown.store(true, std::memory_order_relaxed);
    g_shutdown_cv.notify_all();

    if (g_writer_thread.joinable()) {
        g_writer_thread.join();
    }
}

} // namespace cxxime
