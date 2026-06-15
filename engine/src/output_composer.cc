// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/output_composer.h>
#include <cctype>

#ifndef VK_0
#define VK_0 0x30
#define VK_9 0x39
#define VK_A 0x41
#define VK_Z 0x5A
#define VK_SPACE 0x20
#define VK_RETURN 0x0D
#endif

namespace cxxime {

bool OutputComposer::intercept_key(const KeyEvent& event, const OutputOptions& opts,
                                   bool good_old_caps_lock, std::string& committed_text) {
    // 1. key-up: don't intercept
    if (event.is_key_up)
        return false;

    // 2. Chinese mode: don't intercept (letters→pinyin, digits→candidate selection)
    if (opts.chinese_mode)
        return false;

    // 3. Not full-width: don't intercept
    if (!opts.full_shape)
        return false;

    uint32_t vk = event.keycode;

    // 4. Shift+digit: don't intercept (produces punctuation like !@#, needs ToUnicode)
    if (event.is_shift() && vk >= VK_0 && vk <= VK_9)
        return false;

    // 5. Letters/space/enter: don't intercept (Engine handles, transform does full-width)
    if ((vk >= VK_A && vk <= VK_Z) || vk == VK_SPACE || vk == VK_RETURN)
        return false;

    // 6. Digit keys (no Shift): map VK to '0'-'9', convert to full-width
    if (vk >= VK_0 && vk <= VK_9) {
        char ch = static_cast<char>(vk);  // VK_0='0', VK_9='9'
        committed_text = to_full_width(ch);
        return true;
    }

    // Other keys (punctuation etc.): not supported in v1
    return false;
}

std::string OutputComposer::transform(const std::string& text, const OutputOptions& opts,
                                      CommitSource source, bool good_old_caps_lock) {
    // Candidate text: no conversion at all
    if (source == CommitSource::kCandidate)
        return text;

    if (text.empty())
        return text;

    // kRawCode: apply Caps Lock inversion, then full-width conversion
    std::string result;
    result.reserve(text.size() * 3);  // worst case: every byte becomes 3 bytes

    bool do_caps = opts.caps_lock && !good_old_caps_lock;
    bool do_full = opts.full_shape;

    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);

        // Skip non-ASCII bytes (UTF-8 continuation bytes or leading bytes)
        if (ch >= 0x80) {
            result.push_back(static_cast<char>(ch));
            continue;
        }

        // Control characters (0x00-0x1F): pass through unchanged
        if (ch <= 0x1F) {
            result.push_back(static_cast<char>(ch));
            continue;
        }

        // ASCII range 0x20-0x7E: apply transformations
        char c = static_cast<char>(ch);

        // Step 1: Caps Lock inversion (a-z ↔ A-Z)
        if (do_caps) {
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            else if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }

        // Step 2: Full-width conversion
        if (do_full) {
            result += to_full_width(c);
        } else {
            result.push_back(c);
        }
    }

    return result;
}

std::string OutputComposer::to_full_width(char ch) {
    // ASCII 0x20 (space) → U+3000 (ideographic space)
    if (ch == 0x20) {
        return "\xe3\x80\x80";  // UTF-8 for U+3000
    }

    // ASCII 0x21-0x7E → U+FF01-U+FF5E (offset +0xFEE0)
    // UTF-8 encoding: 3 bytes per character
    // codepoint = ch + 0xFEE0
    uint32_t codepoint = static_cast<uint8_t>(ch) + 0xFEE0u;
    char buf[4];
    buf[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
    buf[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
    buf[3] = '\0';
    return std::string(buf, 3);
}

}  // namespace cxxime
