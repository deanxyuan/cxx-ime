// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_PREEDIT_MODE_H_
#define CXXIME_TSF_PREEDIT_MODE_H_

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cxxime_tsf {

struct PreeditText {
    std::wstring text;
    size_t cursor = 0;
    size_t converted_prefix = 0;
    size_t focused_start = 0;
    size_t focused_end = 0;
};

struct PreeditDecision {
    std::wstring inline_text;   // Text written to TSF composition (app inline area)
    size_t inline_cursor = 0;   // UTF-16 offset within inline_text
    size_t inline_converted_prefix = 0;
    size_t inline_focused_start = 0;
    size_t inline_focused_end = 0;
    bool inline_focus_converted = false;
    // Logical text to retain if the host ends a composition containing generated boundaries.
    std::optional<std::wstring> host_termination_text;
    bool show_preedit_in_popup; // Whether candidate window shows raw input
    bool start_composition;     // Whether to start TSF composition
};

inline size_t clamp_preedit_cursor(size_t cursor, size_t preedit_length) {
    return (cursor < preedit_length) ? cursor : preedit_length;
}

// Decide what to show inline vs. in the candidate window popup.
//
// inline_preedit=false: no TSF composition, candidate window shows raw input.
//   preedit_type is ignored.
// inline_preedit=true: TSF composition active.
//   composition shows the display preedit inline and hides duplicate popup preedit.
//   preview shows the highlighted candidate after the converted prefix and keeps logical
//   preedit in the popup. Without a candidate, an existing converted prefix keeps the active
//   raw suffix inline; otherwise the popup remains the only raw-input surface.
inline PreeditText normalize_preedit_text(PreeditText preedit) {
    preedit.cursor = clamp_preedit_cursor(preedit.cursor, preedit.text.size());
    preedit.converted_prefix = clamp_preedit_cursor(preedit.converted_prefix, preedit.cursor);
    if (preedit.focused_start == 0 && preedit.focused_end == 0) {
        preedit.focused_start = preedit.converted_prefix;
        preedit.focused_end = preedit.text.size();
    }
    preedit.focused_start =
        (std::max)(preedit.converted_prefix,
                   clamp_preedit_cursor(preedit.focused_start, preedit.text.size()));
    preedit.focused_end =
        (std::max)(preedit.focused_start,
                   clamp_preedit_cursor(preedit.focused_end, preedit.text.size()));
    return preedit;
}

inline PreeditDecision decide_preedit(bool inline_preedit, const std::string& preedit_type,
                                      PreeditText logical_preedit,
                                      PreeditText display_preedit,
                                      const std::vector<std::wstring>& candidates,
                                      int highlighted = 0,
                                      bool display_has_generated_boundaries = false) {
    PreeditDecision d;
    logical_preedit = normalize_preedit_text(std::move(logical_preedit));
    display_preedit = normalize_preedit_text(std::move(display_preedit));

    if (!inline_preedit) {
        d.inline_text.clear();
        d.inline_cursor = 0;
        d.show_preedit_in_popup = true;
        d.start_composition = false;
    } else {
        d.start_composition = true;
        const bool preview_mode = preedit_type == "preview";
        if (preview_mode) {
            if (!candidates.empty()) {
                const size_t selected =
                    highlighted >= 0 && static_cast<size_t>(highlighted) < candidates.size()
                        ? static_cast<size_t>(highlighted)
                        : 0;
                d.inline_text = logical_preedit.text.substr(
                    0, logical_preedit.converted_prefix) + candidates[selected];
                d.inline_cursor = d.inline_text.size();
                d.inline_converted_prefix = logical_preedit.converted_prefix;
                d.inline_focused_start = logical_preedit.converted_prefix;
                d.inline_focused_end = d.inline_text.size();
                d.inline_focus_converted = true;
            } else if (logical_preedit.converted_prefix > 0) {
                d.inline_text = logical_preedit.text;
                d.inline_cursor = logical_preedit.cursor;
                d.inline_converted_prefix = logical_preedit.converted_prefix;
                d.inline_focused_start = logical_preedit.focused_start;
                d.inline_focused_end = logical_preedit.focused_end;
            }
        } else {
            d.inline_text = display_preedit.text;
            d.inline_cursor = display_preedit.cursor;
            d.inline_converted_prefix = display_preedit.converted_prefix;
            d.inline_focused_start = display_preedit.focused_start;
            d.inline_focused_end = display_preedit.focused_end;
            if (display_has_generated_boundaries &&
                display_preedit.text != logical_preedit.text) {
                d.host_termination_text = logical_preedit.text;
            }
        }
        d.show_preedit_in_popup = preview_mode;
    }

    return d;
}

inline bool empty_composition_requires_placeholder(bool immersive_mode, bool composition_active,
                                                   const std::wstring& current_text,
                                                   const std::wstring& next_text) {
    if (!next_text.empty()) {
        return false;
    }
    return immersive_mode || (composition_active && !current_text.empty());
}

inline bool should_wait_for_composition_layout(bool empty_placeholder_active,
                                               bool tsf_caret_resolved,
                                               bool trusted_native_caret_resolved) {
    return empty_placeholder_active && !tsf_caret_resolved &&
        !trusted_native_caret_resolved;
}

inline bool should_defer_candidate_show(bool commit_continues, bool tsf_caret_resolved,
                                        bool trusted_native_caret_resolved) {
    return commit_continues || (!tsf_caret_resolved && !trusted_native_caret_resolved);
}

} // namespace cxxime_tsf

#endif // CXXIME_TSF_PREEDIT_MODE_H_
