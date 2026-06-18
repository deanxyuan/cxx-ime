// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager.h"
#include <windows.h>
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include <json.hpp>
#include <fstream>

namespace {

void parse_punct_section(const nlohmann::json& j, const char* section,
                         std::unordered_map<std::string, cxxime::PunctEntry>& target) {
    if (!j.contains(section) || !j[section].is_object())
        return;
    for (auto it = j[section].begin(); it != j[section].end(); ++it) {
        std::string key = it.key();
        const nlohmann::json& val = it.value();
        if (!val.is_object()) continue;
        cxxime::PunctEntry entry{};
        if (val.contains("commit") && val["commit"].is_string()) {
            entry.type = cxxime::PunctType::COMMIT;
            entry.commit = val["commit"].get<std::string>();
        } else if (val.contains("pair") && val["pair"].is_array()) {
            entry.type = cxxime::PunctType::PAIR;
            for (const auto& item : val["pair"]) {
                if (item.is_string())
                    entry.pair.push_back(item.get<std::string>());
            }
        } else if (val.contains("alternatives") && val["alternatives"].is_array()) {
            entry.type = cxxime::PunctType::ALTERNATIVES;
            for (const auto& item : val["alternatives"]) {
                if (item.is_string())
                    entry.alternatives.push_back(item.get<std::string>());
            }
        } else {
            CXXIME_LOG(L"Punct: unknown entry type for key '%S'", key.c_str());
            continue;
        }
        target[key] = std::move(entry);
    }
}

}  // anonymous namespace

bool SharedResources::load(const std::string& dict_path, const std::string& cfg_path) {
    std::string user_dict_path = cxxime::user_data_path("user.tsv");
    if (!dict.open(dict_path, user_dict_path)) {
        CXXIME_LOG(L"SharedResources: dict.open FAILED");
        return false;
    }
    auto cfg = std::make_shared<cxxime::Config>();
    if (!cfg_path.empty()) {
        config_path = cfg_path;
        cfg->load(config_path);
        // Overlay user config from %APPDATA%
        cfg->load(cxxime::user_data_path("default.json"));
        cfg->load_themes(cxxime::data_path("themes.json"));
    }
    config = std::move(cfg);

    // Load punctuation mapping (non-fatal)
    load_punctuation(cxxime::data_path("punctuation.json"));
    std::string sp_path = cxxime::Engine::derive_spellings_path(dict_path);
    if (!sp_path.empty() && spellings.load(sp_path) && spellings.has_spellings()) {
        syllabifier = std::make_unique<cxxime::Syllabifier>(spellings);
    }

    // 加载五笔词典（可选，失败不影响）
    std::string wubi_dict_path = cxxime::data_path("wubi86.dict.bin");
    if (!wubi_dict.open(wubi_dict_path)) {
        CXXIME_LOG(L"SharedResources: wubi dict not found, wubi mode disabled");
    } else {
        CXXIME_LOG(L"SharedResources: wubi dict loaded");
    }

    return true;
}

bool SharedResources::load_punctuation(const std::string& path) {
    if (path.empty()) return true;

    std::ifstream file(path);
    if (!file.is_open()) {
        CXXIME_LOG(L"SharedResources: punctuation file not found: %S", path.c_str());
        return false;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(file);

        auto mapping = std::make_shared<cxxime::PunctMapping>();

        parse_punct_section(j, "half_shape", mapping->half_shape);
        parse_punct_section(j, "full_shape", mapping->full_shape);

        punct_mapping = std::move(mapping);
        punct_path = path;
        CXXIME_LOG(L"SharedResources: punctuation loaded (%zu half, %zu full)",
                   punct_mapping->half_shape.size(), punct_mapping->full_shape.size());
        return true;
    } catch (const nlohmann::json::exception& e) {
        CXXIME_LOG(L"SharedResources: punctuation parse error: %S", e.what());
        return false;
    }
}

bool SessionManager::initialize(const std::string& dict_path, const std::string& config_path) {
    return shared_.load(dict_path, config_path);
}

uint32_t SessionManager::create_session() {
    auto engine = std::make_unique<cxxime::Engine>();
    // Snapshot config under mutex (reload_config may race)
    std::shared_ptr<const cxxime::Config> cfg_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg_snapshot = shared_.config;
    }
    if (!cfg_snapshot) return 0;
    if (!engine->initialize(shared_.dict, shared_.spellings,
                            shared_.syllabifier.get(), *cfg_snapshot))
        return 0;
    if (shared_.wubi_dict.is_open()) {
        engine->set_wubi_dict(&shared_.wubi_dict);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = next_id_++;
    auto entry = std::make_shared<SessionEntry>();
    entry->engine = std::move(engine);
    entry->last_activity = std::chrono::steady_clock::now();
    entry->config_snapshot = std::move(cfg_snapshot);
    entry->ime_status.input_mode = static_cast<cxxime::InputMode>(entry->config_snapshot->input_mode);
    entry->engine->switch_mode(entry->ime_status.input_mode);
    entry->engine->set_fuzzy_enabled(entry->config_snapshot->fuzzy_pinyin);
    sessions_[id] = entry;
    return id;
}

void SessionManager::destroy_session(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(id);
}

std::shared_ptr<SessionEntry> SessionManager::lookup_session(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second->last_activity = std::chrono::steady_clock::now();
        return it->second;
    }
    return nullptr;
}

cxxime::Engine* SessionManager::get_engine(uint32_t id) {
    auto entry = lookup_session(id);
    return entry ? entry->engine.get() : nullptr;
}

void SessionManager::touch_session(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second->last_activity = std::chrono::steady_clock::now();
    }
}

size_t SessionManager::cleanup_idle_sessions(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    size_t count = 0;
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second->last_activity).count();
        if (static_cast<uint32_t>(idle) > timeout_ms) {
            it = sessions_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

void SharedResources::reload_config() {
    if (config_path.empty())
        return;
    CXXIME_LOG(L"SharedResources: reloading config from %S", config_path.c_str());
    auto cfg = std::make_shared<cxxime::Config>();
    cfg->load(config_path);
    cfg->load(cxxime::user_data_path("default.json"));
    cfg->load_themes(cxxime::data_path("themes.json"));
    config = std::move(cfg);
    CXXIME_LOG(L"SharedResources: config reloaded");

    // Reload punctuation mapping
    if (!punct_path.empty()) {
        load_punctuation(punct_path);
    }
}

void SessionManager::reload_config() {
    // Hold sessions_mutex_ to synchronize with process_key's first-phase lookup
    std::lock_guard<std::mutex> lock(mutex_);
    auto new_mode = shared_.config ? static_cast<cxxime::InputMode>(shared_.config->input_mode)
                                   : cxxime::InputMode::PINYIN;
    shared_.reload_config();
    // Sync input_mode from config to all active sessions
    if (shared_.config) {
        auto target = static_cast<cxxime::InputMode>(shared_.config->input_mode);
        for (auto& [id, entry] : sessions_) {
            std::lock_guard<std::mutex> elock(entry->mutex);
            if (entry->ime_status.input_mode != target) {
                entry->engine->switch_mode(target);
                entry->ime_status.input_mode = target;
                entry->ime_status.revision++;
            }
        }
        {
            bool fuzzy = shared_.config->fuzzy_pinyin;
            for (auto& [id, entry] : sessions_) {
                std::lock_guard<std::mutex> elock(entry->mutex);
                entry->engine->set_fuzzy_enabled(fuzzy);
            }
        }
    }
    CXXIME_LOG(L"SessionManager: config reloaded, %zu active sessions", sessions_.size());
}

bool SessionManager::reload_punctuation(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return shared_.load_punctuation(path);
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::get_ime_status(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    return {cxxime::IPCStatus::OK, entry->ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::toggle_chinese(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    s.ime_status.chinese_mode = !s.ime_status.chinese_mode;
    s.ime_status.revision++;
    s.engine->ascii_composer().set_ascii_mode(!s.ime_status.chinese_mode);
    return {cxxime::IPCStatus::OK, s.ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::toggle_shape(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    s.ime_status.full_shape = !s.ime_status.full_shape;
    s.ime_status.revision++;
    return {cxxime::IPCStatus::OK, s.ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::toggle_punct(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    s.ime_status.chinese_punct = !s.ime_status.chinese_punct;
    s.ime_status.revision++;
    return {cxxime::IPCStatus::OK, s.ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::switch_input_mode(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    auto target = (s.ime_status.input_mode == cxxime::InputMode::PINYIN)
        ? cxxime::InputMode::WUBI : cxxime::InputMode::PINYIN;
    s.engine->switch_mode(target);
    s.ime_status.input_mode = s.engine->mode();  // sync with actual engine mode
    s.ime_status.revision++;
    persist_input_mode(s.ime_status.input_mode);
    return {cxxime::IPCStatus::OK, s.ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::switch_input_mode(uint32_t id, cxxime::InputMode mode) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    s.engine->switch_mode(mode);
    s.ime_status.input_mode = s.engine->mode();  // sync with actual engine mode
    s.ime_status.revision++;
    persist_input_mode(s.ime_status.input_mode);
    return {cxxime::IPCStatus::OK, s.ime_status};
}

cxxime::IPCStatus SessionManager::sync_ascii_mode(uint32_t id, bool ascii_mode) {
    auto entry = lookup_session(id);
    if (!entry) return cxxime::IPCStatus::ERR_INVALID_SESSION;
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->ime_status.chinese_mode = !ascii_mode;
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SessionManager::sync_caps_lock(uint32_t id, bool caps_lock) {
    auto entry = lookup_session(id);
    if (!entry) return cxxime::IPCStatus::ERR_INVALID_SESSION;
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->ime_status.caps_lock = caps_lock;
    return cxxime::IPCStatus::OK;
}

ProcessKeyResult SessionManager::process_key(uint32_t id, const cxxime::KeyEvent& event) {
    // Two-phase lock: lookup session and copy shared_ptr, then lock session
    std::shared_ptr<SessionEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            ProcessKeyResult err;
            err.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
            return err;
        }
        entry = it->second;
    }
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    auto& engine = *s.engine;

    // 0. Check config snapshot — detect hot reload
    {
        std::shared_ptr<const cxxime::Config> current_cfg;
        {
            std::lock_guard<std::mutex> map_lock(mutex_);
            current_cfg = shared_.config;
        }
        if (current_cfg && current_cfg.get() != s.config_snapshot.get()) {
            engine.reload_config(*current_cfg);
            s.config_snapshot = std::move(current_cfg);
        }
    }

    // 1. sync caps_lock (must happen before OutputOptions derivation)
    s.ime_status.caps_lock = event.is_caps_lock();

    // 2. derive OutputOptions
    auto opts = cxxime::OutputOptions::from(s.ime_status);
    opts.punct_mapping = shared_.punct_mapping.get();

    // 3. set trace
    engine.set_trace_session_id(id);

    // 4. call Engine
    auto result = engine.process_key(event, opts);

    // 5. sync ascii_mode -> ime_status.chinese_mode
    bool old_ascii = !s.ime_status.chinese_mode;
    bool new_ascii = engine.ascii_composer().is_ascii_mode();
    s.ime_status.chinese_mode = !new_ascii;
    if (old_ascii != new_ascii) {
        s.ime_status.revision++;
    }

    // 6. populate return value
    //    Key: process COMMITTED first (take + clear context), THEN read composing.
    //    COMMITTED sets committed_text but does NOT clear pinyin_buffer,
    //    so is_composing() would return true if read before take.
    ProcessKeyResult ret;
    ret.status = cxxime::IPCStatus::OK;
    ret.result = result;

    // Handle toggle results — flip status and return updated ImeStatus
    if (result == cxxime::ProcessResult::TOGGLE_SHAPE) {
        s.ime_status.full_shape = !s.ime_status.full_shape;
        s.ime_status.revision++;
    } else if (result == cxxime::ProcessResult::TOGGLE_PUNCT) {
        // In English mode, Ctrl+. also switches to Chinese mode
        if (!s.ime_status.chinese_mode) {
            s.ime_status.chinese_mode = true;
            engine.ascii_composer().set_ascii_mode(false);
        }
        s.ime_status.chinese_punct = !s.ime_status.chinese_punct;
        s.ime_status.revision++;
    }

    ret.ime_status = s.ime_status;

    if (result == cxxime::ProcessResult::COMMITTED) {
        auto [raw, source] = engine.take_commit_text_with_source();
        ret.commit_text = cxxime::OutputComposer::transform(raw, opts, source,
                                                            s.config_snapshot->good_old_caps_lock);
        ret.composing = false;
    } else if (result == cxxime::ProcessResult::TOGGLE_PUNCT
            || result == cxxime::ProcessResult::TOGGLE_SHAPE) {
        // Toggle results should not carry stale preedit — clear the composition
        // so the TSF client ends the inline display cleanly.
        engine.clear_composition();
        ret.composing = false;
    } else {
        ret.composing = engine.context().is_composing();
        if (ret.composing) {
            ret.preedit = engine.context().pinyin_buffer;
            ret.candidates = engine.context().candidates;
        }
    }

    // trace log
    if (engine.last_trace().should_log()) {
        engine.last_trace().log();
    }

    return ret;
}

ProcessKeyResult SessionManager::select_candidate(uint32_t id, int index) {
    std::shared_ptr<SessionEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            ProcessKeyResult err;
            err.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
            return err;
        }
        entry = it->second;
    }
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    auto opts = cxxime::OutputOptions::from(s.ime_status);
    ProcessKeyResult ret;
    ret.status = cxxime::IPCStatus::OK;
    ret.ime_status = s.ime_status;

    if (s.engine->select_candidate(index)) {
        auto [raw, source] = s.engine->take_commit_text_with_source();
        ret.commit_text = cxxime::OutputComposer::transform(raw, opts, source,
                                                            s.config_snapshot->good_old_caps_lock);
        ret.result = cxxime::ProcessResult::COMMITTED;
    } else {
        ret.result = cxxime::ProcessResult::REJECTED;
    }
    return ret;
}

ProcessKeyResult SessionManager::commit_composition(uint32_t id) {
    std::shared_ptr<SessionEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            ProcessKeyResult err;
            err.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
            return err;
        }
        entry = it->second;
    }
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    auto opts = cxxime::OutputOptions::from(s.ime_status);
    ProcessKeyResult ret;
    ret.status = cxxime::IPCStatus::OK;
    ret.ime_status = s.ime_status;

    auto [raw, source] = s.engine->commit_composition_with_source();
    if (!raw.empty()) {
        ret.commit_text = cxxime::OutputComposer::transform(raw, opts, source,
                                                            s.config_snapshot->good_old_caps_lock);
        ret.result = cxxime::ProcessResult::COMMITTED;
    } else {
        ret.result = cxxime::ProcessResult::ACCEPTED;
    }
    ret.composing = false;
    s.ime_status.revision++;
    ret.ime_status = s.ime_status;
    return ret;
}

cxxime::IPCStatus SessionManager::clear_composition(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return cxxime::IPCStatus::ERR_INVALID_SESSION;
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->engine->clear_composition();
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SessionManager::focus_out(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return cxxime::IPCStatus::ERR_INVALID_SESSION;
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->engine->clear_composition();
    entry->ime_status.revision++;
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SessionManager::add_user_entry(uint32_t id, const std::string& text, const std::string& code) {
    if (text.empty() || code.empty()) return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    // Operates on shared dict — no session validation needed.
    shared_.dict.update_frequency(text, code);
    shared_.dict.save_user_dict();
    return cxxime::IPCStatus::OK;
}

void SessionManager::persist_input_mode(cxxime::InputMode mode) {
    // Only write to user config directory (program dir may be read-only).
    const std::string path = cxxime::user_data_path("default.json");
    if (path.empty()) return;

    try {
        // Read existing JSON (may not exist yet)
        nlohmann::json j;
        std::ifstream in(path);
        if (in.is_open()) {
            in >> j;
            in.close();
        }

        j["engine"]["input_mode"] = static_cast<int>(mode);

        std::ofstream out(path);
        if (!out.is_open()) {
            CXXIME_LOG(L"persist_input_mode: cannot write %S", path.c_str());
            return;
        }
        out << j.dump(4) << "\n";
    } catch (const std::exception& e) {
        CXXIME_LOG(L"persist_input_mode: exception: %S", e.what());
    }
}
