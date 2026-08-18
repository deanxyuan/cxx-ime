// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "lexicon_panel_model.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cxxime {
namespace settings {
namespace {

struct CodeChoice {
    std::string code;
    int score = 0;
};

bool decode_utf8_character(std::string_view text, std::size_t offset, std::size_t* length,
                           std::uint32_t* code_point) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) {
        *length = 1;
        *code_point = first;
        return true;
    }

    std::size_t expected = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if ((first & 0xe0) == 0xc0) {
        expected = 2;
        value = first & 0x1f;
        minimum = 0x80;
    } else if ((first & 0xf0) == 0xe0) {
        expected = 3;
        value = first & 0x0f;
        minimum = 0x800;
    } else if ((first & 0xf8) == 0xf0) {
        expected = 4;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        return false;
    }
    if (offset + expected > text.size()) {
        return false;
    }
    for (std::size_t index = 1; index < expected; ++index) {
        const auto continuation = static_cast<unsigned char>(text[offset + index]);
        if ((continuation & 0xc0) != 0x80) {
            return false;
        }
        value = (value << 6) | (continuation & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        return false;
    }
    *length = expected;
    *code_point = value;
    return true;
}

bool is_cjk(std::uint32_t code_point) {
    return (code_point >= 0x3400 && code_point <= 0x4dbf) ||
           (code_point >= 0x4e00 && code_point <= 0x9fff) ||
           (code_point >= 0x20000 && code_point <= 0x3134f);
}

std::vector<std::string> split_utf8_text(std::string_view text) {
    std::vector<std::string> characters;
    for (std::size_t offset = 0; offset < text.size();) {
        std::size_t length = 0;
        std::uint32_t code_point = 0;
        if (!decode_utf8_character(text, offset, &length, &code_point) || !is_cjk(code_point)) {
            return {};
        }
        characters.emplace_back(text.substr(offset, length));
        offset += length;
    }
    return characters;
}

std::string entry_key(std::string_view text, std::string_view code) {
    std::string key;
    key.reserve(text.size() + code.size() + 1);
    key.append(text);
    key.push_back('\0');
    key.append(code);
    return key;
}

std::vector<CodeChoice> ranked_codes(std::vector<SystemLexiconEntry> entries, std::size_t limit,
                                     bool prefer_longest = false) {
    std::size_t preferred_length = 0;
    if (prefer_longest) {
        for (const auto& entry : entries) {
            preferred_length = (std::max)(preferred_length, entry.code.size());
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const SystemLexiconEntry& left, const SystemLexiconEntry& right) {
                         if (left.frequency != right.frequency) {
                             return left.frequency > right.frequency;
                         }
                         if (left.code != right.code) {
                             return left.code < right.code;
                         }
                         return left.entry_id < right.entry_id;
                     });

    std::vector<CodeChoice> choices;
    std::unordered_set<std::string> seen;
    for (const auto& entry : entries) {
        if (entry.code.empty() || (prefer_longest && entry.code.size() != preferred_length) ||
            !seen.insert(entry.code).second) {
            continue;
        }
        choices.push_back({entry.code, entry.frequency});
        if (choices.size() == limit) {
            break;
        }
    }
    return choices;
}

std::string wubi_phrase_code(const std::vector<CodeChoice>& choices) {
    const std::size_t count = choices.size();
    if (count == 1) {
        return choices[0].code;
    }
    if (count == 2) {
        return choices[0].code.substr(0, 2) + choices[1].code.substr(0, 2);
    }
    if (count == 3) {
        return choices[0].code.substr(0, 1) + choices[1].code.substr(0, 1) +
               choices[2].code.substr(0, 2);
    }
    return choices[0].code.substr(0, 1) + choices[1].code.substr(0, 1) +
           choices[2].code.substr(0, 1) + choices[count - 1].code.substr(0, 1);
}

std::string combine_codes(SystemLexiconType type, const std::vector<CodeChoice>& choices) {
    if (type == SystemLexiconType::kWubi) {
        return wubi_phrase_code(choices);
    }
    std::string result;
    for (const auto& choice : choices) {
        result += choice.code;
    }
    return result;
}

} // namespace

LexiconSearchKind classify_lexicon_search(std::string_view query) {
    if (query.empty()) {
        return LexiconSearchKind::kEmpty;
    }

    bool text_only = true;
    bool code_only = true;
    for (std::size_t offset = 0; offset < query.size();) {
        std::size_t length = 0;
        std::uint32_t code_point = 0;
        if (!decode_utf8_character(query, offset, &length, &code_point)) {
            return LexiconSearchKind::kInvalid;
        }
        if (!is_cjk(code_point)) {
            text_only = false;
        }
        if (code_point > 0x7f || !((code_point >= 'a' && code_point <= 'z') || code_point == ':' ||
                                    code_point == '\'')) {
            code_only = false;
        }
        offset += length;
    }
    if (text_only) {
        return LexiconSearchKind::kText;
    }
    return code_only ? LexiconSearchKind::kCode : LexiconSearchKind::kInvalid;
}

bool should_prefill_new_lexicon_entry(LexiconSearchKind search_kind, std::string_view query,
                                      bool preserve_editor, bool system_query_available,
                                      const std::vector<LexiconPanelEntry>& results) {
    if (search_kind != LexiconSearchKind::kText || preserve_editor || !system_query_available) {
        return false;
    }
    return std::none_of(results.begin(), results.end(),
                        [&](const LexiconPanelEntry& entry) { return entry.text == query; });
}

std::vector<LexiconPanelEntry>
merge_lexicon_entries(const std::vector<SystemLexiconEntry>& system_entries,
                      const std::vector<UserDictEntryInfo>& user_entries,
                      const std::vector<UserDictEntryInfo>& disabled_entries) {
    std::unordered_set<std::string> disabled_texts;
    for (const auto& entry : disabled_entries) {
        disabled_texts.insert(entry.text);
    }

    std::vector<LexiconPanelEntry> rows;
    std::unordered_map<std::string, std::size_t> row_by_key;
    rows.reserve(system_entries.size() + user_entries.size());
    for (const auto& entry : system_entries) {
        const std::string key = entry_key(entry.text, entry.code);
        auto existing = row_by_key.find(key);
        if (existing != row_by_key.end()) {
            rows[existing->second].frequency =
                (std::max)(rows[existing->second].frequency, entry.frequency);
            continue;
        }
        LexiconPanelEntry row;
        row.text = entry.text;
        row.code = entry.code;
        row.frequency = entry.frequency;
        row.has_system_source = true;
        row.system_disabled = disabled_texts.find(entry.text) != disabled_texts.end();
        row_by_key.emplace(key, rows.size());
        rows.push_back(std::move(row));
    }
    for (const auto& entry : user_entries) {
        const std::string key = entry_key(entry.text, entry.code);
        auto existing = row_by_key.find(key);
        if (existing != row_by_key.end()) {
            rows[existing->second].has_user_source = true;
            continue;
        }
        LexiconPanelEntry row;
        row.text = entry.text;
        row.code = entry.code;
        row.frequency = entry.frequency;
        row.has_user_source = true;
        row_by_key.emplace(key, rows.size());
        rows.push_back(std::move(row));
    }
    // User entries must remain visible when a broad system prefix fills the page.
    std::stable_partition(rows.begin(), rows.end(),
                          [](const LexiconPanelEntry& row) { return row.has_user_source; });
    return rows;
}

std::vector<std::string> generate_lexicon_code_suggestions(SystemLexiconType type,
                                                           std::string_view text,
                                                           const ExactLexiconLookup& lookup,
                                                           std::size_t limit) {
    if (text.empty() || limit == 0 || !lookup) {
        return {};
    }

    limit = (std::min)(limit, static_cast<std::size_t>(8));
    const std::vector<std::string> characters = split_utf8_text(text);
    const bool require_complete_wubi_code =
        type == SystemLexiconType::kWubi && characters.size() == 1;
    const auto exact = ranked_codes(lookup(text, 128), limit, require_complete_wubi_code);
    if (!exact.empty()) {
        std::vector<std::string> result;
        result.reserve(exact.size());
        for (const auto& choice : exact) {
            result.push_back(choice.code);
        }
        return result;
    }

    if (characters.empty()) {
        return {};
    }

    std::vector<std::vector<CodeChoice>> per_character;
    per_character.reserve(characters.size());
    for (const auto& character : characters) {
        auto choices =
            ranked_codes(lookup(character, 128), limit, type == SystemLexiconType::kWubi);
        if (choices.empty()) {
            return {};
        }
        per_character.push_back(std::move(choices));
    }

    std::vector<std::vector<CodeChoice>> combinations(1);
    for (const auto& character_choices : per_character) {
        std::vector<std::vector<CodeChoice>> expanded;
        for (const auto& combination : combinations) {
            for (const auto& choice : character_choices) {
                auto next = combination;
                next.push_back(choice);
                expanded.push_back(std::move(next));
            }
        }
        std::stable_sort(
            expanded.begin(), expanded.end(),
            [](const std::vector<CodeChoice>& left, const std::vector<CodeChoice>& right) {
                std::int64_t left_score = 0;
                std::int64_t right_score = 0;
                for (const auto& choice : left) {
                    left_score += choice.score;
                }
                for (const auto& choice : right) {
                    right_score += choice.score;
                }
                return left_score > right_score;
            });
        if (expanded.size() > limit) {
            expanded.resize(limit);
        }
        combinations = std::move(expanded);
    }

    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& combination : combinations) {
        std::string code = combine_codes(type, combination);
        if (!code.empty() && seen.insert(code).second) {
            result.push_back(std::move(code));
            if (result.size() == limit) {
                break;
            }
        }
    }
    return result;
}

LexiconSelectionSummary
summarize_lexicon_selection(const std::vector<LexiconPanelEntry>& rows,
                            const std::vector<std::size_t>& selected_indices) {
    LexiconSelectionSummary summary;
    for (std::size_t index : selected_indices) {
        if (index >= rows.size()) {
            continue;
        }
        if (summary.selected_count == 0) {
            summary.first_index = index;
        }
        ++summary.selected_count;
        if (rows[index].has_user_source) {
            ++summary.deletable_count;
        }
    }
    return summary;
}

std::vector<LexiconEntryKey>
selected_lexicon_entry_keys(const std::vector<LexiconPanelEntry>& rows,
                            const std::vector<std::size_t>& selected_indices) {
    std::vector<LexiconEntryKey> entries;
    entries.reserve(selected_indices.size());
    for (std::size_t index : selected_indices) {
        if (index < rows.size()) {
            entries.push_back({rows[index].text, rows[index].code});
        }
    }
    return entries;
}

std::vector<LexiconEntryKey>
selected_user_entry_keys(const std::vector<LexiconPanelEntry>& rows,
                         const std::vector<std::size_t>& selected_indices) {
    std::vector<LexiconEntryKey> entries;
    entries.reserve(selected_indices.size());
    for (std::size_t index : selected_indices) {
        if (index < rows.size() && rows[index].has_user_source) {
            entries.push_back({rows[index].text, rows[index].code});
        }
    }
    return entries;
}

} // namespace settings
} // namespace cxxime
