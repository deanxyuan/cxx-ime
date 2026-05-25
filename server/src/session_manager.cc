// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager.h"
#include <windows.h>
#include <cxxime/logging.h>
#include <cxxime/data_path.h>

bool SharedResources::load(const std::string& dict_path, const std::string& config_path) {
    if (!dict.open(dict_path)) {
        CXXIME_LOG(L"SharedResources: dict.open FAILED");
        return false;
    }
    if (!config_path.empty()) {
        config.load(config_path);
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
