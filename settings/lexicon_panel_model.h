// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LEXICON_PANEL_MODEL_H_
#define CXXIME_LEXICON_PANEL_MODEL_H_

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <cxxime/user_dict.h>

#include "system_lexicon_inspector.h"

namespace cxxime {
namespace settings {

enum class LexiconSearchKind {
    kEmpty,
    kText,
    kCode,
    kInvalid,
};

struct LexiconPanelEntry {
    std::string text;
    std::string code;
    int frequency = 0;
    bool has_system_source = false;
    bool has_user_source = false;
    bool system_disabled = false;
};

using ExactLexiconLookup =
    std::function<std::vector<SystemLexiconEntry>(std::string_view text, std::size_t limit)>;

LexiconSearchKind classify_lexicon_search(std::string_view query);

bool should_prefill_new_lexicon_entry(LexiconSearchKind search_kind, std::string_view query,
                                      bool preserve_editor, bool system_query_available,
                                      const std::vector<LexiconPanelEntry>& results);

std::vector<LexiconPanelEntry>
merge_lexicon_entries(const std::vector<SystemLexiconEntry>& system_entries,
                      const std::vector<UserDictEntryInfo>& user_entries,
                      const std::vector<UserDictEntryInfo>& disabled_entries);

std::vector<std::string> generate_lexicon_code_suggestions(SystemLexiconType type,
                                                           std::string_view text,
                                                           const ExactLexiconLookup& lookup,
                                                           std::size_t limit);

} // namespace settings
} // namespace cxxime

#endif // CXXIME_LEXICON_PANEL_MODEL_H_
