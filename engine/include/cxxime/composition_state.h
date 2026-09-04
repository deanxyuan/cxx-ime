// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_COMPOSITION_STATE_H_
#define CXXIME_COMPOSITION_STATE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cxxime/candidate_selection.h>

namespace cxxime {

struct ConvertedSegment {
    std::string text;
    std::string raw_input;
    std::vector<CandidateCanonicalVariant> variants;
    std::size_t primary_variant = 0;
};

struct ActiveSegment {
    CompositionScheme scheme = CompositionScheme::kPinyin;
    std::string input;
    std::size_t cursor = 0;
};

class CompositionState {
public:
    const std::vector<ConvertedSegment>& converted_segments() const { return converted_segments_; }
    const ActiveSegment& active() const { return active_; }
    uint64_t revision() const { return revision_; }

    bool is_composing() const;
    std::size_t raw_input_size() const;
    bool set_scheme(CompositionScheme scheme);
    bool set_active_input(std::string input, std::size_t cursor);
    bool insert(char ch);
    bool erase_before_cursor();
    bool erase_at_cursor();
    bool move_cursor_left();
    bool move_cursor_right();
    bool move_cursor_home();
    bool move_cursor_end();
    bool confirm_prefix(const TextSelectionAction& action);
    bool replace_active_input(const ReplaceActiveInputAction& action);
    bool reopen_last_segment();
    bool finalize_candidate(const TextSelectionAction& action, std::string& output);
    bool finalize_raw(std::string& output);
    void cancel();

private:
    void touch();

    std::vector<ConvertedSegment> converted_segments_;
    ActiveSegment active_;
    uint64_t revision_ = 0;
};

} // namespace cxxime

#endif // CXXIME_COMPOSITION_STATE_H_
