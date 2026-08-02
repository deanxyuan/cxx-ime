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
    bool show_preedit_in_popup; // Whether candidate window shows raw input
    bool start_composition;     // Whether to start TSF composition
};

inline size_t clamp_preedit_cursor(size_t cursor, size_t preedit_length) {
    return (cursor < preedit_length) ? cursor : preedit_length;
}

// Decide what to show inline vs. in the candidate window popup.
//
// inline_preedit=false: no TSF composition, candidate window shows raw pinyin.
//   preedit_type is ignored.
// inline_preedit=true: TSF composition active, candidate window hides preedit.
//   preedit_type controls what goes inline (composition/preview).
inline PreeditDecision decide_preedit(bool inline_preedit, const std::string& preedit_type,
                                      const std::wstring& preedit, size_t preedit_cursor,
                                      const std::vector<std::wstring>& candidates) {
    PreeditDecision d;
    preedit_cursor = clamp_preedit_cursor(preedit_cursor, preedit.size());

    if (!inline_preedit) {
        d.inline_text.clear();
        d.inline_cursor = 0;
        d.show_preedit_in_popup = true;
        d.start_composition = false;
    } else {
        d.start_composition = true;
        d.show_preedit_in_popup = false;

        if (preedit_type == "preview" && !candidates.empty() && preedit_cursor == preedit.size()) {
            d.inline_text = candidates[0];
            d.inline_cursor = d.inline_text.size();
        } else {
            d.inline_text = preedit;
            d.inline_cursor = preedit_cursor;
        }
    }

    return d;
}

} // namespace cxxime_tsf

#endif // CXXIME_TSF_PREEDIT_MODE_H_
