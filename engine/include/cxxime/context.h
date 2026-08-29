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
#include <cxxime/candidate.h>
#include <cxxime/output_options.h>

namespace cxxime {

struct KeyEvent;

enum class CompositionKind {
    kIme,
    kInlineAscii,
    kSymbol,
};

struct CompositionOrigin {
    CompositionKind kind = CompositionKind::kIme;
    std::string code;
    size_t cursor = 0;
};

class Context {
public:
    std::string pinyin_buffer;
    CandidatePage candidates;
    std::string committed_text;
    int page_index = 0;
    int page_offset = 0;
    int visible_candidate_count = 0;
    // CapsLock mode (set by Engine before calling processor)
    AsciiModeSwitchStyle caps_lock_style{};

    bool is_composing() const;
    size_t preedit_cursor() const;
    uint64_t preedit_revision() const { return preedit_revision_; }
    bool set_preedit(std::string text);
    bool start_composition(CompositionKind kind, std::string text, size_t cursor);
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
    const Candidate* committed_candidate() const;
    const std::string& committed_candidate_code() const;
    void clear_commit_evidence();
    std::string commit();
    void update_candidates(CandidatePage&& page);
    void reset_pagination();
    void move_to_next_page();
    void move_to_previous_page(bool highlight_last = false);
    void move_to_next_candidate();
    void move_to_previous_candidate();
    int selectable_candidate_count() const;

    CompositionKind composition_kind() const { return composition_kind_; }
    const std::optional<CompositionOrigin>& composition_origin() const {
        return composition_origin_;
    }

    void set_commit_source(CommitSource s) { commit_source_ = s; }
    CommitSource commit_source() const { return commit_source_; }

    // 一次性取走 committed_text 和 commit_source，清空内部状态
    std::pair<std::string, CommitSource> commit_with_source();

    // Punctuation state (not cleared on reset() — persists across compositions)
    std::unordered_map<std::string, bool> pair_open;   // Quote alternation state
    std::unordered_map<std::string, int> alt_index;    // Alternatives rotation state
    char last_committed_char = '\0';                    // Digit separator guard

private:
    struct PageHistoryEntry {
        int offset = 0;
        int visible_candidate_count = 0;
    };

    size_t preedit_cursor_from_end_ = 0;
    uint64_t preedit_revision_ = 0;
    std::vector<PageHistoryEntry> previous_pages_;
    int highlight_count_after_page_change_ = 0;
    CommitSource commit_source_ = CommitSource::kRawCode;
    Candidate committed_candidate_;
    std::string committed_candidate_code_;
    bool has_committed_candidate_ = false;
    CompositionKind composition_kind_ = CompositionKind::kIme;
    std::optional<CompositionOrigin> composition_origin_;
};

} // namespace cxxime

#endif // CXXIME_CONTEXT_H_
