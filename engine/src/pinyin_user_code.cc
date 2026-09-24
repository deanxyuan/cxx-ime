// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/pinyin_user_code.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

#include <cxxime/pinyin_resource.h>
#include <cxxime/segmentor.h>
#include <cxxime/user_dict_validation.h>

namespace cxxime {
namespace {

std::vector<std::string> split_syllables(std::string_view syllables) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < syllables.size()) {
        const std::size_t separator = syllables.find(':', start);
        result.emplace_back(syllables.substr(start, separator - start));
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return result;
}

std::string compact_code(const std::vector<std::string>& syllables) {
    std::string result;
    for (const auto& syllable : syllables) {
        result.append(syllable);
    }
    return result;
}

std::string joined_syllables(const std::vector<std::string>& syllables) {
    std::string result;
    for (const auto& syllable : syllables) {
        if (!result.empty()) {
            result.push_back(':');
        }
        result.append(syllable);
    }
    return result;
}

PinyinSegmentor& pinyin_segmentor() {
    static PinyinSegmentor segmentor;
    return segmentor;
}

bool complete_pinyin_syllables(const std::string& code, std::string* syllables) {
    syllables->clear();
    bool found = false;
    bool ambiguous = false;
    for (const auto& path : pinyin_segmentor().segment(code)) {
        bool complete = true;
        for (const auto& syllable : path) {
            if (!pinyin_segmentor().is_syllable(syllable)) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            continue;
        }
        const std::string joined = joined_syllables(path);
        if (!found) {
            *syllables = joined;
            found = true;
        } else if (*syllables != joined) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        syllables->clear();
    }
    return found;
}

bool is_complete_normal_path(const SegmentedPath& path, std::size_t input_size) {
    const std::size_t consumed =
        std::accumulate(path.input_lengths.begin(), path.input_lengths.end(), std::size_t{0});
    return consumed == input_size && path.input_lengths.size() == path.syllables.size() &&
           std::all_of(path.input_lengths.begin(), path.input_lengths.end(),
                       [](uint16_t length) { return length == 2; }) &&
           path.spelling_types.size() == path.syllables.size() &&
           std::all_of(path.spelling_types.begin(), path.spelling_types.end(),
                       [](uint8_t type) { return type == kNormalSpelling; });
}

} // namespace

bool is_canonical_pinyin_user_code(std::string_view code, std::string_view syllables) {
    const std::string code_string(code);
    if (!is_valid_user_dict_code(code_string)) {
        return false;
    }

    if (syllables.empty()) {
        std::string normalized_syllables;
        return complete_pinyin_syllables(code_string, &normalized_syllables);
    }
    const std::string syllables_string(syllables);
    if (!is_valid_user_dict_syllables(syllables_string)) {
        return false;
    }
    const auto path = split_syllables(syllables);
    if (compact_code(path) != code_string) {
        return false;
    }
    return std::all_of(path.begin(), path.end(), [&](const std::string& syllable) {
        return pinyin_segmentor().is_syllable(syllable);
    });
}

bool canonicalize_pinyin_user_code(std::string_view input, PinyinSchemeKind input_scheme,
                                   const PinyinResourceSet* pinyin_resources, std::string* code,
                                   std::string* syllables) {
    if (!code || !syllables || input.empty()) {
        return false;
    }
    code->clear();
    syllables->clear();
    const std::string input_string(input);
    if (is_valid_user_dict_code(input_string) &&
        complete_pinyin_syllables(input_string, syllables)) {
        *code = input_string;
        return true;
    }
    if (input_scheme != PinyinSchemeKind::kShuangpin || !pinyin_resources ||
        std::any_of(input.begin(), input.end(),
                    [](char value) { return (value < 'a' || value > 'z') && value != ';'; })) {
        return false;
    }

    SyllabifierOptions options;
    options.collect_path_metadata = true;
    const SegmentResult segmented =
        pinyin_resources->segment(std::string(input), nullptr, options);
    std::string normalized_code;
    std::string normalized_syllables;
    bool ambiguous_syllables = false;
    for (const auto& path : segmented.paths) {
        if (!is_complete_normal_path(path, input.size())) {
            continue;
        }
        const std::string path_code = compact_code(path.syllables);
        if (normalized_code.empty()) {
            normalized_code = path_code;
            normalized_syllables = joined_syllables(path.syllables);
        } else if (normalized_code != path_code) {
            return false;
        } else if (normalized_syllables != joined_syllables(path.syllables)) {
            ambiguous_syllables = true;
        }
    }
    if (normalized_code.empty() || !is_canonical_pinyin_user_code(normalized_code)) {
        return false;
    }
    *code = std::move(normalized_code);
    if (!ambiguous_syllables) {
        *syllables = std::move(normalized_syllables);
    }
    return true;
}

} // namespace cxxime
