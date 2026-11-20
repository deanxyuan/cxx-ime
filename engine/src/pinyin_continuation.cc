// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pinyin_continuation.h"

#include <algorithm>
#include <cstdint>

#include <cxxime/pinyin_resource.h>

namespace cxxime {
namespace {

std::vector<std::size_t> han_text_offsets(const std::string& text) {
    std::vector<std::size_t> offsets{0};
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset]);
        const std::size_t length = first >= 0xe0 && first <= 0xef   ? 3
                                   : first >= 0xf0 && first <= 0xf4 ? 4
                                                                    : 0;
        if (length == 0 || offset + length > text.size()) {
            return {};
        }
        uint32_t codepoint = first & (length == 3 ? 0x0f : 0x07);
        for (std::size_t index = 1; index < length; ++index) {
            const auto next = static_cast<unsigned char>(text[offset + index]);
            if ((next & 0xc0) != 0x80) {
                return {};
            }
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        const bool han = (codepoint >= 0x3400 && codepoint <= 0x9fff) ||
                         (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
                         (codepoint >= 0x20000 && codepoint <= 0x323af);
        if (!han || (length == 4 && codepoint < 0x10000)) {
            return {};
        }
        offset += length;
        offsets.push_back(offset);
    }
    return offsets;
}

} // namespace

PinyinContinuation PinyinContinuation::build(const PinyinResourceSet& resources,
                                             const PinyinInputGraph& current, bool fuzzy,
                                             const CandidateContinuation* previous,
                                             const SegmentedPath* previous_path) {
    PinyinContinuation result;
    if (!previous || previous->input.empty() ||
        current.input.size() != previous->input.size() + 1 ||
        current.input.compare(0, previous->input.size(), previous->input) != 0 ||
        previous->candidate.source != CandidateSource::kPinyin) {
        return result;
    }
    std::vector<SegmentedPath> paths;
    if (!previous_path) {
        const auto old = PinyinInputGraph::build(resources, previous->input, fuzzy, true);
        paths = old.align(previous->candidate.code, previous->candidate.syllables,
                          previous->input.size());
        if (paths.empty()) {
            return result;
        }
        previous_path = &paths.front();
    }
    const auto offsets = han_text_offsets(previous->candidate.text);
    const auto& path = *previous_path;
    if (offsets.size() != path.syllables.size() + 1) {
        return result;
    }
    std::size_t retained = 0;
    std::size_t raw = 0;
    for (; retained < path.syllables.size(); ++retained) {
        const auto vertex = current.edges.find(raw);
        if (vertex == current.edges.end()) {
            break;
        }
        // Only the tail whose spelling can cross the old end is mutable.
        const bool extends_tail =
            std::any_of(vertex->second.begin(), vertex->second.end(), [&](const auto& group) {
                return group.first > previous->input.size() &&
                       std::any_of(group.second.begin(), group.second.end(),
                                   [](const auto& edge) { return edge.type != kAbbreviation; });
            });
        const auto end = raw + path.input_lengths[retained];
        const auto group = vertex->second.find(end);
        if (extends_tail || group == vertex->second.end() ||
            std::none_of(group->second.begin(), group->second.end(), [&](const auto& edge) {
                return edge.syllable == path.syllables[retained];
            })) {
            break;
        }
        raw = end;
    }
    if (retained == 0) {
        return result;
    }
    for (const auto& vertex : current.edges) {
        const auto tail = vertex.second.find(current.input_size);
        if (tail != vertex.second.end() &&
            std::any_of(tail->second.begin(), tail->second.end(), [](const auto& edge) {
                return edge.type == kNormalSpelling || edge.type == kFuzzySpelling;
            })) {
            result.completes_tail = true;
            break;
        }
    }
    result.end = raw;
    result.text = previous->candidate.text.substr(0, offsets[retained]);
    result.text_offsets.assign(raw + 1, SIZE_MAX);
    raw = 0;
    result.text_offsets[0] = 0;
    for (std::size_t index = 0; index < retained; ++index) {
        const auto end = raw + path.input_lengths[index];
        const auto type = path.spelling_types[index];
        const bool abbreviated = type == kAbbreviation || type == kCompletionSpelling ||
                                 type == kFuzzyCompletionSpelling;
        result.spelling_locks.push_back({raw, end, path.syllables[index], abbreviated});
        raw = end;
        result.text_offsets[raw] = offsets[index + 1];
    }
    return result;
}

bool PinyinContinuation::accepts(std::size_t start, std::size_t finish,
                                 const std::string& word) const {
    if (start >= end) {
        return true;
    }
    const auto from = text_offsets[start];
    const auto to = text_offsets[(std::min)(finish, end)];
    if (from == SIZE_MAX || to == SIZE_MAX || to < from) {
        return false;
    }
    const auto length = to - from;
    return word.compare(0, length, text, from, length) == 0 &&
           (finish > end || word.size() == length);
}

bool PinyinContinuation::accepts_spelling(std::size_t start, std::size_t finish,
                                          const std::string& syllable) const {
    for (const auto& lock : spelling_locks) {
        if (!completes_tail) {
            if (start == lock.start) {
                return finish == lock.end && syllable == lock.syllable;
            }
        } else if (lock.abbreviated && start < lock.end && finish > lock.start &&
                   (start != lock.start || finish != lock.end || syllable != lock.syllable)) {
            // A complete tail may reshape normal spans, but not an earlier abbreviation.
            return false;
        }
    }
    return true;
}

} // namespace cxxime
