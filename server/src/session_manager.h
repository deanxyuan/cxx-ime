// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SESSION_MANAGER_H_
#define CXXIME_SESSION_MANAGER_H_

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cxxime/composition_learning.h>
#include <cxxime/config.h>
#include <cxxime/engine.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/output_composer.h>
#include <cxxime/punct_types.h>
#include <cxxime/spellings_index.h>
#include <cxxime/symbol_table.h>
#include <cxxime/syllabifier.h>
#include <cxxime/user_dict.h>

struct SharedResourceSnapshot {
    std::shared_ptr<cxxime::Dict> dict;
    std::shared_ptr<cxxime::Dict> wubi_dict;
    std::shared_ptr<cxxime::SpellingsIndex> spellings;
    std::shared_ptr<cxxime::Syllabifier> syllabifier;
    std::shared_ptr<const cxxime::SymbolTable> symbol_table;
    std::shared_ptr<const cxxime::Config> config;
    std::shared_ptr<const cxxime::PunctMapping> punct_mapping;
    std::shared_ptr<cxxime::CompositionLearningService> composition_learning;
};

// Replaceable resources shared across all sessions.
struct SharedResources {
    std::shared_ptr<cxxime::Dict> dict;
    std::shared_ptr<cxxime::Dict> wubi_dict;
    std::shared_ptr<cxxime::SpellingsIndex> spellings;
    std::shared_ptr<cxxime::Syllabifier> syllabifier;
    std::shared_ptr<const cxxime::SymbolTable> symbol_table;
    std::shared_ptr<const cxxime::Config> config;
    std::shared_ptr<const cxxime::PunctMapping> punct_mapping;
    std::shared_ptr<cxxime::CompositionLearningService> composition_learning;
    std::string punct_path;   // Stored for reload
    std::string dict_path;    // Stored for dictionary reload
    std::string wubi_dict_path;
    std::string manifest_path;
    mutable std::mutex mutex;

    bool load(const std::string& dict_path,
        const std::shared_ptr<const cxxime::Config>& config);
    SharedResourceSnapshot snapshot() const;
    std::shared_ptr<cxxime::Dict> dict_for_kind(cxxime::UserDictKind kind) const;
    bool load_punctuation(const std::string& path);
    void replace_config(const std::shared_ptr<const cxxime::Config>& next_config);
    bool reload_dictionaries();
    cxxime::IPCStatus add_user_entry(cxxime::UserDictKind kind,
        const std::string& text, const std::string& code);
    cxxime::UserDictQueryResult query_user_entries(const std::string& query,
                                                   cxxime::UserDictKind kind,
                                                   size_t offset, size_t limit);
    cxxime::UserDictQueryResult query_lexicon_entries(
        cxxime::LexiconResource resource, const std::string& query,
        cxxime::UserDictKind kind, size_t offset, size_t limit, bool exact_text = false);
    cxxime::UserDictQueryResult query_disabled_system_entry_status(
        cxxime::UserDictKind kind, const std::vector<std::string>& texts);
    cxxime::IPCStatus delete_user_entries(
        cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries);
    cxxime::IPCStatus replace_user_entry(cxxime::UserDictKind kind,
                                         const std::string& old_text,
                                         const std::string& old_code,
                                         const std::string& new_text,
                                         const std::string& new_code);
    cxxime::IPCStatus import_user_dict(cxxime::UserDictKind kind,
                                       const std::string& source_path);
    cxxime::IPCStatus save_user_dict(cxxime::UserDictKind kind);
    cxxime::IPCStatus disable_system_entry(cxxime::UserDictKind kind,
                                           const std::string& text);
    cxxime::IPCStatus restore_system_entry(cxxime::UserDictKind kind,
                                           const std::string& text);
    cxxime::IPCStatus delete_candidate_preferences(
        cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries);
    cxxime::IPCStatus clear_candidate_preferences(cxxime::UserDictKind kind);
    cxxime::IPCStatus save_candidate_preferences(cxxime::UserDictKind kind);
    cxxime::CandidateOrderQueryResult query_candidate_order(cxxime::UserDictKind kind,
                                                            const std::string& code,
                                                            std::size_t limit);
    cxxime::IPCStatus replace_candidate_order(
        cxxime::UserDictKind kind, const std::string& code,
        const std::vector<cxxime::ManualCandidateOrderEntry>& entries,
        std::uint64_t expected_version, bool* version_conflict);
    cxxime::IPCStatus clear_candidate_order(cxxime::UserDictKind kind,
                                            const std::string& code,
                                            std::uint64_t expected_version,
                                            bool* version_conflict);
    bool save_candidate_preferences(bool force);
    bool freeze_and_stop_composition_learning();
};

struct SessionEntry {
    std::unique_ptr<cxxime::Engine> engine;
    std::chrono::steady_clock::time_point last_activity;
    cxxime::ImeStatus ime_status;
    bool base_chinese_mode = true;
    bool full_shape = false;
    bool chinese_punct = true;
    bool closing = false;
    SharedResourceSnapshot resources;
    std::mutex mutex;  // per-session concurrency protection
};

struct ProcessKeyResult {
    cxxime::IPCStatus status = cxxime::IPCStatus::ERR_INVALID_SESSION;
    cxxime::ProcessResult result = cxxime::ProcessResult::REJECTED;
    std::string commit_text;
    bool composing = false;
    std::string preedit;
    size_t preedit_cursor = 0;
    cxxime::CandidatePage candidates;
    cxxime::ImeStatus ime_status;
};

class SessionManager {
public:
    using ConfigPatchHandler = std::function<void(const std::string& merge_patch_json)>;

    bool initialize(const std::string& dict_path,
                    const std::shared_ptr<const cxxime::Config>& config =
                    std::make_shared<const cxxime::Config>());
    uint32_t create_session();
    void destroy_session(uint32_t id);
    void touch_session(uint32_t id);

    size_t cleanup_idle_sessions(uint32_t timeout_ms);
    void apply_config(const std::shared_ptr<const cxxime::Config>& config);
    void set_config_patch_handler(ConfigPatchHandler handler);
    cxxime::IPCStatus reload_dictionaries();
    bool reload_punctuation(const std::string& path);

    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> get_ime_status(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> toggle_chinese(uint32_t id);
    ProcessKeyResult set_chinese_mode(uint32_t id, bool chinese_mode);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> toggle_shape(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> toggle_punct(uint32_t id);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> switch_input_mode(uint32_t id, cxxime::InputMode mode);
    cxxime::IPCStatus sync_ascii_mode(uint32_t id, bool ascii_mode);
    std::pair<cxxime::IPCStatus, cxxime::ImeStatus> sync_caps_lock(uint32_t id, bool caps_lock);

    ProcessKeyResult process_key(uint32_t id, const cxxime::KeyEvent& event,
                                 uint32_t visible_candidate_count = 0);
    cxxime::CandidatePage search_candidates(const std::string& input);
    bool record_search_result(const std::string& input, const std::string& result);
    ProcessKeyResult select_candidate(uint32_t id, int index);
    ProcessKeyResult commit_composition(uint32_t id);
    cxxime::IPCStatus clear_composition(uint32_t id);
    cxxime::IPCStatus focus_out(uint32_t id);

    cxxime::IPCStatus add_user_entry(cxxime::UserDictKind kind, const std::string& text,
                                     const std::string& code);
    cxxime::UserDictQueryResult query_user_entries(const std::string& query,
                                                   cxxime::UserDictKind kind,
                                                   size_t offset, size_t limit);
    cxxime::UserDictQueryResult query_lexicon_entries(
        cxxime::LexiconResource resource, const std::string& query,
        cxxime::UserDictKind kind, size_t offset, size_t limit, bool exact_text = false);
    cxxime::UserDictQueryResult query_disabled_system_entry_status(
        cxxime::UserDictKind kind, const std::vector<std::string>& texts);
    cxxime::IPCStatus delete_user_entries(
        cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries);
    cxxime::IPCStatus replace_user_entry(cxxime::UserDictKind kind,
                                         const std::string& old_text, const std::string& old_code,
                                         const std::string& new_text, const std::string& new_code);
    cxxime::IPCStatus import_user_dict(cxxime::UserDictKind kind,
                                       const std::string& source_path);
    cxxime::IPCStatus save_user_dict(cxxime::UserDictKind kind);
    cxxime::IPCStatus disable_system_entry(cxxime::UserDictKind kind,
                                           const std::string& text);
    cxxime::IPCStatus restore_system_entry(cxxime::UserDictKind kind,
                                           const std::string& text);
    cxxime::IPCStatus delete_candidate_preferences(
        cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries);
    cxxime::IPCStatus clear_candidate_preferences(cxxime::UserDictKind kind);
    cxxime::IPCStatus save_candidate_preferences(cxxime::UserDictKind kind);
    cxxime::CandidateOrderQueryResult query_candidate_order(cxxime::UserDictKind kind,
                                                            const std::string& code,
                                                            std::size_t limit);
    cxxime::IPCStatus replace_candidate_order(
        cxxime::UserDictKind kind, const std::string& code,
        const std::vector<cxxime::ManualCandidateOrderEntry>& entries,
        std::uint64_t expected_version, bool* version_conflict);
    cxxime::IPCStatus clear_candidate_order(cxxime::UserDictKind kind,
                                            const std::string& code,
                                            std::uint64_t expected_version,
                                            bool* version_conflict);
    bool save_candidate_preferences(bool force);
    bool freeze_and_save_candidate_preferences();
    bool freeze_and_stop_composition_learning();

private:
    struct GlobalVisibleState {
        bool caps_lock = false;
        cxxime::InputMode input_mode = cxxime::InputMode::PINYIN;
    };

    cxxime::Engine* get_engine(uint32_t id);

    // Helper: two-phase lock lookup. Returns nullptr if session not found.
    std::shared_ptr<SessionEntry> lookup_session(uint32_t id);

    void reset_global_state(const SharedResourceSnapshot& resources);
    GlobalVisibleState snapshot_global_state();
    void commit_global_state(GlobalVisibleState next);
    void align_session_to_global(SessionEntry& entry);

    void persist_input_mode(cxxime::InputMode mode);

    SharedResources shared_;
    std::unordered_map<uint32_t, std::shared_ptr<SessionEntry>> sessions_;
    GlobalVisibleState global_state_;
    uint32_t next_id_ = 1;
    std::mutex mutex_;
    std::mutex state_mutex_;
    std::mutex reload_mutex_;
    ConfigPatchHandler config_patch_handler_;
};

#endif // CXXIME_SESSION_MANAGER_H_
