// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/context.h>

#include <algorithm>

#include <windows.h>

#include <cxxime/key_event.h>

namespace cxxime {

bool Context::is_composing() const {
    return !pinyin_buffer.empty();
}

size_t Context::preedit_cursor() const {
    const size_t distance = (std::min)(preedit_cursor_from_end_, pinyin_buffer.size());
    return pinyin_buffer.size() - distance;
}

void Context::set_preedit(std::string text) {
    if (pinyin_buffer != text) {
        pinyin_buffer = std::move(text);
        ++preedit_revision_;
    }
    preedit_cursor_from_end_ = 0;
}

void Context::clear_preedit() {
    if (!pinyin_buffer.empty()) {
        pinyin_buffer.clear();
        ++preedit_revision_;
    }
    preedit_cursor_from_end_ = 0;
}

void Context::insert_preedit(char ch) {
    pinyin_buffer.insert(preedit_cursor(), 1, ch);
    ++preedit_revision_;
}

bool Context::erase_preedit_before_cursor() {
    const size_t cursor = preedit_cursor();
    if (cursor == 0) {
        return false;
    }
    pinyin_buffer.erase(cursor - 1, 1);
    ++preedit_revision_;
    return true;
}

bool Context::erase_preedit_at_cursor() {
    const size_t cursor = preedit_cursor();
    if (cursor >= pinyin_buffer.size()) {
        return false;
    }
    pinyin_buffer.erase(cursor, 1);
    --preedit_cursor_from_end_;
    ++preedit_revision_;
    return true;
}

bool Context::move_preedit_cursor_left() {
    if (preedit_cursor() == 0) {
        return false;
    }
    ++preedit_cursor_from_end_;
    return true;
}

bool Context::move_preedit_cursor_right() {
    if (preedit_cursor_from_end_ == 0) {
        return false;
    }
    --preedit_cursor_from_end_;
    return true;
}

bool Context::move_preedit_cursor_home() {
    if (preedit_cursor() == 0) {
        return false;
    }
    preedit_cursor_from_end_ = pinyin_buffer.size();
    return true;
}

bool Context::move_preedit_cursor_end() {
    if (preedit_cursor_from_end_ == 0) {
        return false;
    }
    preedit_cursor_from_end_ = 0;
    return true;
}

bool Context::edit_preedit(const KeyEvent& event) {
    if (event.is_key_up || event.is_ctrl() || event.is_alt() || !is_composing()) {
        return false;
    }

    switch (event.keycode) {
    case VK_BACK:
        erase_preedit_before_cursor();
        break;
    case VK_DELETE:
        erase_preedit_at_cursor();
        break;
    case VK_LEFT:
        move_preedit_cursor_left();
        break;
    case VK_RIGHT:
        move_preedit_cursor_right();
        break;
    case VK_HOME:
        move_preedit_cursor_home();
        break;
    case VK_END:
        move_preedit_cursor_end();
        break;
    default:
        return false;
    }

    if (pinyin_buffer.empty()) {
        candidates = {};
    }
    return true;
}

void Context::reset() {
    clear_preedit();
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
    clear_preedit();
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
    clear_preedit();
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
