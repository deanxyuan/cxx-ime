// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager.h"
#include <windows.h>
#include <cxxime/logging.h>
#include <cxxime/data_path.h>

bool SharedResources::load(const std::string& dict_path, const std::string& cfg_path) {
    std::string user_dict_path = cxxime::user_data_path("user.tsv");
    if (!dict.open(dict_path, user_dict_path)) {
        CXXIME_LOG(L"SharedResources: dict.open FAILED");
        return false;
    }
    if (!cfg_path.empty()) {
        config_path = cfg_path;
        config.load(config_path);
        // Overlay user config from %APPDATA%
        config.load(cxxime::user_data_path("default.json"));
        config.load_themes(cxxime::data_path("themes.json"));
    }
    std::string sp_path = cxxime::Engine::derive_spellings_path(dict_path);
    if (!sp_path.empty() && spellings.load(sp_path) && spellings.has_spellings()) {
        syllabifier = std::make_unique<cxxime::Syllabifier>(spellings);
    }
    return true;
}

bool SessionManager::initialize(const std::string& dict_path, const std::string& config_path) {
    return shared_.load(dict_path, config_path);
}

uint32_t SessionManager::create_session() {
    auto engine = std::make_unique<cxxime::Engine>();
    if (!engine->initialize(shared_.dict, shared_.spellings,
                            shared_.syllabifier.get(), shared_.config))
        return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = next_id_++;
    sessions_[id] = {std::move(engine), std::chrono::steady_clock::now()};
    return id;
}

void SessionManager::destroy_session(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(id);
}

cxxime::Engine* SessionManager::get_engine(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second.last_activity = std::chrono::steady_clock::now();
        return it->second.engine.get();
    }
    return nullptr;
}

void SessionManager::touch_session(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second.last_activity = std::chrono::steady_clock::now();
    }
}

size_t SessionManager::cleanup_idle_sessions(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    size_t count = 0;
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.last_activity).count();
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
    config = cxxime::Config();  // Reset to defaults
    config.load(config_path);
    config.load(cxxime::user_data_path("default.json"));
    config.load_themes(cxxime::data_path("themes.json"));
}

void SessionManager::reload_config() {
    shared_.reload_config();
    CXXIME_LOG(L"SessionManager: config reloaded, %zu active sessions", sessions_.size());
}

cxxime::ImeStatus SessionManager::get_ime_status(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        return it->second.ime_status;
    }
    return {};
}

cxxime::ImeStatus SessionManager::toggle_chinese(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return {};
    auto& s = it->second;
    s.ime_status.chinese_mode = !s.ime_status.chinese_mode;
    s.ime_status.revision++;
    s.engine->ascii_composer().set_ascii_mode(!s.ime_status.chinese_mode);
    return s.ime_status;
}

cxxime::ImeStatus SessionManager::toggle_shape(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return {};
    auto& s = it->second;
    s.ime_status.full_shape = !s.ime_status.full_shape;
    s.ime_status.revision++;
    return s.ime_status;
}

cxxime::ImeStatus SessionManager::toggle_punct(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return {};
    auto& s = it->second;
    s.ime_status.chinese_punct = !s.ime_status.chinese_punct;
    s.ime_status.revision++;
    return s.ime_status;
}

cxxime::ImeStatus SessionManager::switch_input_mode(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return {};
    auto& s = it->second;
    s.ime_status.input_mode = (s.ime_status.input_mode == cxxime::InputMode::PINYIN)
        ? cxxime::InputMode::WUBI : cxxime::InputMode::PINYIN;
    s.ime_status.revision++;
    return s.ime_status;
}

void SessionManager::sync_ascii_mode(uint32_t id, bool ascii_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second.ime_status.chinese_mode = !ascii_mode;
    }
}

void SessionManager::sync_caps_lock(uint32_t id, bool caps_lock) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second.ime_status.caps_lock = caps_lock;
    }
}
