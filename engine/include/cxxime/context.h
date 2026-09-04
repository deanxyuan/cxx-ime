// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONTEXT_H_
#define CXXIME_CONTEXT_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cxxime/ascii_composer.h>
#include <cxxime/composition_state.h>
#include <cxxime/output_options.h>
#include <cxxime/translation_result.h>

namespace cxxime {

struct KeyEvent;

struct CompositionOrigin {
    CompositionScheme scheme = CompositionScheme::kPinyin;
    std::string input;
    std::size_t cursor = 0;
};

class Context {
public:
    std::string committed_text;
    int visible_candidate_count = 0;
    AsciiModeSwitchStyle caps_lock_style{};

    bool is_composing() const { return composition_.is_composing(); }
    const std::string& active_input() const { return composition_.active().input; }
    std::size_t preedit_cursor() const { return composition_.active().cursor; }
    uint64_t preedit_revision() const { return composition_.revision(); }
    CompositionScheme composition_scheme() const { return composition_.active().scheme; }
    const CompositionState& composition() const { return composition_; }
    const TranslationResult& translation() const { return translation_; }
    TranslationResult& translation() { return translation_; }
    CandidatePage candidate_page() const { return translation_.candidate_page(); }
    int page_index() const { return translation_.page_index; }
    int page_offset() const { return translation_.page_offset; }
    int highlighted() const { return translation_.highlighted; }
    int candidate_count() const { return static_cast<int>(translation_.entries.size()); }
    const CandidateEntry* candidate_entry(int index) const;
    const Candidate* candidate(int index) const;

    bool set_ime_scheme(CompositionScheme scheme);
    bool set_preedit(std::string text);
    bool start_composition(CompositionScheme scheme, std::string text, std::size_t cursor);
    bool enter_inline_ascii(bool preserve_origin);
    bool restore_composition_origin();
    void clear_preedit();
    bool insert_preedit(char ch);
    bool erase_preedit_before_cursor();
    bool erase_preedit_at_cursor();
    bool move_preedit_cursor_left();
    bool move_preedit_cursor_right();
    bool move_preedit_cursor_home();
    bool move_preedit_cursor_end();
    bool edit_preedit(const KeyEvent& event);
    void reset();
    bool commit_candidate(int index);
    bool commit_entry(const CandidateEntry& entry);
    bool commit_selection(const TextSelectionAction& action);
    bool finalize_raw(CommitSource source);
    bool request_candidate_selection(int index);
    std::optional<int> take_requested_candidate_selection();
    const Candidate* committed_candidate() const;
    const std::string& committed_candidate_code() const;
    void clear_commit_evidence();
    std::string commit();
    void update_candidates(CandidatePage&& page);
    void update_translation(TranslationResult&& result);
    void clear_translation();
    void replace_composition(CompositionState&& state, TranslationResult&& result);
    void reset_pagination();
    void move_to_next_page();
    void move_to_previous_page(bool highlight_last = false);
    void move_to_next_candidate();
    void move_to_previous_candidate();
    int selectable_candidate_count() const;

    const std::optional<CompositionOrigin>& composition_origin() const {
        return composition_origin_;
    }

    void set_commit_source(CommitSource source) { commit_source_ = source; }
    CommitSource commit_source() const { return commit_source_; }
    std::pair<std::string, CommitSource> commit_with_source();

    std::unordered_map<std::string, bool> pair_open;
    std::unordered_map<std::string, int> alt_index;
    char last_committed_char = '\0';

private:
    struct PageHistoryEntry {
        int offset = 0;
        int visible_candidate_count = 0;
    };

    CompositionState composition_;
    CompositionScheme ime_scheme_ = CompositionScheme::kPinyin;
    TranslationResult translation_;
    std::vector<PageHistoryEntry> previous_pages_;
    int highlight_count_after_page_change_ = 0;
    CommitSource commit_source_ = CommitSource::kRawCode;
    Candidate committed_candidate_;
    std::string committed_candidate_code_;
    bool has_committed_candidate_ = false;
    std::optional<int> requested_candidate_index_;
    std::optional<CompositionOrigin> composition_origin_;
};

} // namespace cxxime

#endif // CXXIME_CONTEXT_H_
