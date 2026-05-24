// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/context.h>

namespace cxxime {

bool Context::is_composing() const {
    return !pinyin_buffer.empty();
}

void Context::reset() {
    pinyin_buffer.clear();
    committed_text.clear();
    candidates = {};
    page_index = 0;
}

std::string Context::commit() {
    std::string text;
    if (!committed_text.empty()) {
        text = committed_text;
    } else if (!candidates.candidates.empty() && candidates.highlighted >= 0 &&
               candidates.highlighted < (int)candidates.candidates.size()) {
        text = candidates.candidates[candidates.highlighted].text;
    } else if (!pinyin_buffer.empty()) {
        text = pinyin_buffer;
    }
    pinyin_buffer.clear();
    committed_text.clear();
    candidates = {};
    page_index = 0;
    return text;
}

void Context::update_candidates(CandidatePage&& page) {
    candidates = std::move(page);
    if (!candidates.candidates.empty() && candidates.highlighted < 0) {
        candidates.highlighted = 0;
    }
}

} // namespace cxxime
