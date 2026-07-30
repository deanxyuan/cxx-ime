// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/context.h>

#include <algorithm>

namespace cxxime {

bool Context::is_composing() const {
    return !pinyin_buffer.empty();
}

void Context::reset() {
    pinyin_buffer.clear();
    committed_text.clear();
    candidates = {};
    reset_pagination();
    temporary_ascii_composition = false;
    commit_source_ = CommitSource::kRawCode;
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
    reset_pagination();
    temporary_ascii_composition = false;
    commit_source_ = CommitSource::kRawCode;
    return text;
}

std::pair<std::string, CommitSource> Context::commit_with_source() {
    std::string text;
    CommitSource source = commit_source_;
    if (!committed_text.empty()) {
        text = committed_text;
        // source 已经由 Engine 设置（kRawCode 或 kCandidate）
    } else if (!candidates.candidates.empty() && candidates.highlighted >= 0 &&
               candidates.highlighted < (int)candidates.candidates.size()) {
        text = candidates.candidates[candidates.highlighted].text;
        source = CommitSource::kCandidate;
    } else if (!pinyin_buffer.empty()) {
        text = pinyin_buffer;
        source = temporary_ascii_composition
            ? CommitSource::kRawCodePreserveCase
            : CommitSource::kRawCode;
    }
    pinyin_buffer.clear();
    committed_text.clear();
    candidates = {};
    reset_pagination();
    temporary_ascii_composition = false;
    commit_source_ = CommitSource::kRawCode;
    return {std::move(text), source};
}

void Context::update_candidates(CandidatePage&& page) {
    candidates = std::move(page);
    page_index = candidates.page_index;
    page_offset = candidates.page_offset;
    if (!candidates.candidates.empty() && candidates.highlighted < 0) {
        candidates.highlighted = 0;
    }
}

void Context::reset_pagination() {
    page_index = 0;
    page_offset = 0;
    visible_candidate_count = 0;
    previous_page_offsets_.clear();
}

int Context::selectable_candidate_count() const {
    int count = static_cast<int>(candidates.candidates.size());
    if (visible_candidate_count > 0) {
        count = (std::min)(count, visible_candidate_count);
    }
    return count;
}

void Context::move_to_next_page() {
    int step = selectable_candidate_count();
    if (step <= 0 || page_offset + step >= candidates.total_count) {
        return;
    }
    previous_page_offsets_.push_back(page_offset);
    page_offset += step;
    ++page_index;
}

void Context::move_to_previous_page() {
    if (previous_page_offsets_.empty()) {
        return;
    }
    page_offset = previous_page_offsets_.back();
    previous_page_offsets_.pop_back();
    --page_index;
}

} // namespace cxxime
