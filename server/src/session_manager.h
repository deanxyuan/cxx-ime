// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SESSION_MANAGER_H_
#define CXXIME_SESSION_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <unordered_map>
#include <chrono>
#include <vector>
#include <cxxime/ipc_protocol.h>
#include <cxxime/engine.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/config.h>
#include <cxxime/output_composer.h>
#include <cxxime/punct_types.h>

struct SharedResourceSnapshot {
    std::shared_ptr<cxxime::Dict> dict;
    std::shared_ptr<cxxime::Dict> wubi_dict;
    std::shared_ptr<cxxime::SpellingsIndex> spellings;
    std::shared_ptr<cxxime::Syllabifier> syllabifier;
    std::shared_ptr<const cxxime::Config> config;
    std::shared_ptr<const cxxime::PunctMapping> punct_mapping;
};

// Replaceable resources shared across all sessions.
struct SharedResources {
    std::shared_ptr<cxxime::Dict> dict;
    std::shared_ptr<cxxime::Dict> wubi_dict;
    std::shared_ptr<cxxime::SpellingsIndex> spellings;
    std::shared_ptr<cxxime::Syllabifier> syllabifier;
    std::shared_ptr<const cxxime::Config> config;
    std::shared_ptr<const cxxime::PunctMapping> punct_mapping;
    std::string config_path;  // Stored for reload
    std::string punct_path;   // Stored for reload
    std::string dict_path;    // Stored for dictionary reload
    std::string wubi_dict_path;
    std::string manifest_path;
    mutable std::mutex mutex;

    bool load(const std::string& dict_path, const std::string& config_path);
    SharedResourceSnapshot snapshot() const;
    std::shared_ptr<cxxime::Dict> dict_for_kind(cxxime::UserDictKind kind) const;
    bool load_punctuation(const std::string& path);
    void reload_config();
    bool reload_dictionaries();
    cxxime::IPCStatus add_user_entry(cxxime::UserDictKind kind,
        const std::string& text, const std::string& code);
    std::vector<cxxime::UserDictEntryInfo> query_user_entries(
        const std::string& query, cxxime::UserDictKind kind, size_t limit, size_t& total);
    cxxime::IPCStatus delete_user_entry(cxxime::UserDictKind kind,
                                        const std::string& text,
                                        const std::string& code);
    cxxime::IPCStatus replace_user_entry(cxxime::UserDictKind kind,
                                         const std::string& old_text,
                                         const std::string& old_code,
                                         const std::string& new_text,
                                         const std::string& new_code);
    bool reload_user_dict(cxxime::UserDictKind kind);
    cxxime::IPCStatus save_user_dict(cxxime::UserDictKind kind);
};

struct SessionEntry {
    std::unique_ptr<cxxime::Engine> engine;
    std::chrono::steady_clock::time_point last_activity;
    cxxime::ImeStatus ime_status;
    SharedResourceSnapshot resources;
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
    cxxime::IPCStatus reload_dictionaries();
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

    cxxime::IPCStatus add_user_entry(uint32_t id, cxxime::UserDictKind kind,
                                     const std::string& text, const std::string& code);
    std::vector<cxxime::UserDictEntryInfo> query_user_entries(const std::string& query,
                                                              cxxime::UserDictKind kind,
                                                              size_t limit, size_t& total);
    cxxime::IPCStatus delete_user_entry(cxxime::UserDictKind kind,
                                        const std::string& text, const std::string& code);
    cxxime::IPCStatus replace_user_entry(cxxime::UserDictKind kind,
                                         const std::string& old_text, const std::string& old_code,
                                         const std::string& new_text, const std::string& new_code);
    cxxime::IPCStatus reload_user_dict(cxxime::UserDictKind kind);
    cxxime::IPCStatus save_user_dict(cxxime::UserDictKind kind);

private:
    struct GlobalVisibleState {
        cxxime::ImeStatus status;
        bool base_chinese_mode = true;
    };

    cxxime::Engine* get_engine(uint32_t id);

    // Helper: two-phase lock lookup. Returns nullptr if session not found.
    std::shared_ptr<SessionEntry> lookup_session(uint32_t id);

    void reset_global_state(const SharedResourceSnapshot& resources);
    GlobalVisibleState snapshot_global_state();
    cxxime::ImeStatus commit_global_state(GlobalVisibleState next);
    void align_session_to_global(SessionEntry& entry);

    // Persist input_mode to config file so settings window stays in sync.
    void persist_input_mode(cxxime::InputMode mode);

    SharedResources shared_;
    std::unordered_map<uint32_t, std::shared_ptr<SessionEntry>> sessions_;
    GlobalVisibleState global_state_;
    uint32_t next_id_ = 1;
    std::mutex mutex_;
    std::mutex state_mutex_;
    std::mutex reload_mutex_;
};

#endif // CXXIME_SESSION_MANAGER_H_
