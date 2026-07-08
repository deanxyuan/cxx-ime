// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/dictionary_monitor.h>
#include <cxxime/logging.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <utility>

namespace cxxime {
namespace {

struct FileSignature {
    bool exists = false;
    ULONGLONG size = 0;
    FILETIME last_write = {};
};

struct WatchedPath {
    std::wstring path;
    FileSignature signature;
};

struct DirectoryWatch {
    std::wstring dir;
    HANDLE handle = INVALID_HANDLE_VALUE;
};

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty())
        return {};

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                  text.c_str(), -1, nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (len <= 0) {
        code_page = CP_ACP;
        flags = 0;
        len = MultiByteToWideChar(code_page, flags, text.c_str(), -1, nullptr, 0);
    }
    if (len <= 1)
        return {};

    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(code_page, flags, text.c_str(), -1, &result[0], len);
    return result;
}

std::wstring normalize_path(const std::wstring& path) {
    if (path.empty())
        return {};

    DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0)
        return path;

    std::wstring normalized(static_cast<size_t>(required), L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), required, &normalized[0], nullptr);
    if (written == 0)
        return path;

    normalized.resize(static_cast<size_t>(written));
    return normalized;
}

std::wstring directory_of(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return L".";
    if (pos == 0)
        return path.substr(0, 1);
    if (pos == 2 && path.size() > 2 && path[1] == L':')
        return path.substr(0, 3);
    return path.substr(0, pos);
}

bool same_path(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

void append_unique_path(std::vector<std::wstring>& paths, const std::wstring& path) {
    if (path.empty())
        return;
    for (const auto& existing : paths) {
        if (same_path(existing, path))
            return;
    }
    paths.push_back(path);
}

FileSignature read_signature(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    FileSignature sig;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return sig;

    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return sig;

    sig.exists = true;
    sig.size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    sig.last_write = data.ftLastWriteTime;
    return sig;
}

bool operator==(const FileSignature& a, const FileSignature& b) {
    return a.exists == b.exists &&
           a.size == b.size &&
           a.last_write.dwLowDateTime == b.last_write.dwLowDateTime &&
           a.last_write.dwHighDateTime == b.last_write.dwHighDateTime;
}

bool operator!=(const FileSignature& a, const FileSignature& b) {
    return !(a == b);
}

std::vector<WatchedPath> make_watched_paths(const std::vector<std::string>& raw_paths) {
    std::vector<std::wstring> normalized_paths;
    normalized_paths.reserve(raw_paths.size());
    for (const auto& raw : raw_paths) {
        append_unique_path(normalized_paths, normalize_path(utf8_to_wide(raw)));
    }

    std::vector<WatchedPath> watched;
    watched.reserve(normalized_paths.size());
    for (const auto& path : normalized_paths) {
        WatchedPath item;
        item.path = path;
        item.signature = read_signature(path);
        watched.push_back(std::move(item));
    }
    return watched;
}

std::vector<std::wstring> watched_dirs_for(const std::vector<WatchedPath>& paths) {
    std::vector<std::wstring> dirs;
    dirs.reserve(paths.size());
    for (const auto& path : paths) {
        append_unique_path(dirs, directory_of(path.path));
    }
    return dirs;
}

std::vector<DirectoryWatch> open_directory_watches(const std::vector<WatchedPath>& paths) {
    std::vector<DirectoryWatch> watches;
    auto dirs = watched_dirs_for(paths);
    if (dirs.size() > MAXIMUM_WAIT_OBJECTS - 1) {
        CXXIME_LOG(L"%s", L"DictionaryMonitor: too many directories, polling only");
        return watches;
    }

    watches.reserve(dirs.size());
    for (const auto& dir : dirs) {
        HANDLE handle = FindFirstChangeNotificationW(
            dir.c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (handle == INVALID_HANDLE_VALUE) {
            CXXIME_LOG(L"DictionaryMonitor: cannot watch dir=%s", dir.c_str());
            continue;
        }
        watches.push_back({dir, handle});
    }
    return watches;
}

void close_directory_watches(std::vector<DirectoryWatch>& watches) {
    for (auto& watch : watches) {
        if (watch.handle != INVALID_HANDLE_VALUE) {
            FindCloseChangeNotification(watch.handle);
            watch.handle = INVALID_HANDLE_VALUE;
        }
    }
}

bool refresh_signatures(std::vector<WatchedPath>& watched) {
    bool changed = false;
    for (auto& item : watched) {
        FileSignature current = read_signature(item.path);
        if (current != item.signature) {
            item.signature = current;
            changed = true;
        }
    }
    return changed;
}

DWORD wait_for_change(HANDLE stop_event,
                      const std::vector<DirectoryWatch>& watches,
                      DWORD timeout_ms) {
    HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
    DWORD count = 0;
    handles[count++] = stop_event;
    for (const auto& watch : watches) {
        if (count >= MAXIMUM_WAIT_OBJECTS)
            break;
        if (watch.handle != INVALID_HANDLE_VALUE)
            handles[count++] = watch.handle;
    }
    return WaitForMultipleObjects(count, handles, FALSE, timeout_ms);
}

} // namespace

DictionaryMonitor::DictionaryMonitor() = default;

DictionaryMonitor::~DictionaryMonitor() {
    stop();
}

bool DictionaryMonitor::start(const std::vector<std::string>& paths,
                              ChangeCallback on_change,
                              DictionaryMonitorOptions options) {
    if (running_.load(std::memory_order_acquire))
        return true;
    if (paths.empty() || !on_change)
        return false;

    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_)
        return false;

    paths_ = paths;
    on_change_ = std::move(on_change);
    options_ = options;
    running_.store(true, std::memory_order_release);
    watcher_thread_ = std::thread([this] { watcher_func(); });
    return true;
}

void DictionaryMonitor::stop() {
    if (!running_.exchange(false))
        return;
    if (stop_event_)
        SetEvent(stop_event_);
    if (watcher_thread_.joinable())
        watcher_thread_.join();
    if (stop_event_) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
}

bool DictionaryMonitor::running() const {
    return running_.load(std::memory_order_acquire);
}

void DictionaryMonitor::watcher_func() {
    auto watched = make_watched_paths(paths_);
    auto watches = open_directory_watches(watched);

    uint32_t remaining_retries = 0;
    bool retry_pending = false;

    CXXIME_LOG(L"DictionaryMonitor: started paths=%u dirs=%u",
               static_cast<unsigned>(watched.size()),
               static_cast<unsigned>(watches.size()));

    while (running_.load(std::memory_order_acquire)) {
        DWORD timeout = retry_pending ? options_.retry_ms : options_.poll_ms;
        DWORD wait_result = wait_for_change(stop_event_, watches, timeout);
        if (wait_result == WAIT_OBJECT_0)
            break;

        bool notification = wait_result > WAIT_OBJECT_0 &&
                            wait_result < WAIT_OBJECT_0 + 1 + watches.size();
        if (notification) {
            DWORD watch_index = wait_result - WAIT_OBJECT_0 - 1;
            if (watch_index < watches.size()) {
                FindNextChangeNotification(watches[watch_index].handle);
            }
            if (WaitForSingleObject(stop_event_, options_.debounce_ms) == WAIT_OBJECT_0)
                break;
        }

        bool changed = refresh_signatures(watched);
        if (!changed && !retry_pending)
            continue;

        if (changed) {
            remaining_retries = options_.max_retries;
            retry_pending = false;
        }

        CXXIME_LOG(L"%s", L"DictionaryMonitor: dictionary files changed");
        bool ok = on_change_();
        if (ok) {
            refresh_signatures(watched);
            retry_pending = false;
            remaining_retries = 0;
            CXXIME_LOG(L"%s", L"DictionaryMonitor: reload succeeded");
        } else if (remaining_retries > 0) {
            --remaining_retries;
            retry_pending = true;
            CXXIME_LOG(L"%s", L"DictionaryMonitor: reload failed, retrying");
        } else {
            retry_pending = false;
            CXXIME_LOG(L"%s", L"DictionaryMonitor: reload failed, old resources kept");
        }
    }

    close_directory_watches(watches);
    CXXIME_LOG(L"%s", L"DictionaryMonitor: stopped");
}

} // namespace cxxime
