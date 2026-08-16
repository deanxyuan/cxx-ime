// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pinyin_code_paths.h"

#include <utility>

namespace cxxime {
namespace settings {
namespace {

bool is_lowercase_letter(char character) { return character >= 'a' && character <= 'z'; }

} // namespace

std::vector<std::string> PinyinCodePaths::parse(std::string_view code) {
    const bool has_explicit_boundary =
        code.find(':') != std::string_view::npos || code.find('\'') != std::string_view::npos;
    std::vector<std::string> tokens;
    std::string token;
    bool trailing_separator = false;
    for (const char character : code) {
        if (is_lowercase_letter(character)) {
            token.push_back(character);
            trailing_separator = false;
            continue;
        }
        if ((character != ':' && character != '\'') || token.empty()) {
            return {};
        }
        tokens.push_back(std::move(token));
        token = {};
        trailing_separator = true;
    }
    if (token.empty() && !trailing_separator) {
        return {};
    }
    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }

    std::vector<std::string> normalized(1);
    for (const auto& item : tokens) {
        auto paths = segmentor_.segment(item);
        if (paths.empty()) {
            return {};
        }
        if (has_explicit_boundary) {
            paths.resize(1);
        }
        std::vector<std::string> expanded;
        for (const auto& prefix : normalized) {
            for (const auto& path : paths) {
                std::string value = prefix;
                for (const auto& syllable : path) {
                    if (!value.empty()) {
                        value.push_back(':');
                    }
                    value.append(syllable);
                }
                expanded.push_back(std::move(value));
            }
        }
        normalized = std::move(expanded);
    }
    if (trailing_separator) {
        for (auto& value : normalized) {
            value.push_back(':');
        }
    }
    return normalized;
}

} // namespace settings
} // namespace cxxime
