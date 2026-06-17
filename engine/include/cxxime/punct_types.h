// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PUNCT_TYPES_H_
#define CXXIME_PUNCT_TYPES_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cxxime {

enum class PunctType { COMMIT, PAIR, ALTERNATIVES };

// Three fields are mutually exclusive; which one is valid depends on `type`.
struct PunctEntry {
    PunctType type;
    std::string commit;                    // type == COMMIT
    std::vector<std::string> pair;         // type == PAIR: [left, right]
    std::vector<std::string> alternatives; // type == ALTERNATIVES
};

struct PunctMapping {
    std::unordered_map<std::string, PunctEntry> half_shape;
    std::unordered_map<std::string, PunctEntry> full_shape;
};

// VK code to character mapping (US QWERTY layout).
// Returns '\0' for unmapped VK codes.
inline char vk_to_char(uint32_t vk, bool shift) {
    switch (vk) {
    case 0xBE: return shift ? '>' : '.';   // VK_OEM_PERIOD
    case 0xBC: return shift ? '<' : ',';   // VK_OEM_COMMA
    case 0xBA: return shift ? ':' : ';';   // VK_OEM_1
    case 0xBF: return shift ? '?' : '/';   // VK_OEM_2
    case 0xDB: return shift ? '{' : '[';   // VK_OEM_4
    case 0xDC: return shift ? '|' : '\\';  // VK_OEM_5
    case 0xDD: return shift ? '}' : ']';   // VK_OEM_6
    case 0xDE: return shift ? '"' : '\'';  // VK_OEM_7
    case 0xBD: return shift ? '_' : '-';   // VK_OEM_MINUS
    case 0xBB: return shift ? '+' : '=';   // VK_OEM_PLUS
    // Digit keys: Shift produces symbols (!@#$%^&*())
    case 0x30: return shift ? ')' : '0';   // VK_0
    case 0x31: return shift ? '!' : '1';   // VK_1
    case 0x32: return shift ? '@' : '2';   // VK_2
    case 0x33: return shift ? '#' : '3';   // VK_3
    case 0x34: return shift ? '$' : '4';   // VK_4
    case 0x35: return shift ? '%' : '5';   // VK_5
    case 0x36: return shift ? '^' : '6';   // VK_6
    case 0x37: return shift ? '&' : '7';   // VK_7
    case 0x38: return shift ? '*' : '8';   // VK_8
    case 0x39: return shift ? '(' : '9';   // VK_9
    default:   return '\0';
    }
}

}  // namespace cxxime

#endif  // CXXIME_PUNCT_TYPES_H_
