// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager.h"
#include <windows.h>
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include <cxxime/diagnostics_config.h>
#include <cxxime/dictionary_manifest.h>
#include <json.hpp>
#include <fstream>

namespace {

struct DictionaryResources {
    std::shared_ptr<cxxime::Dict> dict;
    std::shared_ptr<cxxime::Dict> wubi_dict;
    std::shared_ptr<cxxime::SpellingsIndex> spellings;
    std::shared_ptr<cxxime::Syllabifier> syllabifier;
    std::string dict_path;
    std::string wubi_dict_path;
    std::string manifest_path;
};

std::string user_dict_path_for(cxxime::UserDictKind kind) {
    return cxxime::user_data_path(kind == cxxime::UserDictKind::WUBI
                                  ? "user_wubi.tsv"
                                  : "user_pinyin.tsv");
}

std::shared_ptr<cxxime::Dict>& dict_slot_for(SharedResources& shared,
                                             cxxime::UserDictKind kind) {
    return kind == cxxime::UserDictKind::WUBI ? shared.wubi_dict : shared.dict;
}

const std::shared_ptr<cxxime::Dict>& dict_slot_for(const SharedResources& shared,
                                                   cxxime::UserDictKind kind) {
    return kind == cxxime::UserDictKind::WUBI ? shared.wubi_dict : shared.dict;
}

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

std::string manifest_role_path(const cxxime::DictionaryManifest& manifest,
                               const char* role) {
    const auto* file = manifest.find_role(role);
    return file ? file->absolute_path : std::string{};
}

bool load_dictionary_resources(const std::string& manifest_path, DictionaryResources& out) {
    cxxime::DictionaryManifest manifest;
    std::string manifest_error;
    if (!cxxime::load_dictionary_manifest(manifest_path, manifest, &manifest_error)) {
        CXXIME_LOG(L"SharedResources: manifest load FAILED: %S", manifest_error.c_str());
        return false;
    }
    if (!cxxime::validate_dictionary_manifest(manifest, &manifest_error)) {
        CXXIME_LOG(L"SharedResources: manifest validate FAILED: %S", manifest_error.c_str());
        return false;
    }

    std::string dict_path = manifest_role_path(manifest, "pinyin_dict");
    std::string dict_idx_path = manifest_role_path(manifest, "pinyin_idx");
    std::string topn_path = manifest_role_path(manifest, "pinyin_topn");
    std::string spellings_path = manifest_role_path(manifest, "pinyin_spellings");
    std::string wubi_dict_path = manifest_role_path(manifest, "wubi_dict");
    std::string wubi_idx_path = manifest_role_path(manifest, "wubi_idx");

    auto loaded_dict = std::make_shared<cxxime::Dict>();
    std::string user_dict_path = user_dict_path_for(cxxime::UserDictKind::PINYIN);
    if (!loaded_dict->open_bundle(dict_path, user_dict_path, dict_idx_path, topn_path)) {
        CXXIME_LOG(L"SharedResources: dict.open FAILED");
        return false;
    }
    loaded_dict->set_user_scoring_profile(cxxime::UserScoringProfile::kPinyin);

    auto loaded_spellings = std::make_shared<cxxime::SpellingsIndex>();
    std::shared_ptr<cxxime::Syllabifier> loaded_syllabifier;
    if (!spellings_path.empty()) {
        if (!loaded_spellings->load(spellings_path) || !loaded_spellings->has_spellings()) {
            CXXIME_LOG(L"SharedResources: spellings load FAILED");
            return false;
        }
        loaded_syllabifier = std::make_shared<cxxime::Syllabifier>(*loaded_spellings);
    }

    auto loaded_wubi_dict = std::make_shared<cxxime::Dict>();
    if (wubi_dict_path.empty() ||
        !loaded_wubi_dict->open_bundle(wubi_dict_path,
                                       user_dict_path_for(cxxime::UserDictKind::WUBI),
                                       wubi_idx_path, {})) {
        loaded_wubi_dict.reset();
        CXXIME_LOG(L"SharedResources: wubi dict not found, wubi mode disabled");
    } else {
        loaded_wubi_dict->set_user_scoring_profile(cxxime::UserScoringProfile::kWubi);
        CXXIME_LOG(L"SharedResources: wubi dict loaded");
    }

    out.dict = std::move(loaded_dict);
    out.wubi_dict = std::move(loaded_wubi_dict);
    out.spellings = std::move(loaded_spellings);
    out.syllabifier = std::move(loaded_syllabifier);
    out.dict_path = dict_path;
    out.wubi_dict_path = std::move(wubi_dict_path);
    out.manifest_path = manifest_path;
    return true;
}

std::shared_ptr<const cxxime::PunctMapping> load_punctuation_mapping(const std::string& path) {
    if (path.empty()) return nullptr;

    std::ifstream file(path);
    if (!file.is_open()) {
        CXXIME_LOG(L"SharedResources: punctuation file not found: %S", path.c_str());
        return nullptr;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        auto mapping = std::make_shared<cxxime::PunctMapping>();

        parse_punct_section(j, "half_shape", mapping->half_shape);
        parse_punct_section(j, "full_shape", mapping->full_shape);

        CXXIME_LOG(L"SharedResources: punctuation loaded (%zu half, %zu full)",
                   mapping->half_shape.size(), mapping->full_shape.size());
        return mapping;
    } catch (const nlohmann::json::exception& e) {
        CXXIME_LOG(L"SharedResources: punctuation parse error: %S", e.what());
        return nullptr;
    }
}

}  // anonymous namespace

bool SharedResources::load(const std::string& dict_path, const std::string& cfg_path) {
    std::string manifest_path = cxxime::dictionary_manifest_path_for_dict(dict_path);
    DictionaryResources dictionaries;
    if (!load_dictionary_resources(manifest_path, dictionaries))
        return false;

    auto loaded_config = std::make_shared<cxxime::Config>();
    std::string loaded_config_path;
    if (!cfg_path.empty()) {
        loaded_config_path = cfg_path;
        loaded_config->load(loaded_config_path);
        // Overlay user config from %USERPROFILE%\cxxime
        loaded_config->load(cxxime::user_data_path("default.json"));
        loaded_config->load_themes(cxxime::data_path("themes.json"));
        cxxime::set_diagnostics_config(loaded_config->diagnostics);
    }

    // Load punctuation mapping (non-fatal)
    std::string loaded_punct_path = cxxime::data_path("punctuation.json");
    auto loaded_punct_mapping = load_punctuation_mapping(loaded_punct_path);

    {
        std::lock_guard<std::mutex> lock(mutex);
        this->dict_path = std::move(dictionaries.dict_path);
        wubi_dict_path = std::move(dictionaries.wubi_dict_path);
        this->manifest_path = std::move(dictionaries.manifest_path);
        config_path = std::move(loaded_config_path);
        dict = std::move(dictionaries.dict);
        wubi_dict = std::move(dictionaries.wubi_dict);
        spellings = std::move(dictionaries.spellings);
        syllabifier = std::move(dictionaries.syllabifier);
        config = std::move(loaded_config);
        punct_mapping = std::move(loaded_punct_mapping);
        punct_path = punct_mapping ? std::move(loaded_punct_path) : std::string{};
    }

    return true;
}

SharedResourceSnapshot SharedResources::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    SharedResourceSnapshot s;
    s.dict = dict;
    s.wubi_dict = wubi_dict;
    s.spellings = spellings;
    s.syllabifier = syllabifier;
    s.config = config;
    s.punct_mapping = punct_mapping;
    return s;
}

std::shared_ptr<cxxime::Dict> SharedResources::dict_for_kind(cxxime::UserDictKind kind) const {
    std::lock_guard<std::mutex> lock(mutex);
    return dict_slot_for(*this, kind);
}

bool SharedResources::load_punctuation(const std::string& path) {
    if (path.empty()) return true;

    auto mapping = load_punctuation_mapping(path);
    if (!mapping)
        return false;

    {
        std::lock_guard<std::mutex> lock(mutex);
        punct_mapping = std::move(mapping);
        punct_path = path;
    }
    return true;
}

bool SharedResources::reload_dictionaries() {
    std::string current_manifest_path;
    std::shared_ptr<cxxime::Dict> old_dict;
    std::shared_ptr<cxxime::Dict> old_wubi_dict;
    {
        std::lock_guard<std::mutex> lock(mutex);
        current_manifest_path = manifest_path;
        old_dict = dict;
        old_wubi_dict = wubi_dict;
    }
    if (current_manifest_path.empty())
        return false;

    if (old_dict)
        old_dict->save_user_dict();
    if (old_wubi_dict)
        old_wubi_dict->save_user_dict();

    DictionaryResources dictionaries;
    if (!load_dictionary_resources(current_manifest_path, dictionaries))
        return false;

    {
        std::lock_guard<std::mutex> lock(mutex);
        dict = std::move(dictionaries.dict);
        wubi_dict = std::move(dictionaries.wubi_dict);
        spellings = std::move(dictionaries.spellings);
        syllabifier = std::move(dictionaries.syllabifier);
        dict_path = std::move(dictionaries.dict_path);
        wubi_dict_path = std::move(dictionaries.wubi_dict_path);
        manifest_path = std::move(dictionaries.manifest_path);
    }

    CXXIME_LOG(L"SharedResources: dictionaries reloaded generation=%S",
               current_manifest_path.c_str());
    return true;
}

bool SessionManager::initialize(const std::string& dict_path, const std::string& config_path) {
    return shared_.load(dict_path, config_path);
}

uint32_t SessionManager::create_session() {
    auto engine = std::make_unique<cxxime::Engine>();
    auto resources = shared_.snapshot();
    if (!resources.config || !resources.dict || !resources.spellings)
        return 0;

    if (!engine->initialize(*resources.dict, *resources.spellings,
            resources.syllabifier.get(), *resources.config))
        return 0;
    if (resources.wubi_dict && resources.wubi_dict->is_open()) {
        engine->set_wubi_dict(resources.wubi_dict.get());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = next_id_++;
    auto entry = std::make_shared<SessionEntry>();
    entry->engine = std::move(engine);
    entry->last_activity = std::chrono::steady_clock::now();
    entry->resources = std::move(resources);
    entry->ime_status.input_mode = static_cast<cxxime::InputMode>(entry->resources.config->input_mode);
    entry->engine->switch_mode(entry->ime_status.input_mode);
    entry->engine->set_fuzzy_enabled(entry->resources.config->fuzzy_pinyin);
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
    std::string current_config_path;
    std::string current_punct_path;
    {
        std::lock_guard<std::mutex> lock(mutex);
        current_config_path = config_path;
        current_punct_path = punct_path;
    }

    if (current_config_path.empty())
        return;
    CXXIME_LOG(L"SharedResources: reloading config from %S", current_config_path.c_str());
    auto cfg = std::make_shared<cxxime::Config>();
    cfg->load(current_config_path);
    cfg->load(cxxime::user_data_path("default.json"));
    cfg->load_themes(cxxime::data_path("themes.json"));
    cxxime::set_diagnostics_config(cfg->diagnostics);
    auto mapping = current_punct_path.empty()
        ? std::shared_ptr<const cxxime::PunctMapping>{}
        : load_punctuation_mapping(current_punct_path);

    {
        std::lock_guard<std::mutex> lock(mutex);
        config = std::move(cfg);
        if (mapping) {
            punct_mapping = std::move(mapping);
        }
    }
    CXXIME_LOG(L"SharedResources: config reloaded");
}

static void apply_resource_snapshot(SessionEntry& entry, const SharedResourceSnapshot& resources) {
    if (resources.dict && resources.spellings &&
        (resources.dict.get() != entry.resources.dict.get() ||
         resources.spellings.get() != entry.resources.spellings.get() ||
         resources.syllabifier.get() != entry.resources.syllabifier.get() ||
         resources.wubi_dict.get() != entry.resources.wubi_dict.get())) {
        auto old_mode = entry.ime_status.input_mode;
        entry.engine->rebind_shared_resources(*resources.dict, *resources.spellings,
            resources.syllabifier.get(),
            resources.wubi_dict ? resources.wubi_dict.get() : nullptr);
        entry.resources.dict = resources.dict;
        entry.resources.spellings = resources.spellings;
        entry.resources.syllabifier = resources.syllabifier;
        entry.resources.wubi_dict = resources.wubi_dict;
        entry.ime_status.input_mode = entry.engine->mode();
        if (entry.resources.config) {
            entry.engine->set_fuzzy_enabled(entry.resources.config->fuzzy_pinyin);
        }
        if (old_mode != entry.ime_status.input_mode)
            entry.ime_status.revision++;
    }
    entry.resources.punct_mapping = resources.punct_mapping;
}

bool SharedResources::reload_user_dict(cxxime::UserDictKind kind) {
    std::lock_guard<std::mutex> lock(mutex);
    auto dict = dict_slot_for(*this, kind);
    return dict && dict->load_user_dict(user_dict_path_for(kind));
}

cxxime::IPCStatus SharedResources::add_user_entry(cxxime::UserDictKind kind,
        const std::string& text,
        const std::string& code) {
    if (text.empty() || code.empty())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    std::lock_guard<std::mutex> lock(mutex);
    auto dict = dict_slot_for(*this, kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    dict->update_frequency(text, code);
    if (!dict->save_user_dict())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    return cxxime::IPCStatus::OK;
}

std::vector<cxxime::UserDictEntryInfo> SharedResources::query_user_entries(
        const std::string& query, cxxime::UserDictKind kind, size_t limit, size_t& total) {
    std::lock_guard<std::mutex> lock(mutex);
    auto dict = dict_slot_for(*this, kind);
    total = dict && dict->is_open() ? dict->user_entry_count() : 0;
    return dict && dict->is_open() ? dict->query_user_entries(query, limit)
                                   : std::vector<cxxime::UserDictEntryInfo>{};
}

cxxime::IPCStatus SharedResources::delete_user_entry(cxxime::UserDictKind kind,
        const std::string& text,
        const std::string& code) {
    if (text.empty())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    std::lock_guard<std::mutex> lock(mutex);
    auto dict = dict_slot_for(*this, kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    if (!dict->delete_user_entry(text, code))
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    if (!dict->save_user_dict())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SharedResources::replace_user_entry(cxxime::UserDictKind kind,
        const std::string& old_text,
        const std::string& old_code,
        const std::string& new_text,
        const std::string& new_code) {
    if (old_text.empty() || new_text.empty() || new_code.empty())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    std::lock_guard<std::mutex> lock(mutex);
    auto dict = dict_slot_for(*this, kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    if (!dict->replace_user_entry(old_text, old_code, new_text, new_code))
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    if (!dict->save_user_dict())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SharedResources::save_user_dict(cxxime::UserDictKind kind) {
    std::lock_guard<std::mutex> lock(mutex);
    auto dict = dict_slot_for(*this, kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    if (!dict->save_user_dict())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    return cxxime::IPCStatus::OK;
}

void SessionManager::reload_config() {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);

    shared_.reload_config();
    auto resources = shared_.snapshot();

    std::vector<std::shared_ptr<SessionEntry>> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries.reserve(sessions_.size());
        for (auto& item : sessions_) {
            entries.push_back(item.second);
        }
    }

    // Sync input_mode from config to all active sessions
    if (resources.config) {
        auto target = static_cast<cxxime::InputMode>(resources.config->input_mode);
        bool fuzzy = resources.config->fuzzy_pinyin;
        for (auto& entry : entries) {
            std::lock_guard<std::mutex> lock(entry->mutex);
            apply_resource_snapshot(*entry, resources);
            if (resources.config.get() != entry->resources.config.get()) {
                entry->engine->reload_config(*resources.config);
                entry->resources.config = resources.config;
            }
            if (entry->ime_status.input_mode != target) {
                entry->engine->switch_mode(target);
                entry->ime_status.input_mode = target;
                entry->ime_status.revision++;
            }
            entry->engine->set_fuzzy_enabled(fuzzy);
        }
    }
    CXXIME_LOG(L"SessionManager: config reloaded, %zu active sessions", entries.size());
}

cxxime::IPCStatus SessionManager::reload_dictionaries() {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);

    std::vector<std::shared_ptr<SessionEntry>> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries.reserve(sessions_.size());
        for (auto& item : sessions_) {
            entries.push_back(item.second);
        }
    }

    std::vector<std::unique_lock<std::mutex>> entry_locks;
    entry_locks.reserve(entries.size());
    for (auto& entry : entries) {
        entry_locks.emplace_back(entry->mutex);
    }

    if (!shared_.reload_dictionaries())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;

    auto resources = shared_.snapshot();
    for (auto& entry : entries) {
        apply_resource_snapshot(*entry, resources);
    }

    CXXIME_LOG(L"SessionManager: dictionaries reloaded, %zu active sessions", entries.size());
    return cxxime::IPCStatus::OK;
}

bool SessionManager::reload_punctuation(const std::string& path) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
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
    entry->engine->ascii_composer().set_ascii_mode(ascii_mode);
    entry->ime_status.revision++;
    return cxxime::IPCStatus::OK;
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::sync_caps_lock(uint32_t id, bool caps_lock) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    auto& s = *entry;
    bool old_caps = s.ime_status.caps_lock;
    bool old_chinese = s.ime_status.chinese_mode;
    s.engine->ascii_composer().sync_caps_lock(caps_lock, s.engine->context());
    s.ime_status.caps_lock = caps_lock;
    s.ime_status.chinese_mode = !s.engine->ascii_composer().is_ascii_mode();
    if (old_caps != s.ime_status.caps_lock || old_chinese != s.ime_status.chinese_mode)
        s.ime_status.revision++;
    return {cxxime::IPCStatus::OK, s.ime_status};
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
    auto resources = shared_.snapshot();
    apply_resource_snapshot(s, resources);
    bool trace_enabled = true;
    if (resources.config) {
        trace_enabled =
            resources.config->diagnostics.trace_mode != cxxime::DiagnosticTraceMode::kOff;
    }
    engine.set_trace_enabled(trace_enabled);

    // 0. Check config snapshot to detect hot reload.
    if (resources.config && resources.config.get() != s.resources.config.get()) {
        engine.reload_config(*resources.config);
        engine.set_fuzzy_enabled(resources.config->fuzzy_pinyin);
        s.resources.config = resources.config;
    }

    // 1. Sync CapsLock before OutputOptions derivation. On first activation,
    // TSF may not deliver a VK_CAPITAL event because CapsLock was already on.
    // The physical modifier bit on the first real key is still authoritative.
    bool is_caps_lock_key = event.keycode == VK_CAPITAL;
    bool old_caps = s.ime_status.caps_lock;
    bool old_ascii = !s.ime_status.chinese_mode;
    if (!is_caps_lock_key && old_caps != event.is_caps_lock()) {
        engine.ascii_composer().sync_caps_lock(event.is_caps_lock(), engine.context());
        s.ime_status.caps_lock = event.is_caps_lock();
        bool new_ascii = engine.ascii_composer().is_ascii_mode();
        s.ime_status.chinese_mode = !new_ascii;
        if (old_ascii != new_ascii) {
            s.ime_status.revision++;
        }
    }

    // 2. derive OutputOptions
    auto opts = cxxime::OutputOptions::from(s.ime_status);
    opts.punct_mapping = resources.punct_mapping.get();

    // 3. set trace
    engine.set_trace_session_id(id);

    // 4. call Engine
    auto result = engine.process_key(event, opts);

    // 5. sync ascii_mode -> ime_status.chinese_mode
    old_ascii = !s.ime_status.chinese_mode;
    bool new_ascii = engine.ascii_composer().is_ascii_mode();
    s.ime_status.chinese_mode = !new_ascii;
    bool caps_changed = false;
    if (is_caps_lock_key && s.ime_status.caps_lock != event.is_caps_lock()) {
        s.ime_status.caps_lock = event.is_caps_lock();
        caps_changed = true;
    }
    if (old_ascii != new_ascii || caps_changed) {
        s.ime_status.revision++;
    }

    // 6. populate return value
    //    Key: process COMMITTED first (take + clear context), THEN read composing.
    //    COMMITTED sets committed_text but does NOT clear pinyin_buffer,
    //    so is_composing() would return true if read before take.
    ProcessKeyResult ret;
    ret.status = cxxime::IPCStatus::OK;
    ret.result = result;

    // Handle toggle results and return updated ImeStatus.
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
        ret.commit_text = cxxime::OutputComposer::transform(raw, opts, source);
        ret.composing = false;
    } else if (result == cxxime::ProcessResult::TOGGLE_PUNCT
            || result == cxxime::ProcessResult::TOGGLE_SHAPE) {
        // Toggle results should not carry stale preedit. Clear the composition
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
    if (trace_enabled && engine.last_trace().should_log()) {
        engine.last_trace().log_unchecked();
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
        ret.commit_text = cxxime::OutputComposer::transform(raw, opts, source);
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
        ret.commit_text = cxxime::OutputComposer::transform(raw, opts, source);
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

cxxime::IPCStatus SessionManager::add_user_entry(uint32_t id, cxxime::UserDictKind kind,
                                                 const std::string& text, const std::string& code) {
    return shared_.add_user_entry(kind, text, code);
}

std::vector<cxxime::UserDictEntryInfo> SessionManager::query_user_entries(
    const std::string& query, cxxime::UserDictKind kind, size_t limit, size_t& total) {
    return shared_.query_user_entries(query, kind, limit, total);
}

cxxime::IPCStatus SessionManager::delete_user_entry(cxxime::UserDictKind kind,
                                                    const std::string& text,
                                                    const std::string& code) {
    return shared_.delete_user_entry(kind, text, code);
}

cxxime::IPCStatus SessionManager::replace_user_entry(cxxime::UserDictKind kind,
                                                     const std::string& old_text,
                                                     const std::string& old_code,
                                                     const std::string& new_text,
                                                     const std::string& new_code) {
    return shared_.replace_user_entry(kind, old_text, old_code, new_text, new_code);
}

cxxime::IPCStatus SessionManager::reload_user_dict(cxxime::UserDictKind kind) {
    return shared_.reload_user_dict(kind)
        ? cxxime::IPCStatus::OK
        : cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
}

cxxime::IPCStatus SessionManager::save_user_dict(cxxime::UserDictKind kind) {
    return shared_.save_user_dict(kind);
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
