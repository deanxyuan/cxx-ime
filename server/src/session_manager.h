// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SESSION_MANAGER_H_
#define CXXIME_SESSION_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <cxxime/engine.h>

struct SessionEntry {
    std::unique_ptr<cxxime::Engine> engine;
    std::chrono::steady_clock::time_point last_activity;
};

class SessionManager {
public:
    uint32_t create_session(const std::string& dict_path, const std::string& config_path = "");
    void destroy_session(uint32_t id);
    cxxime::Engine* get_engine(uint32_t id);
    void touch_session(uint32_t id);

    // Heartbeat: destroy sessions idle longer than timeout_ms. Returns count of cleaned sessions.
    size_t cleanup_idle_sessions(uint32_t timeout_ms);

private:
    std::unordered_map<uint32_t, SessionEntry> sessions_;
    uint32_t next_id_ = 1;
    std::mutex mutex_;
};

#endif // CXXIME_SESSION_MANAGER_H_
