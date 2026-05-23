// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/engine.h>

namespace cxxime {

bool Engine::initialize(const std::string& dict_path, const std::string& config_path) {
    if (!dict_.open(dict_path))
        return false;

    translator_.set_dict(&dict_);

    if (!config_path.empty()) {
        config_.load(config_path);
    }

    return true;
}

void Engine::finalize() {
    dict_.close();
    context_.reset();
}

ProcessResult Engine::process_key(const KeyEvent& event) {
    auto result = processor_.process_key(event, context_);

    // After processing, update candidates if still composing
    if (result == ProcessResult::ACCEPTED && context_.is_composing()) {
        auto page = translator_.translate(context_.pinyin_buffer, context_.page_index, config_.page_size);
        context_.update_candidates(std::move(page));
    }

    // If committed, update user frequency
    if (result == ProcessResult::COMMITTED && !context_.committed_text.empty()) {
        std::string code = context_.pinyin_buffer;
        if (code.empty()) {
            code = dict_.reverse_lookup(context_.committed_text);
        }
        dict_.update_frequency(context_.committed_text, code);
    }

    return result;
}

const Context& Engine::context() const {
    return context_;
}

Context& Engine::context() {
    return context_;
}

bool Engine::select_candidate(int index) {
    if (index < 0 || index >= (int)context_.candidates.candidates.size())
        return false;

    context_.candidates.highlighted = index;
    context_.committed_text = context_.candidates.candidates[index].text;
    return true;
}

std::string Engine::get_commit_text() {
    std::string text = context_.committed_text;
    context_.pinyin_buffer.clear();
    context_.committed_text.clear();
    context_.candidates = {};
    context_.page_index = 0;
    return text;
}

void Engine::clear() {
    context_.reset();
}

} // namespace cxxime
