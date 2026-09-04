// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager.h"

#include <algorithm>
#include <fstream>

#include <windows.h>

#include <json.hpp>

#include <cxxime/data_path.h>
#include <cxxime/diagnostics_config.h>
#include <cxxime/dictionary_manifest.h>
#include <cxxime/input_limits.h>
#include <cxxime/logging.h>
#include <cxxime/query_trace.h>
#include <cxxime/translator.h>
#include <cxxime/wubi_translator.h>

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

std::string candidate_preference_path_for(cxxime::UserDictKind kind) {
    return cxxime::user_data_path(kind == cxxime::UserDictKind::WUBI
                                  ? "learning_wubi.tsv"
                                  : "learning_pinyin.tsv");
}

std::string manual_candidate_order_path_for(cxxime::UserDictKind kind) {
    return cxxime::user_data_path(kind == cxxime::UserDictKind::WUBI
                                      ? "candidate_order_wubi.tsv"
                                      : "candidate_order_pinyin.tsv");
}

std::string disabled_system_lexicon_path_for(cxxime::UserDictKind kind) {
    return cxxime::user_data_path(kind == cxxime::UserDictKind::WUBI
                                      ? "disabled_wubi.tsv"
                                      : "disabled_pinyin.tsv");
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

bool same_visible_status(const cxxime::ImeStatus& a, const cxxime::ImeStatus& b) {
    return a.flags == b.flags && a.input_mode == b.input_mode;
}

cxxime::InputMode next_input_mode(cxxime::InputMode mode) {
    switch (mode) {
    case cxxime::InputMode::PINYIN:
        return cxxime::InputMode::WUBI;
    case cxxime::InputMode::WUBI:
        return cxxime::InputMode::MIXED;
    case cxxime::InputMode::MIXED:
    default:
        return cxxime::InputMode::PINYIN;
    }
}

bool load_dictionary_resources(const std::string& manifest_path, DictionaryResources& out) {
    static const std::vector<std::string> kRuntimeDictionaryRoles = {
        "pinyin_dict",
        "pinyin_idx",
        "pinyin_spellings",
        "pinyin_topn",
        "wubi_dict",
        "wubi_prefix_index",
    };
    cxxime::DictionaryManifest manifest;
    std::string manifest_error;
    if (!cxxime::load_dictionary_manifest(manifest_path, manifest, &manifest_error)) {
        CXXIME_LOG(L"SharedResources: manifest load FAILED: %S", manifest_error.c_str());
        return false;
    }
    if (!cxxime::validate_dictionary_manifest_files(
            manifest, kRuntimeDictionaryRoles, &manifest_error)) {
        CXXIME_LOG(L"SharedResources: manifest validate FAILED: %S", manifest_error.c_str());
        return false;
    }

    std::string dict_path = manifest_role_path(manifest, "pinyin_dict");
    std::string dict_idx_path = manifest_role_path(manifest, "pinyin_idx");
    std::string topn_path = manifest_role_path(manifest, "pinyin_topn");
    std::string spellings_path = manifest_role_path(manifest, "pinyin_spellings");
    std::string wubi_dict_path = manifest_role_path(manifest, "wubi_dict");
    std::string wubi_prefix_index_path =
        manifest_role_path(manifest, "wubi_prefix_index");

    auto loaded_dict = std::make_shared<cxxime::Dict>();
    std::string user_dict_path = user_dict_path_for(cxxime::UserDictKind::PINYIN);
    if (!loaded_dict->open_bundle(dict_path, user_dict_path, dict_idx_path, topn_path)) {
        CXXIME_LOG(L"SharedResources: dict.open FAILED");
        return false;
    }
    if (!loaded_dict->load_candidate_preferences(
            candidate_preference_path_for(cxxime::UserDictKind::PINYIN))) {
        CXXIME_LOG(L"SharedResources: pinyin candidate preferences load FAILED");
        return false;
    }
    if (!loaded_dict->load_manual_candidate_order(
            manual_candidate_order_path_for(cxxime::UserDictKind::PINYIN),
            cxxime::kMaxInputCodeLength)) {
        CXXIME_LOG(L"SharedResources: pinyin manual candidate order load FAILED");
        return false;
    }
    if (!loaded_dict->load_disabled_system_entries(
            disabled_system_lexicon_path_for(cxxime::UserDictKind::PINYIN))) {
        CXXIME_LOG(L"SharedResources: disabled pinyin entries load FAILED");
        return false;
    }

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
    if (!loaded_wubi_dict->open_wubi_bundle(
            wubi_dict_path, user_dict_path_for(cxxime::UserDictKind::WUBI),
            wubi_prefix_index_path)) {
        CXXIME_LOG(L"SharedResources: required Wubi resources load FAILED");
        return false;
    }
    if (!loaded_wubi_dict->load_candidate_preferences(
            candidate_preference_path_for(cxxime::UserDictKind::WUBI))) {
        CXXIME_LOG(L"SharedResources: Wubi candidate preferences load FAILED");
        return false;
    }
    if (!loaded_wubi_dict->load_manual_candidate_order(
            manual_candidate_order_path_for(cxxime::UserDictKind::WUBI),
            cxxime::kMaxWubiCodeLength)) {
        CXXIME_LOG(L"SharedResources: Wubi manual candidate order load FAILED");
        return false;
    }
    if (!loaded_wubi_dict->load_disabled_system_entries(
            disabled_system_lexicon_path_for(cxxime::UserDictKind::WUBI))) {
        CXXIME_LOG(L"SharedResources: disabled Wubi entries load FAILED");
        return false;
    }
    CXXIME_LOG(L"SharedResources: wubi dict loaded");

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

        CXXIME_LOG(L"SharedResources: punctuation loaded (%zu entries)",
                   mapping->half_shape.size());
        return mapping;
    } catch (const nlohmann::json::exception& e) {
        CXXIME_LOG(L"SharedResources: punctuation parse error: %S", e.what());
        return nullptr;
    }
}

std::shared_ptr<const cxxime::SymbolTable> load_symbol_table(const std::string& path) {
    auto table = std::make_shared<cxxime::SymbolTable>();
    if (!table->load(path)) {
        CXXIME_LOG(L"SharedResources: symbol table not found or invalid: %S", path.c_str());
        return nullptr;
    }
    CXXIME_LOG(L"SharedResources: symbol table loaded");
    return table;
}

}  // anonymous namespace

bool SharedResources::load(const std::string& dict_path,
                           const std::shared_ptr<const cxxime::Config>& loaded_config) {
    std::string manifest_path = cxxime::dictionary_manifest_path_for_dict(dict_path);
    DictionaryResources dictionaries;
    if (!load_dictionary_resources(manifest_path, dictionaries))
        return false;
    if (!loaded_config) {
        return false;
    }

    // Load punctuation mapping (non-fatal)
    std::string loaded_punct_path = cxxime::data_path("punctuation.json");
    auto loaded_punct_mapping = load_punctuation_mapping(loaded_punct_path);
    auto loaded_symbol_table = load_symbol_table(cxxime::data_path("symbols.json"));

    {
        std::lock_guard<std::mutex> lock(mutex);
        this->dict_path = std::move(dictionaries.dict_path);
        wubi_dict_path = std::move(dictionaries.wubi_dict_path);
        this->manifest_path = std::move(dictionaries.manifest_path);
        dict = std::move(dictionaries.dict);
        wubi_dict = std::move(dictionaries.wubi_dict);
        spellings = std::move(dictionaries.spellings);
        syllabifier = std::move(dictionaries.syllabifier);
        symbol_table = std::move(loaded_symbol_table);
        config = loaded_config;
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
    s.symbol_table = symbol_table;
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

    if ((old_dict && (!old_dict->save_user_dict() ||
                      !old_dict->save_candidate_preferences() ||
                      !old_dict->save_disabled_system_entries())) ||
        (old_wubi_dict && (!old_wubi_dict->save_user_dict() ||
                           !old_wubi_dict->save_candidate_preferences() ||
                           !old_wubi_dict->save_disabled_system_entries()))) {
        CXXIME_LOG(L"%s", L"SharedResources: user data flush before reload FAILED");
        return false;
    }

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

bool SessionManager::initialize(const std::string& dict_path,
                                const std::shared_ptr<const cxxime::Config>& config) {
    if (!shared_.load(dict_path, config)) {
        return false;
    }
    cxxime::set_diagnostics_config(config->diagnostics);
    cxxime::QueryTrace::set_enabled(config->diagnostics.trace_mode !=
                                    cxxime::DiagnosticTraceMode::kOff);
    reset_global_state(shared_.snapshot());
    return true;
}

void SessionManager::reset_global_state(const SharedResourceSnapshot& resources) {
    GlobalVisibleState state;
    if (resources.config) {
        state.input_mode = static_cast<cxxime::InputMode>(resources.config->input_mode);
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    global_state_ = state;
}

SessionManager::GlobalVisibleState SessionManager::snapshot_global_state() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return global_state_;
}

void SessionManager::commit_global_state(GlobalVisibleState next) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    global_state_ = next;
}

void SessionManager::align_session_to_global(SessionEntry& entry) {
    GlobalVisibleState state = snapshot_global_state();
    const cxxime::ImeStatus previous = entry.ime_status;

    if (entry.ime_status.input_mode != state.input_mode) {
        entry.engine->switch_mode(state.input_mode);
    }
    auto& ascii_composer = entry.engine->ascii_composer();
    if (!ascii_composer.is_temporary_ascii() || !entry.base_chinese_mode) {
        ascii_composer.set_ascii_mode(!entry.base_chinese_mode);
    }
    entry.engine->ascii_composer().sync_caps_lock(state.caps_lock,
                                                  entry.engine->context());
    entry.ime_status.set_chinese_mode(state.caps_lock ? false : entry.base_chinese_mode);
    entry.ime_status.set_caps_lock(state.caps_lock);
    entry.ime_status.set_full_shape(entry.full_shape);
    entry.ime_status.set_chinese_punct(entry.chinese_punct);
    entry.ime_status.input_mode = entry.engine->mode();
    entry.ime_status.revision = same_visible_status(previous, entry.ime_status)
        ? previous.revision
        : previous.revision + 1;
    if (entry.ime_status.input_mode != state.input_mode) {
        state.input_mode = entry.ime_status.input_mode;
        commit_global_state(state);
    }
}

uint32_t SessionManager::create_session() {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    auto engine = std::make_unique<cxxime::Engine>();
    auto resources = shared_.snapshot();
    if (!resources.config || !resources.dict || !resources.spellings)
        return 0;

    if (!engine->initialize(*resources.dict, *resources.spellings,
                            resources.syllabifier.get(), *resources.config,
                            resources.symbol_table.get()))
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
    entry->full_shape = entry->resources.config->initial_full_shape;
    entry->chinese_punct = entry->resources.config->initial_chinese_punct;
    entry->ime_status.set_full_shape(entry->full_shape);
    entry->ime_status.set_chinese_punct(entry->chinese_punct);
    entry->engine->set_fuzzy_enabled(entry->resources.config->fuzzy_pinyin);
    align_session_to_global(*entry);
    sessions_[id] = entry;
    return id;
}

void SessionManager::destroy_session(uint32_t id) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    std::shared_ptr<SessionEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = sessions_.find(id);
        if (found == sessions_.end()) {
            return;
        }
        entry = found->second;
    }
    {
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->closing = true;
    }
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
            std::lock_guard<std::mutex> entry_lock(it->second->mutex);
            it->second->closing = true;
            it = sessions_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

void SharedResources::replace_config(const std::shared_ptr<const cxxime::Config>& next_config) {
    std::lock_guard<std::mutex> lock(mutex);
    config = next_config;
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

cxxime::IPCStatus SharedResources::add_user_entry(cxxime::UserDictKind kind,
                                                  const std::string& text,
                                                  const std::string& code) {
    if (text.empty() || code.empty())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    if (!dict->add_user_entry_and_save(text, code))
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SharedResources::import_user_dict(cxxime::UserDictKind kind,
                                                    const std::string& source_path) {
    if (source_path.empty()) {
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open()) {
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    }
    if (!dict->import_user_dict(source_path)) {
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    return cxxime::IPCStatus::OK;
}

cxxime::UserDictQueryResult SharedResources::query_user_entries(
    const std::string& query, cxxime::UserDictKind kind, size_t offset, size_t limit) {
    return query_lexicon_entries(cxxime::LexiconResource::kUserLexicon, query, kind, offset,
                                 limit);
}

cxxime::UserDictQueryResult SharedResources::query_lexicon_entries(
    cxxime::LexiconResource resource, const std::string& query, cxxime::UserDictKind kind,
    size_t offset, size_t limit, bool exact_text) {
    auto dict = dict_for_kind(kind);
    cxxime::UserDictQueryResult result;
    result.offset = offset;
    if (dict && dict->is_open()) {
        if (resource == cxxime::LexiconResource::kCandidatePreference) {
            result.resource_total = dict->candidate_preference_count();
            result.entries =
                dict->query_candidate_preferences(query, offset, limit, &result.match_total);
        } else if (resource == cxxime::LexiconResource::kDisabledSystemLexicon) {
            result.resource_total = dict->disabled_system_entry_count();
            result.entries =
                dict->query_disabled_system_entries(query, offset, limit, &result.match_total);
        } else {
            result.resource_total = dict->user_entry_count();
            result.entries =
                dict->query_user_entries(query, offset, limit, &result.match_total, exact_text);
        }
        result.has_more = offset < result.match_total &&
                          result.entries.size() < result.match_total - offset;
    }
    return result;
}

cxxime::UserDictQueryResult SharedResources::query_disabled_system_entry_status(
    cxxime::UserDictKind kind, const std::vector<std::string>& texts) {
    std::lock_guard<std::mutex> lock(mutex);
    cxxime::UserDictQueryResult result;
    const auto& dictionary = dict_slot_for(*this, kind);
    if (!dictionary) {
        return result;
    }
    result.resource_total = dictionary->disabled_system_entry_count();
    result.offset = 0;
    result.entries.reserve(texts.size());
    for (const auto& text : texts) {
        if (!dictionary->is_system_entry_disabled(text)) {
            continue;
        }
        cxxime::UserDictEntryInfo entry;
        entry.text = text;
        result.entries.push_back(std::move(entry));
    }
    result.match_total = result.entries.size();
    return result;
}

cxxime::IPCStatus SharedResources::disable_system_entry(cxxime::UserDictKind kind,
                                                       const std::string& text) {
    if (text.empty()) {
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open()) {
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    }
    if (!dict->disable_system_entry_and_save(text)) {
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SharedResources::restore_system_entry(cxxime::UserDictKind kind,
                                                       const std::string& text) {
    if (text.empty()) {
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open()) {
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    }
    if (!dict->restore_system_entry_and_save(text)) {
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SharedResources::delete_candidate_preferences(
    cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries) {
    if (entries.empty())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    return dict->delete_candidate_preferences_and_save(entries)
        ? cxxime::IPCStatus::OK
        : cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
}

cxxime::IPCStatus SharedResources::clear_candidate_preferences(cxxime::UserDictKind kind) {
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    return dict->clear_candidate_preferences_and_save()
        ? cxxime::IPCStatus::OK
        : cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
}

cxxime::IPCStatus SharedResources::save_candidate_preferences(cxxime::UserDictKind kind) {
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    return dict->save_candidate_preferences()
        ? cxxime::IPCStatus::OK
        : cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
}

cxxime::CandidateOrderQueryResult SharedResources::query_candidate_order(
    cxxime::UserDictKind kind, const std::string& code, std::size_t limit) {
    cxxime::CandidateOrderQueryResult result;
    result.input_code = code;
    auto dictionary = dict_for_kind(kind);
    if (!dictionary || !dictionary->is_open() || limit == 0) {
        return result;
    }

    cxxime::CandidatePage page;
    const auto resources = snapshot();
    const bool learning_enabled = resources.config && resources.config->candidate_learning;
    if (kind == cxxime::UserDictKind::WUBI) {
        cxxime::WubiTranslator translator;
        translator.set_dict(dictionary.get());
        translator.set_candidate_learning_enabled(learning_enabled);
        page = translator.translate_page(code, 0, static_cast<int>(limit));
    } else {
        cxxime::PinyinTranslator translator;
        translator.set_dict(dictionary.get());
        translator.set_syllabifier(resources.syllabifier.get());
        translator.set_short_cache(&dictionary->short_cache());
        translator.set_candidate_learning_enabled(learning_enabled);
        page = translator.translate_page(code, 0, static_cast<int>(limit));
    }

    result.version = dictionary->manual_candidate_order_version();
    result.manual_entries = dictionary->manual_candidate_order(code);
    result.has_more = page.total_count > static_cast<int>(page.candidates.size());
    result.entries.reserve(page.candidates.size());
    const auto source = kind == cxxime::UserDictKind::WUBI ? cxxime::CandidateSource::kWubi
                                                           : cxxime::CandidateSource::kPinyin;
    for (const auto& candidate : page.candidates) {
        cxxime::CandidateOrderEntryInfo entry;
        entry.text = candidate.text;
        entry.code = candidate.code;
        entry.syllables = candidate.syllables;
        entry.available = dictionary->can_resolve_manual_candidate(
            {candidate.text, candidate.code, candidate.syllables}, source);
        if (dictionary->has_manual_candidate_order(code, candidate.text, candidate.code,
                                                   candidate.syllables)) {
            entry.reason = cxxime::CandidateOrderReason::kManual;
        } else if (learning_enabled &&
                   dictionary->has_candidate_preference(candidate.text, code)) {
            entry.reason = cxxime::CandidateOrderReason::kLearned;
        } else if (candidate.origin == cxxime::CandidateOrigin::kUser) {
            entry.reason = cxxime::CandidateOrderReason::kUserLexicon;
        }
        result.entries.push_back(std::move(entry));
    }
    for (const auto& manual : result.manual_entries) {
        const bool present =
            std::any_of(result.entries.begin(), result.entries.end(), [&](const auto& entry) {
                return entry.text == manual.text && entry.code == manual.code &&
                       entry.syllables == manual.syllables;
            });
        if (!present) {
            cxxime::CandidateOrderEntryInfo entry;
            entry.text = manual.text;
            entry.code = manual.code;
            entry.syllables = manual.syllables;
            entry.reason = cxxime::CandidateOrderReason::kManual;
            entry.available = dictionary->can_resolve_manual_candidate(manual, source);
            result.entries.push_back(std::move(entry));
        }
    }
    return result;
}

cxxime::IPCStatus SharedResources::replace_candidate_order(
    cxxime::UserDictKind kind, const std::string& code,
    const std::vector<cxxime::ManualCandidateOrderEntry>& entries, std::uint64_t expected_version,
    bool* version_conflict) {
    auto dictionary = dict_for_kind(kind);
    if (!dictionary || !dictionary->is_open()) {
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    }
    const auto source = kind == cxxime::UserDictKind::WUBI ? cxxime::CandidateSource::kWubi
                                                           : cxxime::CandidateSource::kPinyin;
    const auto previous_order = dictionary->manual_candidate_order(code);
    if (std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
            if (dictionary->can_resolve_manual_candidate(entry, source)) {
                return false;
            }
            return std::none_of(
                previous_order.begin(), previous_order.end(), [&](const auto& previous) {
                    return previous.text == entry.text && previous.code == entry.code &&
                           previous.syllables == entry.syllables;
                });
        })) {
        if (version_conflict) {
            *version_conflict = false;
        }
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    return dictionary->replace_manual_candidate_order_if_version(code, entries, expected_version,
                                                                 version_conflict)
               ? cxxime::IPCStatus::OK
               : cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
}

cxxime::IPCStatus SharedResources::clear_candidate_order(cxxime::UserDictKind kind,
                                                         const std::string& code,
                                                         std::uint64_t expected_version,
                                                         bool* version_conflict) {
    auto dictionary = dict_for_kind(kind);
    if (!dictionary || !dictionary->is_open()) {
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    }
    const auto previous_order = dictionary->manual_candidate_order(code);
    if (!dictionary->replace_manual_candidate_order_if_version(code, {}, expected_version,
                                                               version_conflict)) {
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    }
    if (dictionary->clear_candidate_preferences_for_code_and_save(code)) {
        return cxxime::IPCStatus::OK;
    }
    dictionary->replace_manual_candidate_order_and_save(code, previous_order);
    return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
}

cxxime::IPCStatus SharedResources::delete_user_entries(
    cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries) {
    if (entries.empty())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    if (!dict->delete_user_entries_and_save(entries))
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
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    if (!dict->replace_user_entry_and_save(old_text, old_code, new_text, new_code))
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SharedResources::save_user_dict(cxxime::UserDictKind kind) {
    auto dict = dict_for_kind(kind);
    if (!dict || !dict->is_open())
        return cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
    if (!dict->save_user_dict())
        return cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
    return cxxime::IPCStatus::OK;
}

bool SharedResources::save_candidate_preferences(bool force) {
    std::shared_ptr<cxxime::Dict> pinyin;
    std::shared_ptr<cxxime::Dict> wubi;
    {
        std::lock_guard<std::mutex> lock(mutex);
        pinyin = dict;
        wubi = wubi_dict;
    }
    constexpr auto kSaveDelay = std::chrono::milliseconds(1500);
    const bool pinyin_saved = !pinyin || (force ? pinyin->save_candidate_preferences()
                                                : pinyin->save_candidate_preferences_if_due(
                                                      kSaveDelay));
    const bool wubi_saved = !wubi || (force ? wubi->save_candidate_preferences()
                                            : wubi->save_candidate_preferences_if_due(
                                                  kSaveDelay));
    return pinyin_saved && wubi_saved;
}

void SessionManager::apply_config(const std::shared_ptr<const cxxime::Config>& config) {
    if (!config) {
        return;
    }
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    auto previous_resources = shared_.snapshot();
    bool input_mode_changed = !previous_resources.config ||
                              previous_resources.config->input_mode != config->input_mode;
    shared_.replace_config(config);
    cxxime::set_diagnostics_config(config->diagnostics);
    cxxime::QueryTrace::set_enabled(config->diagnostics.trace_mode !=
                                    cxxime::DiagnosticTraceMode::kOff);
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
        bool fuzzy = resources.config->fuzzy_pinyin;
        if (input_mode_changed) {
            GlobalVisibleState state = snapshot_global_state();
            state.input_mode = static_cast<cxxime::InputMode>(resources.config->input_mode);
            commit_global_state(state);
        }
        for (auto& entry : entries) {
            std::lock_guard<std::mutex> lock(entry->mutex);
            apply_resource_snapshot(*entry, resources);
            if (resources.config.get() != entry->resources.config.get()) {
                entry->engine->reload_config(*resources.config);
                entry->resources.config = resources.config;
            }
            entry->engine->set_fuzzy_enabled(fuzzy);
            align_session_to_global(*entry);
        }
    }
    CXXIME_LOG(L"SessionManager: config applied, %zu active sessions", entries.size());
}

void SessionManager::set_config_patch_handler(ConfigPatchHandler handler) {
    config_patch_handler_ = std::move(handler);
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
        align_session_to_global(*entry);
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
    align_session_to_global(*entry);
    return {cxxime::IPCStatus::OK, entry->ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::toggle_chinese(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->base_chinese_mode = !entry->base_chinese_mode;
    align_session_to_global(*entry);
    return {cxxime::IPCStatus::OK, entry->ime_status};
}

ProcessKeyResult SessionManager::set_chinese_mode(uint32_t id, bool chinese_mode) {
    auto entry = lookup_session(id);
    if (!entry) {
        ProcessKeyResult error;
        error.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
        return error;
    }

    std::lock_guard<std::mutex> lock(entry->mutex);
    ProcessKeyResult result;
    result.status = cxxime::IPCStatus::OK;
    result.result = cxxime::ProcessResult::ACCEPTED;

    const bool mode_changed = entry->base_chinese_mode != chinese_mode;
    if (mode_changed && entry->engine->context().is_composing()) {
        result.commit_text = entry->engine->commit_raw_composition();
        result.result = cxxime::ProcessResult::COMMITTED;
    }

    entry->base_chinese_mode = chinese_mode;
    align_session_to_global(*entry);
    result.composing = entry->engine->context().is_composing();
    result.ime_status = entry->ime_status;
    return result;
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::toggle_shape(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->full_shape = !entry->full_shape;
    align_session_to_global(*entry);
    return {cxxime::IPCStatus::OK, entry->ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::toggle_punct(uint32_t id) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->chinese_punct = !entry->chinese_punct;
    align_session_to_global(*entry);
    return {cxxime::IPCStatus::OK, entry->ime_status};
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::switch_input_mode(uint32_t id, cxxime::InputMode mode) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    align_session_to_global(*entry);
    entry->engine->switch_mode(mode);
    GlobalVisibleState state = snapshot_global_state();
    state.input_mode = entry->engine->mode();
    commit_global_state(state);
    align_session_to_global(*entry);
    persist_input_mode(entry->ime_status.input_mode);
    return {cxxime::IPCStatus::OK, entry->ime_status};
}

cxxime::IPCStatus SessionManager::sync_ascii_mode(uint32_t id, bool ascii_mode) {
    auto entry = lookup_session(id);
    if (!entry) return cxxime::IPCStatus::ERR_INVALID_SESSION;
    std::lock_guard<std::mutex> lock(entry->mutex);
    entry->base_chinese_mode = !ascii_mode;
    align_session_to_global(*entry);
    return cxxime::IPCStatus::OK;
}

std::pair<cxxime::IPCStatus, cxxime::ImeStatus> SessionManager::sync_caps_lock(uint32_t id, bool caps_lock) {
    auto entry = lookup_session(id);
    if (!entry) return {cxxime::IPCStatus::ERR_INVALID_SESSION, {}};
    std::lock_guard<std::mutex> lock(entry->mutex);
    GlobalVisibleState state = snapshot_global_state();
    state.caps_lock = caps_lock;
    commit_global_state(state);
    align_session_to_global(*entry);
    return {cxxime::IPCStatus::OK, entry->ime_status};
}

ProcessKeyResult SessionManager::process_key(uint32_t id, const cxxime::KeyEvent& event,
                                             uint32_t visible_candidate_count) {
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
    if (entry->closing) {
        ProcessKeyResult err;
        err.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
        return err;
    }
    auto& s = *entry;
    auto& engine = *s.engine;
    auto resources = shared_.snapshot();
    apply_resource_snapshot(s, resources);
    bool trace_enabled = false;
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
    align_session_to_global(s);

    // 1. Sync CapsLock before OutputOptions derivation. On first activation,
    // TSF may not deliver a VK_CAPITAL event because CapsLock was already on.
    // The physical modifier bit on the first real key is still authoritative.
    bool is_caps_lock_key = event.keycode == VK_CAPITAL;
    if (!is_caps_lock_key && s.ime_status.caps_lock() != event.is_caps_lock()) {
        GlobalVisibleState state = snapshot_global_state();
        state.caps_lock = event.is_caps_lock();
        commit_global_state(state);
        align_session_to_global(s);
    }

    // 2. derive OutputOptions
    auto opts = cxxime::OutputOptions::from(s.ime_status);
    opts.punct_mapping = resources.punct_mapping.get();

    // 3. set trace
    engine.set_trace_session_id(id);

    // 4. call Engine
    auto result = engine.process_key(event, opts, static_cast<int>(visible_candidate_count));

    // 5. Publish shared mode changes and retain this session's base language mode.
    const bool temporary_ascii = engine.ascii_composer().is_temporary_ascii();
    bool new_ascii = engine.ascii_composer().is_ascii_mode();
    GlobalVisibleState state = snapshot_global_state();
    bool shared_state_changed = false;
    if (is_caps_lock_key && !event.is_key_up) {
        state.caps_lock = event.is_caps_lock();
        shared_state_changed = true;
    }
    if (!state.caps_lock && !temporary_ascii) {
        s.base_chinese_mode = !new_ascii;
    }

    // 6. Populate the return value. A Wubi fifth-key commit can start the next
    //    composition in the same engine step, so consume the commit before
    //    publishing the remaining composition state.
    ProcessKeyResult ret;
    ret.status = cxxime::IPCStatus::OK;
    ret.result = result;

    // Handle toggle results and return updated ImeStatus.
    if (result == cxxime::ProcessResult::TOGGLE_SHAPE) {
        s.full_shape = !s.full_shape;
    } else if (result == cxxime::ProcessResult::TOGGLE_PUNCT) {
        // In English mode, Ctrl+. also switches to Chinese mode
        if (!state.caps_lock && !s.base_chinese_mode) {
            s.base_chinese_mode = true;
            engine.ascii_composer().set_ascii_mode(false);
        }
        s.chinese_punct = !s.chinese_punct;
    } else if (result == cxxime::ProcessResult::SWITCH_INPUT_MODE) {
        engine.switch_mode(next_input_mode(engine.mode()));
        state.input_mode = engine.mode();
        shared_state_changed = true;
    }

    if (shared_state_changed) {
        commit_global_state(state);
    }
    align_session_to_global(s);
    ret.ime_status = s.ime_status;
    if (result == cxxime::ProcessResult::SWITCH_INPUT_MODE) {
        persist_input_mode(ret.ime_status.input_mode);
    }

    if (result == cxxime::ProcessResult::COMMITTED) {
        auto [raw, source] = engine.take_commit_text_with_source();
        ret.commit_text = cxxime::OutputComposer::transform(raw, opts, source);
        ret.composing = engine.context().is_composing();
    } else if (result == cxxime::ProcessResult::TOGGLE_PUNCT
            || result == cxxime::ProcessResult::TOGGLE_SHAPE) {
        ret.composing = engine.context().is_composing();
    } else if (result == cxxime::ProcessResult::SWITCH_INPUT_MODE) {
        ret.composing = false;
    } else {
        ret.composing = engine.context().is_composing();
    }
    if (ret.composing) {
        const cxxime::CompositionPresentation presentation =
            cxxime::derive_composition_presentation(engine.context().composition());
        ret.preedit = presentation.logical_preedit;
        ret.preedit_cursor = presentation.cursor_bytes;
        ret.candidates = engine.context().candidate_page();
    }

    // trace log
    if (trace_enabled && engine.last_trace().should_log()) {
        engine.last_trace().log_unchecked();
    }

    return ret;
}

cxxime::CandidatePage SessionManager::search_candidates(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const auto resources = shared_.snapshot();
    if (!resources.dict || !resources.spellings || !resources.config) {
        return {};
    }

    const auto visible_state = snapshot_global_state();
    cxxime::Engine search_engine;
    if (!search_engine.initialize(*resources.dict, *resources.spellings,
                                  resources.syllabifier.get(), *resources.config,
                                  resources.symbol_table.get())) {
        return {};
    }
    search_engine.set_wubi_dict(resources.wubi_dict.get());
    search_engine.switch_mode(visible_state.input_mode);
    cxxime::CandidatePage page = search_engine.translate_for_search(input, 10);
    std::vector<cxxime::Candidate> filtered;
    filtered.reserve(page.candidates.size());
    for (auto& candidate : page.candidates) {
        if (candidate.origin == cxxime::CandidateOrigin::kComposed || candidate.text.empty()) {
            continue;
        }

        bool redundant = false;
        for (auto it = filtered.begin(); it != filtered.end();) {
            const bool existing_prefix = candidate.text.size() >= it->text.size() &&
                candidate.text.compare(0, it->text.size(), it->text) == 0;
            const bool candidate_prefix = it->text.size() >= candidate.text.size() &&
                it->text.compare(0, candidate.text.size(), candidate.text) == 0;
            if (!existing_prefix && !candidate_prefix) {
                ++it;
                continue;
            }
            if (it->text.size() <= candidate.text.size()) {
                redundant = true;
                break;
            }
            it = filtered.erase(it);
        }
        if (!redundant) {
            filtered.push_back(std::move(candidate));
        }
    }
    page.candidates = std::move(filtered);
    page.total_count = static_cast<int>(page.candidates.size());
    page.page_index = 0;
    page.page_offset = 0;
    page.page_size = 10;
    page.highlighted = page.candidates.empty() ? -1 : 0;
    return page;
}

bool SessionManager::record_search_result(const std::string& input,
                                          const std::string& result) {
    if (input.empty() || result.empty()) {
        return false;
    }
    const auto resources = shared_.snapshot();
    if (!resources.dict || !resources.spellings || !resources.config) {
        return false;
    }

    const auto visible_state = snapshot_global_state();
    cxxime::Engine search_engine;
    if (!search_engine.initialize(*resources.dict, *resources.spellings,
                                  resources.syllabifier.get(), *resources.config,
                                  resources.symbol_table.get())) {
        return false;
    }
    search_engine.set_wubi_dict(resources.wubi_dict.get());
    search_engine.switch_mode(visible_state.input_mode);
    return search_engine.record_search_result(input, result);
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
    if (entry->closing) {
        ProcessKeyResult err;
        err.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
        return err;
    }
    auto& s = *entry;
    align_session_to_global(s);
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
    align_session_to_global(s);
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
    return cxxime::IPCStatus::OK;
}

cxxime::IPCStatus SessionManager::add_user_entry(cxxime::UserDictKind kind,
                                                 const std::string& text,
                                                 const std::string& code) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.add_user_entry(kind, text, code);
}

cxxime::UserDictQueryResult SessionManager::query_user_entries(
    const std::string& query, cxxime::UserDictKind kind, size_t offset, size_t limit) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.query_user_entries(query, kind, offset, limit);
}

cxxime::UserDictQueryResult SessionManager::query_lexicon_entries(
    cxxime::LexiconResource resource, const std::string& query, cxxime::UserDictKind kind,
    size_t offset, size_t limit, bool exact_text) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.query_lexicon_entries(resource, query, kind, offset, limit, exact_text);
}

cxxime::UserDictQueryResult SessionManager::query_disabled_system_entry_status(
    cxxime::UserDictKind kind, const std::vector<std::string>& texts) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.query_disabled_system_entry_status(kind, texts);
}

cxxime::IPCStatus SessionManager::delete_user_entries(
    cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.delete_user_entries(kind, entries);
}

cxxime::IPCStatus SessionManager::replace_user_entry(cxxime::UserDictKind kind,
                                                     const std::string& old_text,
                                                     const std::string& old_code,
                                                     const std::string& new_text,
                                                     const std::string& new_code) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.replace_user_entry(kind, old_text, old_code, new_text, new_code);
}

cxxime::IPCStatus SessionManager::import_user_dict(cxxime::UserDictKind kind,
                                                   const std::string& source_path) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.import_user_dict(kind, source_path);
}

cxxime::IPCStatus SessionManager::save_user_dict(cxxime::UserDictKind kind) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.save_user_dict(kind);
}

cxxime::IPCStatus SessionManager::disable_system_entry(cxxime::UserDictKind kind,
                                                       const std::string& text) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.disable_system_entry(kind, text);
}

cxxime::IPCStatus SessionManager::restore_system_entry(cxxime::UserDictKind kind,
                                                       const std::string& text) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.restore_system_entry(kind, text);
}

cxxime::IPCStatus SessionManager::delete_candidate_preferences(
    cxxime::UserDictKind kind, const std::vector<cxxime::LexiconEntryKey>& entries) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.delete_candidate_preferences(kind, entries);
}

cxxime::IPCStatus SessionManager::clear_candidate_preferences(cxxime::UserDictKind kind) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.clear_candidate_preferences(kind);
}

cxxime::IPCStatus SessionManager::save_candidate_preferences(cxxime::UserDictKind kind) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.save_candidate_preferences(kind);
}

cxxime::CandidateOrderQueryResult SessionManager::query_candidate_order(
    cxxime::UserDictKind kind, const std::string& code, std::size_t limit) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.query_candidate_order(kind, code, limit);
}

cxxime::IPCStatus SessionManager::replace_candidate_order(
    cxxime::UserDictKind kind, const std::string& code,
    const std::vector<cxxime::ManualCandidateOrderEntry>& entries,
    std::uint64_t expected_version, bool* version_conflict) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.replace_candidate_order(kind, code, entries, expected_version,
                                           version_conflict);
}

cxxime::IPCStatus SessionManager::clear_candidate_order(cxxime::UserDictKind kind,
                                                        const std::string& code,
                                                        std::uint64_t expected_version,
                                                        bool* version_conflict) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.clear_candidate_order(kind, code, expected_version, version_conflict);
}

bool SessionManager::save_candidate_preferences(bool force) {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    return shared_.save_candidate_preferences(force);
}

bool SessionManager::freeze_and_save_candidate_preferences() {
    std::lock_guard<std::mutex> reload_lock(reload_mutex_);
    auto resources = shared_.snapshot();
    if (resources.dict) {
        resources.dict->freeze_candidate_preferences();
    }
    if (resources.wubi_dict) {
        resources.wubi_dict->freeze_candidate_preferences();
    }
    return shared_.save_candidate_preferences(true);
}

void SessionManager::persist_input_mode(cxxime::InputMode mode) {
    if (config_patch_handler_) {
        nlohmann::json patch;
        patch["engine"]["input_mode"] = static_cast<int>(mode);
        config_patch_handler_(patch.dump());
    }
}
