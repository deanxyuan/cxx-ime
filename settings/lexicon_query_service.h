// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LEXICON_QUERY_SERVICE_H_
#define CXXIME_LEXICON_QUERY_SERVICE_H_

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "lexicon_panel_model.h"
#include "system_lexicon_inspector.h"

namespace cxxime {
namespace settings {

struct SystemLexiconQueryResult {
    bool available = false;
    std::string error;
    std::vector<SystemLexiconEntry> entries;
};

class LexiconQueryService {
public:
    SystemLexiconQueryResult query(SystemLexiconType type, LexiconSearchKind search_kind,
                                   std::string_view query, std::size_t limit);
    SystemLexiconQueryResult query_exact_text(SystemLexiconType type, std::string_view text,
                                              std::size_t limit);
    std::vector<std::string> suggest_codes(SystemLexiconType type, std::string_view text,
                                           std::size_t limit, std::string* error);

private:
    bool ensure_open(SystemLexiconType type);

    std::mutex mutex_;
    SystemLexiconInspector inspector_;
    SystemLexiconType open_type_ = SystemLexiconType::kPinyin;
    bool has_open_type_ = false;
};

} // namespace settings
} // namespace cxxime

#endif // CXXIME_LEXICON_QUERY_SERVICE_H_
