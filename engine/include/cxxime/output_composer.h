// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_OUTPUT_COMPOSER_H_
#define CXXIME_OUTPUT_COMPOSER_H_

#include <string>
#include <cxxime/output_options.h>
#include <cxxime/key_event.h>

namespace cxxime {

// Stateless pure-function collection for output transformation.
struct OutputComposer {
    // Key interception: in English + full-width mode, intercept digit keys
    // that Engine would otherwise REJECT.
    // Chinese mode: no interception (letters go to pinyin, digits for candidate selection).
    // Letters/space/enter: no interception (Engine handles them, transform does full-width).
    // Returns true if intercepted (committed_text is set), false otherwise.
    static bool intercept_key(const KeyEvent& event, const OutputOptions& opts,
                              bool good_old_caps_lock, std::string& committed_text);

    // Text transformation: apply Caps Lock inversion and full-width conversion
    // to committed text. CommitSource determines whether to convert:
    // kRawCode — apply Caps Lock + full-width; kCandidate — no conversion.
    static std::string transform(const std::string& text, const OutputOptions& opts,
                                 CommitSource source, bool good_old_caps_lock);

    // Convert a single ASCII character to full-width UTF-8.
    static std::string to_full_width(char ch);

private:
};

}  // namespace cxxime

#endif  // CXXIME_OUTPUT_COMPOSER_H_
