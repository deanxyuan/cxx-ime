// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/context.h>

#include <algorithm>

#include <windows.h>

#include <cxxime/key_event.h>
#include <cxxime/input_limits.h>

namespace cxxime {

bool Context::is_composing() const {
    return !pinyin_buffer.empty();
}

size_t Context::preedit_cursor() const {
    const size_t distance = (std::min)(preedit_cursor_from_end_, pinyin_buffer.size());
    return pinyin_buffer.size() - distance;
}

bool Context::set_preedit(std::string text) {
    if (text.size() > kMaxInputCodeLength ||
        std::any_of(text.begin(), text.end(), [](char ch) {
            return ch == '\0' || static_cast<unsigned char>(ch) > 0x7f;
        })) {
        return false;
    }
    if (pinyin_buffer != text) {
        pinyin_buffer = std::move(text);
        ++preedit_revision_;
    }
    preedit_cursor_from_end_ = 0;
    return true;
}

void Context::clear_preedit() {
    if (!pinyin_buffer.empty()) {
        pinyin_buffer.clear();
        ++preedit_revision_;
    }
    preedit_cursor_from_end_ = 0;
}

bool Context::insert_preedit(char ch) {
    if (pinyin_buffer.size() >= kMaxInputCodeLength || ch == '\0' ||
        static_cast<unsigned char>(ch) > 0x7f) {
        return false;
    }
    pinyin_buffer.insert(preedit_cursor(), 1, ch);
    ++preedit_revision_;
    return true;
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
    clear_commit_evidence();
}

bool Context::commit_candidate(int index) {
    if (index < 0 || index >= static_cast<int>(candidates.candidates.size())) {
        return false;
    }
    candidates.highlighted = index;
    committed_candidate_ = candidates.candidates[static_cast<std::size_t>(index)];
    committed_candidate_code_ = pinyin_buffer;
    has_committed_candidate_ = true;
    committed_text = committed_candidate_.text;
    commit_source_ = CommitSource::kCandidate;
    return true;
}

const Candidate* Context::committed_candidate() const {
    return has_committed_candidate_ ? &committed_candidate_ : nullptr;
}

const std::string& Context::committed_candidate_code() const {
    return committed_candidate_code_;
}

void Context::clear_commit_evidence() {
    committed_candidate_ = {};
    committed_candidate_code_.clear();
    has_committed_candidate_ = false;
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
    clear_commit_evidence();
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
    clear_commit_evidence();
    return {std::move(text), source};
}

void Context::update_candidates(CandidatePage&& page) {
    const int highlight_count = highlight_count_after_page_change_;
    highlight_count_after_page_change_ = 0;
    candidates = std::move(page);
    page_index = candidates.page_index;
    page_offset = candidates.page_offset;
    const int selectable_count = selectable_candidate_count();
    const int candidate_count = static_cast<int>(candidates.candidates.size());
    if (candidate_count <= 0) {
        candidates.highlighted = -1;
    } else if (highlight_count > 0) {
        candidates.highlighted = (std::min)(highlight_count, candidate_count) - 1;
    } else if (selectable_count <= 0 || candidates.highlighted < 0 ||
               candidates.highlighted >= selectable_count) {
        candidates.highlighted = 0;
    }
}

void Context::reset_pagination() {
    page_index = 0;
    page_offset = 0;
    visible_candidate_count = 0;
    previous_pages_.clear();
    highlight_count_after_page_change_ = 0;
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
    previous_pages_.push_back({page_offset, step});
    page_offset += step;
    ++page_index;
}

void Context::move_to_previous_page(bool highlight_last) {
    if (previous_pages_.empty()) {
        return;
    }
    page_offset = previous_pages_.back().offset;
    highlight_count_after_page_change_ =
        highlight_last ? previous_pages_.back().visible_candidate_count : 0;
    previous_pages_.pop_back();
    --page_index;
}

void Context::move_to_next_candidate() {
    const int count = selectable_candidate_count();
    if (count <= 0) {
        return;
    }

    if (candidates.highlighted < count - 1) {
        ++candidates.highlighted;
        return;
    }

    const int previous_offset = page_offset;
    move_to_next_page();
    if (page_offset != previous_offset) {
        candidates.highlighted = 0;
    }
}

void Context::move_to_previous_candidate() {
    const int count = selectable_candidate_count();
    if (count <= 0) {
        return;
    }

    if (candidates.highlighted > 0) {
        --candidates.highlighted;
        return;
    }

    move_to_previous_page(true);
}

} // namespace cxxime
