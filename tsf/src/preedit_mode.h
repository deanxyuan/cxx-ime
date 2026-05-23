// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_PREEDIT_MODE_H_
#define CXXIME_TSF_PREEDIT_MODE_H_

#include <string>
#include <vector>

namespace cxxime_tsf {

struct PreeditDecision {
    std::wstring inline_text;
    bool show_preedit_in_popup;
    bool start_composition;
};

inline PreeditDecision decide_preedit(
    bool inline_preedit,
    const std::string& preedit_type,
    const std::wstring& preedit,
    const std::vector<std::wstring>& candidates) {

    PreeditDecision d;
    d.start_composition = false;

    if (inline_preedit) {
        d.start_composition = true;
        if (preedit_type == "preview" && !candidates.empty()) {
            d.inline_text = candidates[0];
            d.show_preedit_in_popup = true;
        } else if (preedit_type == "preview_all" && !candidates.empty()) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (i > 0) d.inline_text += L" ";
                d.inline_text += candidates[i];
            }
            d.show_preedit_in_popup = true;
        } else {
            d.inline_text = preedit;
            d.show_preedit_in_popup = false;
        }
    } else {
        d.inline_text.clear();
        d.show_preedit_in_popup = true;
        d.start_composition = false;
    }

    return d;
}

} // namespace cxxime_tsf

#endif // CXXIME_TSF_PREEDIT_MODE_H_
