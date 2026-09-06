// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_PREEDIT_MODE_H_
#define CXXIME_TSF_PREEDIT_MODE_H_

#include <cstddef>
#include <string>
#include <vector>

namespace cxxime_tsf {

struct PreeditDecision {
    std::wstring inline_text;   // Text written to TSF composition (app inline area)
    size_t inline_cursor = 0;   // UTF-16 offset within inline_text
    size_t inline_converted_prefix = 0;
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
//   composition shows raw input inline and hides duplicate popup preedit.
//   preview shows the highlighted candidate after the converted prefix and keeps logical
//   preedit in the popup. Without a candidate, an existing converted prefix keeps the active
//   raw suffix inline; otherwise the popup remains the only raw-input surface.
inline PreeditDecision decide_preedit(bool inline_preedit, const std::string& preedit_type,
                                      const std::wstring& preedit, size_t preedit_cursor,
                                      const std::vector<std::wstring>& candidates,
                                      size_t converted_prefix = 0,
                                      int highlighted = 0) {
    PreeditDecision d;
    preedit_cursor = clamp_preedit_cursor(preedit_cursor, preedit.size());
    converted_prefix = clamp_preedit_cursor(converted_prefix, preedit_cursor);

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
                d.inline_text = preedit.substr(0, converted_prefix) + candidates[selected];
                d.inline_cursor = d.inline_text.size();
                d.inline_converted_prefix = converted_prefix;
            } else if (converted_prefix > 0) {
                d.inline_text = preedit;
                d.inline_cursor = preedit_cursor;
                d.inline_converted_prefix = converted_prefix;
            }
        } else {
            d.inline_text = preedit;
            d.inline_cursor = preedit_cursor;
            d.inline_converted_prefix = converted_prefix;
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
