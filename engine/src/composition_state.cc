// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/composition_state.h>

#include <algorithm>
#include <utility>

#include <cxxime/input_limits.h>

namespace cxxime {

namespace {

bool valid_ascii_input(const std::string& input) {
    return std::all_of(input.begin(), input.end(), [](char ch) {
        return ch != '\0' && static_cast<unsigned char>(ch) <= 0x7f;
    });
}

std::string converted_text(const std::vector<ConvertedSegment>& segments) {
    std::string text;
    for (const auto& segment : segments) {
        text += segment.text;
    }
    return text;
}

std::size_t converted_text_size(const std::vector<ConvertedSegment>& segments) {
    std::size_t size = 0;
    for (const auto& segment : segments) {
        size += segment.text.size();
    }
    return size;
}

} // namespace

bool CompositionState::is_composing() const {
    return !converted_segments_.empty() || !active_.input.empty();
}

std::size_t CompositionState::raw_input_size() const {
    std::size_t size = active_.input.size();
    for (const auto& segment : converted_segments_) {
        size += segment.raw_input.size();
    }
    return size;
}

bool CompositionState::set_scheme(CompositionScheme scheme) {
    if (active_.scheme == scheme) {
        return true;
    }
    active_.scheme = scheme;
    touch();
    return true;
}

bool CompositionState::set_active_input(std::string input, std::size_t cursor) {
    if (cursor > input.size() || !valid_ascii_input(input) ||
        raw_input_size() - active_.input.size() + input.size() > kMaxInputCodeLength ||
        converted_text_size(converted_segments_) + input.size() >= kCandidateTextCapacity) {
        return false;
    }
    if (active_.input != input || active_.cursor != cursor) {
        active_.input = std::move(input);
        active_.cursor = cursor;
        touch();
    }
    return true;
}

bool CompositionState::insert(char ch) {
    if (ch == '\0' || static_cast<unsigned char>(ch) > 0x7f ||
        raw_input_size() >= kMaxInputCodeLength ||
        converted_text_size(converted_segments_) + active_.input.size() + 1 >=
            kCandidateTextCapacity) {
        return false;
    }
    active_.input.insert(active_.cursor, 1, ch);
    ++active_.cursor;
    touch();
    return true;
}

bool CompositionState::erase_before_cursor() {
    if (active_.cursor == 0) {
        return reopen_last_segment();
    }
    active_.input.erase(active_.cursor - 1, 1);
    --active_.cursor;
    touch();
    return true;
}

bool CompositionState::erase_at_cursor() {
    if (active_.cursor >= active_.input.size()) {
        return false;
    }
    active_.input.erase(active_.cursor, 1);
    touch();
    return true;
}

bool CompositionState::move_cursor_left() {
    if (active_.cursor == 0) {
        return reopen_last_segment();
    }
    --active_.cursor;
    return true;
}

bool CompositionState::move_cursor_right() {
    if (active_.cursor >= active_.input.size()) {
        return false;
    }
    ++active_.cursor;
    return true;
}

bool CompositionState::move_cursor_home() {
    if (active_.cursor == 0) {
        return false;
    }
    active_.cursor = 0;
    return true;
}

bool CompositionState::move_cursor_end() {
    if (active_.cursor == active_.input.size()) {
        return false;
    }
    active_.cursor = active_.input.size();
    return true;
}

bool CompositionState::confirm_prefix(const TextSelectionAction& action) {
    if (action.text.empty() || action.consumed_input_bytes == 0 ||
        action.consumed_input_bytes >= active_.input.size() ||
        action.consumed_input_bytes > active_.cursor ||
        action.primary_variant >= action.variants.size() ||
        converted_text_size(converted_segments_) + action.text.size() +
                active_.input.size() - action.consumed_input_bytes >=
            kCandidateTextCapacity) {
        return false;
    }

    ConvertedSegment segment;
    segment.text = action.text;
    segment.raw_input = active_.input.substr(0, action.consumed_input_bytes);
    segment.variants = action.variants;
    segment.primary_variant = action.primary_variant;
    converted_segments_.push_back(std::move(segment));
    active_.input.erase(0, action.consumed_input_bytes);
    active_.cursor -= action.consumed_input_bytes;
    touch();
    return true;
}

bool CompositionState::replace_active_input(const ReplaceActiveInputAction& action) {
    if (action.cursor > action.input.size() || !valid_ascii_input(action.input) ||
        raw_input_size() - active_.input.size() + action.input.size() > kMaxInputCodeLength ||
        converted_text_size(converted_segments_) + action.input.size() >= kCandidateTextCapacity) {
        return false;
    }
    active_.scheme = action.scheme;
    active_.input = action.input;
    active_.cursor = action.cursor;
    touch();
    return true;
}

bool CompositionState::reopen_last_segment() {
    if (converted_segments_.empty() || active_.cursor != 0) {
        return false;
    }
    ConvertedSegment segment = std::move(converted_segments_.back());
    converted_segments_.pop_back();
    active_.input.insert(0, segment.raw_input);
    active_.cursor = segment.raw_input.size();
    touch();
    return true;
}

bool CompositionState::finalize_candidate(const TextSelectionAction& action,
                                          std::string& output) {
    if (action.text.empty() || action.consumed_input_bytes != active_.input.size() ||
        action.primary_variant >= action.variants.size() ||
        converted_text_size(converted_segments_) + action.text.size() >= kCandidateTextCapacity) {
        return false;
    }
    output = converted_text(converted_segments_) + action.text;
    cancel();
    return true;
}

bool CompositionState::finalize_raw(std::string& output) {
    if (!is_composing() ||
        converted_text_size(converted_segments_) + active_.input.size() >= kCandidateTextCapacity) {
        return false;
    }
    output = converted_text(converted_segments_) + active_.input;
    cancel();
    return true;
}

void CompositionState::cancel() {
    const CompositionScheme scheme = active_.scheme;
    if (!converted_segments_.empty() || !active_.input.empty() || active_.cursor != 0) {
        converted_segments_.clear();
        active_ = {};
        active_.scheme = scheme;
        touch();
    }
}

void CompositionState::touch() { ++revision_; }

} // namespace cxxime
