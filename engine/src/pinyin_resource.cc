// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/pinyin_resource.h>

#include <utility>

namespace cxxime {

PinyinResourceSet::PinyinResourceSet(std::string scheme_id, PinyinSchemeKind kind)
    : scheme_id_(std::move(scheme_id))
    , kind_(kind)
    , syllabifier_(spellings_) {}

std::shared_ptr<const PinyinResourceSet>
PinyinResourceSet::create(std::string scheme_id, PinyinSchemeKind kind,
                          const std::string& spellings_path,
                          PinyinSpellingRequirement requirement) {
    if (kind == PinyinSchemeKind::kShuangpin &&
        requirement == PinyinSpellingRequirement::kOptionalForFullPinyin) {
        return nullptr;
    }

    std::unique_ptr<PinyinResourceSet> resources(new PinyinResourceSet(std::move(scheme_id), kind));
    if (!spellings_path.empty() && resources->spellings_.load(spellings_path) &&
        resources->spellings_.has_spellings()) {
        return std::shared_ptr<const PinyinResourceSet>(resources.release());
    }
    if (kind == PinyinSchemeKind::kFullPinyin &&
        requirement == PinyinSpellingRequirement::kOptionalForFullPinyin) {
        return std::shared_ptr<const PinyinResourceSet>(resources.release());
    }
    return nullptr;
}

SegmentResult PinyinResourceSet::segment(const std::string& input, const QueryDeadline* deadline,
                                         const SyllabifierOptions& options) const {
    auto effective = options;
    effective.prefer_complete_spans = kind_ == PinyinSchemeKind::kShuangpin;
    if (kind_ == PinyinSchemeKind::kShuangpin) {
        effective.enable_abbreviation = false;
    }
    return syllabifier_.segment(input, deadline, effective);
}

SegmentResult PinyinResourceSet::segment_graph(SyllableGraph graph, const QueryDeadline* deadline,
                                               bool collect_path_metadata) const {
    return syllabifier_.segment_graph(std::move(graph), deadline, collect_path_metadata);
}

SyllableGraph PinyinResourceSet::build_graph(const std::string& input,
                                             const SyllabifierOptions& options) const {
    auto effective = options;
    effective.prefer_complete_spans = kind_ == PinyinSchemeKind::kShuangpin;
    return syllabifier_.build_graph(input, effective);
}

bool PinyinResourceSet::has_fuzzy_path(const std::string& input,
                                       const SyllabifierOptions& options) const {
    auto effective = options;
    effective.prefer_complete_spans = kind_ == PinyinSchemeKind::kShuangpin;
    if (kind_ == PinyinSchemeKind::kShuangpin) {
        effective.enable_abbreviation = false;
    }
    return syllabifier_.has_fuzzy_path(input, effective);
}

std::vector<SpellingMatch> PinyinResourceSet::prefix_search(std::string_view input,
                                                            bool enable_fuzzy) const {
    return spellings_.prefix_search(input, enable_fuzzy);
}

std::vector<SpellingMatch> PinyinResourceSet::completion_search(std::string_view input,
                                                                bool enable_fuzzy) const {
    return spellings_.completion_search(input, enable_fuzzy);
}

} // namespace cxxime
