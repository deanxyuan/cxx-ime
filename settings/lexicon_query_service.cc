// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "lexicon_query_service.h"

#include <cxxime/data_path.h>

namespace cxxime {
namespace settings {
namespace {

const char* dictionary_filename(SystemLexiconType type) {
    return type == SystemLexiconType::kWubi ? "wubi86.dict.bin" : "pinyin.dict.bin";
}

const char* reverse_index_filename(SystemLexiconType type) {
    return type == SystemLexiconType::kWubi ? "wubi86.reverse.idx" : "pinyin.reverse.idx";
}

} // namespace

SystemLexiconQueryResult LexiconQueryService::query(SystemLexiconType type,
                                                    LexiconSearchKind search_kind,
                                                    std::string_view query, std::size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    SystemLexiconQueryResult result;
    if (!ensure_open(type)) {
        result.error = inspector_.last_error();
        return result;
    }
    result.available = true;
    if (search_kind == LexiconSearchKind::kText) {
        result.entries = inspector_.query_text(query, SystemLexiconTextMatch::kPrefix, limit);
    } else if (search_kind == LexiconSearchKind::kCode) {
        result.entries = inspector_.query_code_prefix(query, limit);
    }
    if (!inspector_.last_error().empty()) {
        result.available = false;
        result.error = inspector_.last_error();
        result.entries.clear();
    }
    return result;
}

SystemLexiconQueryResult LexiconQueryService::query_exact_text(SystemLexiconType type,
                                                               std::string_view text,
                                                               std::size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    SystemLexiconQueryResult result;
    if (!ensure_open(type)) {
        result.error = inspector_.last_error();
        return result;
    }
    result.available = true;
    result.entries = inspector_.query_text(text, SystemLexiconTextMatch::kExact, limit);
    if (!inspector_.last_error().empty()) {
        result.available = false;
        result.error = inspector_.last_error();
        result.entries.clear();
    }
    return result;
}

std::vector<std::string> LexiconQueryService::suggest_codes(SystemLexiconType type,
                                                            std::string_view text,
                                                            std::size_t limit, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensure_open(type)) {
        if (error) {
            *error = inspector_.last_error();
        }
        return {};
    }
    if (error) {
        error->clear();
    }
    auto suggestions = generate_lexicon_code_suggestions(
        type, text,
        [&](std::string_view item, std::size_t query_limit) {
            return inspector_.query_text(item, SystemLexiconTextMatch::kExact, query_limit);
        },
        limit);
    if (!inspector_.last_error().empty()) {
        if (error) {
            *error = inspector_.last_error();
        }
        return {};
    }
    return suggestions;
}

bool LexiconQueryService::ensure_open(SystemLexiconType type) {
    if (has_open_type_ && open_type_ == type && inspector_.is_open()) {
        return true;
    }
    inspector_.close();
    has_open_type_ = false;
    if (!inspector_.open(type, data_path(dictionary_filename(type)),
                         data_path(reverse_index_filename(type)))) {
        return false;
    }
    open_type_ = type;
    has_open_type_ = true;
    return true;
}

} // namespace settings
} // namespace cxxime
