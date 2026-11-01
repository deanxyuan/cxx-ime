// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "custom_phrase_translation.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include <cxxime/custom_phrases.h>
#include <cxxime/translator.h>

namespace cxxime {

TranslationResult translate_with_custom_phrases(ITranslator& translator,
                                                const TranslationRequest& request,
                                                const CustomPhrases* phrases,
                                                std::size_t converted_text_bytes) {
    auto matches = phrases ? phrases->lookup(request.input) : std::vector<std::string>{};
    const std::size_t remaining =
        kCandidateTextCapacity -
        (std::min)(converted_text_bytes, static_cast<std::size_t>(kCandidateTextCapacity));
    matches.erase(std::remove_if(matches.begin(), matches.end(),
                                 [&](const std::string& text) { return text.size() >= remaining; }),
                  matches.end());
    if (matches.empty()) {
        return translator.translate(request);
    }
    // Query a prefix so phrase insertion and duplicate removal cannot shift page boundaries.
    TranslationRequest ordinary = request;
    ordinary.page_index = 0;
    ordinary.page_offset = 0;
    ordinary.page_size = request.page_offset + request.page_size + static_cast<int>(matches.size());
    TranslationResult translated = translator.translate(ordinary);
    const bool more = candidate_extent_may_continue(translated.extent,
                                                    static_cast<int>(translated.entries.size()));
    std::vector<CandidateEntry> combined;
    std::unordered_set<std::string> seen;
    for (const auto& text : matches) {
        Candidate candidate;
        candidate.text = text;
        candidate.code = request.input;
        candidate.source = CandidateSource::kCustomPhrase;
        candidate.origin = CandidateOrigin::kUser;
        auto entry = make_text_candidate_entry(std::move(candidate), request.input.size());
        entry.match_kind = CandidateMatchKind::kExact;
        combined.push_back(std::move(entry));
        seen.insert(text);
    }
    for (auto& entry : translated.entries) {
        if (seen.insert(entry.candidate.text).second) {
            combined.push_back(std::move(entry));
        }
    }
    TranslationResult result;
    result.status = translated.usable() ? translated.status : TranslationStatus::kStableDegraded;
    result.page_index = request.page_index;
    result.page_offset = request.page_offset;
    result.page_size = request.page_size;
    const int count = static_cast<int>(combined.size());
    const int end = (std::min)(count, request.page_offset + request.page_size);
    for (int index = request.page_offset; index < end; ++index) {
        result.entries.push_back(std::move(combined[index]));
    }
    result.extent = make_candidate_extent(count, end, more || !translated.usable());
    result.highlighted = result.entries.empty() ? -1 : 0;
    return result;
}

} // namespace cxxime
