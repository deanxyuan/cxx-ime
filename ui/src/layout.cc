// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cxxime/candidate_window.h>

namespace cxxime {

// Layout calculation utilities
int calculate_candidate_width(const std::string& text) {
    // Rough estimate: 2 bytes per CJK char, 1 byte per ASCII
    int width = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((unsigned char)text[i] >= 0x80) {
            width += 14; // CJK char width
            // Skip continuation bytes
            while (i + 1 < text.size() && ((unsigned char)text[i + 1] & 0xC0) == 0x80)
                ++i;
        } else {
            width += 8; // ASCII char width
        }
    }
    return width + 20; // padding
}

int calculate_panel_width(const CandidatePage& page) {
    int total = 20; // left + right padding
    for (const auto& c : page.candidates) {
        total += calculate_candidate_width(c.text);
    }
    return total;
}

} // namespace cxxime
