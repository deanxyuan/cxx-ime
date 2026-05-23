// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager.h"

uint32_t SessionManager::create_session(const std::string& dict_path, const std::string& config_path) {
    auto engine = std::make_unique<cxxime::Engine>();
    if (!engine->initialize(dict_path, config_path))
        return 0;
    uint32_t id = next_id_++;
    sessions_[id] = std::move(engine);
    return id;
}

void SessionManager::destroy_session(uint32_t id) {
    sessions_.erase(id);
}

cxxime::Engine* SessionManager::get_engine(uint32_t id) {
    auto it = sessions_.find(id);
    return it != sessions_.end() ? it->second.get() : nullptr;
}
