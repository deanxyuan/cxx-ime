// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SESSION_MANAGER_H_
#define CXXIME_SESSION_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <unordered_map>
#include <chrono>
#include <cxxime/ipc_protocol.h>
#include <cxxime/engine.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/config.h>
#include <cxxime/output_composer.h>
#include <cxxime/punct_types.h>

// Read-only resources shared across all sessions, loaded once at server startup.
struct SharedResources {
    cxxime::Dict dict;
    cxxime::Dict wubi_dict;
    cxxime::SpellingsIndex spellings;
    std::shared_ptr<const cxxime::Config> config;
    std::unique_ptr<cxxime::Syllabifier> syllabifier;
    std::shared_ptr<const cxxime::PunctMapping> punct_mapping;
    std::string config_path;  // Stored for reload
    std::string punct_path;   // Stored for reload

    bool load(const std::string& dict_path, const std::string& config_path);
    bool load_punctuation(const std::string& path);
    void reload_config();
};

struct SessionEntry {
    std::unique_ptr<cxxime::Engine> engine;
    std::chrono::steady_clock::time_point last_activity;
    cxxime::ImeStatus ime_status;
    std::shared_ptr<const cxxime::Config> config_snapshot;  // Keeps Engine::config_ pointer valid
    std::mutex mutex;  // per-session concurrency protection
};

struct ProcessKeyResult {
    cxxime::IPCStatus status = cxxime::IPCStatus::ERR_INVALID_SESSION;
    cxxime::ProcessResult result = cxxime::ProcessResult::REJECTED;
    std::string commit_text;
    bool composing = false;
    std::string preedit;
    cxxime::CandidatePage candidates;
    cxxime::ImeStatus ime_status;
};

class SessionManager {
public:
    bool initialize(const std::string& dict_path, const std::string& config_path = "");
    uint32_t create_session();
    void destroy_session(uint32_t id);
    void touch_session(uint32_t id);

    size_t cleanup_idle_sessions(uint32_t timeout_ms);
    void reload_config();
    bool reload_punctuation(const std::string& path);

    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> get_ime_status(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> toggle_chinese(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> toggle_shape(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> toggle_punct(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> switch_input_mode(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> switch_input_mode(uint32_t id, cxxime::InputMode mode);
    cxxime::IPCStatus sync_ascii_mode(uint32_t id, bool ascii_mode);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> sync_caps_lock(uint32_t id, bool caps_lock);

    ProcessKeyResult process_key(uint32_t id, const cxxime::KeyEvent& event);
    ProcessKeyResult select_candidate(uint32_t id, int index);
    ProcessKeyResult commit_composition(uint32_t id);
    cxxime::IPCStatus clear_composition(uint32_t id);
    cxxime::IPCStatus focus_out(uint32_t id);

    cxxime::IPCStatus add_user_entry(uint32_t id, const std::string& text, const std::string& code);

private:
    cxxime::Engine* get_engine(uint32_t id);

    // Helper: two-phase lock lookup. Returns nullptr if session not found.
    std::shared_ptr<SessionEntry> lookup_session(uint32_t id);

    // Persist input_mode to config file so settings window stays in sync.
    void persist_input_mode(cxxime::InputMode mode);

    SharedResources shared_;
    std::unordered_map<uint32_t, std::shared_ptr<SessionEntry>> sessions_;
    uint32_t next_id_ = 1;
    std::mutex mutex_;
};

#endif // CXXIME_SESSION_MANAGER_H_
