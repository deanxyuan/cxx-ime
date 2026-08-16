// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/user_dict_validation.h>

#include <cstddef>

#include <cxxime/input_limits.h>

namespace cxxime {
namespace {

bool is_continuation(unsigned char value) { return value >= 0x80 && value <= 0xbf; }

bool is_valid_utf8(const std::string& value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char lead = static_cast<unsigned char>(value[index]);
        if (lead <= 0x7f) {
            ++index;
            continue;
        }
        if (lead >= 0xc2 && lead <= 0xdf) {
            if (index + 1 >= value.size() ||
                !is_continuation(static_cast<unsigned char>(value[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }
        if (lead >= 0xe0 && lead <= 0xef) {
            if (index + 2 >= value.size()) {
                return false;
            }
            const unsigned char second = static_cast<unsigned char>(value[index + 1]);
            const unsigned char third = static_cast<unsigned char>(value[index + 2]);
            bool second_is_valid = is_continuation(second);
            if (lead == 0xe0) {
                second_is_valid = second >= 0xa0 && second <= 0xbf;
            } else if (lead == 0xed) {
                second_is_valid = second >= 0x80 && second <= 0x9f;
            }
            if (!second_is_valid || !is_continuation(third)) {
                return false;
            }
            index += 3;
            continue;
        }
        if (lead >= 0xf0 && lead <= 0xf4) {
            if (index + 3 >= value.size()) {
                return false;
            }
            const unsigned char second = static_cast<unsigned char>(value[index + 1]);
            bool second_is_valid = is_continuation(second);
            if (lead == 0xf0) {
                second_is_valid = second >= 0x90 && second <= 0xbf;
            } else if (lead == 0xf4) {
                second_is_valid = second >= 0x80 && second <= 0x8f;
            }
            if (!second_is_valid ||
                !is_continuation(static_cast<unsigned char>(value[index + 2])) ||
                !is_continuation(static_cast<unsigned char>(value[index + 3]))) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

bool is_valid_user_dict_text(const std::string& text) {
    if (text.empty() || text.size() >= kCandidateTextCapacity || !is_valid_utf8(text)) {
        return false;
    }
    for (unsigned char value : text) {
        if (value < 0x20 || value == 0x7f) {
            return false;
        }
    }
    return true;
}

bool is_valid_user_dict_code(const std::string& code) {
    if (code.empty() || code.size() > kMaxInputCodeLength) {
        return false;
    }
    for (char value : code) {
        if (value < 'a' || value > 'z') {
            return false;
        }
    }
    return true;
}

bool is_valid_user_dict_syllables(const std::string& syllables) {
    if (syllables.empty()) {
        return true;
    }
    std::size_t letter_count = 0;
    bool previous_was_separator = true;
    for (char value : syllables) {
        if (value == ':') {
            if (previous_was_separator) {
                return false;
            }
            previous_was_separator = true;
            continue;
        }
        if (value < 'a' || value > 'z' || ++letter_count > kMaxInputCodeLength) {
            return false;
        }
        previous_was_separator = false;
    }
    return !previous_was_separator;
}

bool is_valid_user_dict_entry(const std::string& text, const std::string& code,
                              const std::string& syllables) {
    return is_valid_user_dict_text(text) && is_valid_user_dict_code(code) &&
           is_valid_user_dict_syllables(syllables);
}

} // namespace cxxime
