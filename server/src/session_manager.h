// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SESSION_MANAGER_H_
#define CXXIME_SESSION_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <cxxime/ipc_protocol.h>
#include <cxxime/engine.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/config.h>

// Read-only resources shared across all sessions, loaded once at server startup.
struct SharedResources {
    cxxime::Dict dict;
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    std::unique_ptr<cxxime::Syllabifier> syllabifier;
    std::string config_path;  // Stored for reload

    bool load(const std::string& dict_path, const std::string& config_path);
    void reload_config();
};

struct SessionEntry {
    std::unique_ptr<cxxime::Engine> engine;
    std::chrono::steady_clock::time_point last_activity;
    cxxime::ImeStatus ime_status;
};

class SessionManager {
public:
    bool initialize(const std::string& dict_path, const std::string& config_path = "");
    uint32_t create_session();
    void destroy_session(uint32_t id);
    cxxime::Engine* get_engine(uint32_t id);
    void touch_session(uint32_t id);

    size_t cleanup_idle_sessions(uint32_t timeout_ms);
    void reload_config();

    cxxime::ImeStatus get_ime_status(uint32_t id);
    cxxime::ImeStatus toggle_chinese(uint32_t id);
    cxxime::ImeStatus toggle_shape(uint32_t id);
    cxxime::ImeStatus toggle_punct(uint32_t id);
    cxxime::ImeStatus switch_input_mode(uint32_t id);
    void sync_ascii_mode(uint32_t id, bool ascii_mode);
    void sync_caps_lock(uint32_t id, bool caps_lock);

private:
    SharedResources shared_;
    std::unordered_map<uint32_t, SessionEntry> sessions_;
    uint32_t next_id_ = 1;
    std::mutex mutex_;
};

#endif // CXXIME_SESSION_MANAGER_H_
