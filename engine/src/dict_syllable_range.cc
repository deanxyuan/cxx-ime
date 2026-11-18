// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>

#include <cxxime/dict.h>

namespace cxxime {

SyllableRange Dict::syllable_root() const { return {0, 0, id_index_.size(), 0}; }

bool Dict::intersect_syllable_range(const SyllableRange& parent,
                                    const std::vector<uint32_t>& allowed,
                                    std::size_t& allowed_offset, SyllableRange& child,
                                    uint32_t& syllable_id) const {
    if (allowed_offset >= allowed.size() || parent.exact_end >= parent.end ||
        parent.end > id_index_.size()) {
        allowed_offset = allowed.size();
        return false;
    }
    const auto begin = id_index_.begin() + parent.exact_end;
    const auto end = id_index_.begin() + parent.end;
    const auto first = std::lower_bound(
        begin, end, allowed[allowed_offset],
        [&](const IdEntry& entry, uint32_t id) { return entry.ids[parent.depth] < id; });
    if (first == end) {
        allowed_offset = allowed.size();
        return false;
    }
    syllable_id = first->ids[parent.depth];
    const auto match =
        std::lower_bound(allowed.begin() + allowed_offset, allowed.end(), syllable_id);
    allowed_offset = static_cast<std::size_t>(match - allowed.begin());
    if (match == allowed.end() || *match != syllable_id) {
        return false;
    }
    ++allowed_offset;
    const auto last =
        std::upper_bound(first, end, syllable_id, [&](uint32_t id, const IdEntry& entry) {
            return id < entry.ids[parent.depth];
        });
    if (last == end) {
        allowed_offset = allowed.size();
    }
    const auto exact_end = std::partition_point(
        first, last, [&](const IdEntry& entry) { return entry.count == parent.depth + 1; });
    child = {static_cast<std::size_t>(first - id_index_.begin()),
             static_cast<std::size_t>(exact_end - id_index_.begin()),
             static_cast<std::size_t>(last - id_index_.begin()), parent.depth + 1};
    return true;
}

bool Dict::read_syllable_candidate(const SyllableRange& range, std::size_t& cursor,
                                   Candidate& candidate) const {
    candidate = {};
    cursor = (std::max)(cursor, range.begin);
    if (cursor >= range.exact_end || range.exact_end > id_index_.size()) {
        return false;
    }
    fill_system_candidate(id_index_[cursor++].index, candidate, 0);
    // A disabled first homophone does not terminate the range.
    if (is_system_entry_disabled(candidate.text)) {
        candidate = {};
    }
    return true;
}

} // namespace cxxime
