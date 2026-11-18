// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <windows.h>

#include <cxxime/engine.h>
#include <cxxime/mixed_translator.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/query_trace.h>
#include <cxxime/translator.h>

#include "support/test_runtime.h"
#include "support/testutil.h"

namespace {

// Input spelling, canonical syllable, spelling type, and credibility adjustment.
using SpellingSpec = std::tuple<std::string, std::string, int, float>;

// Canonical syllables, candidate text, and dictionary frequency.
using DictionaryEntry = std::tuple<std::string, std::string, int>;

std::string temporary_file() {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    ASSERT_NE(GetTempPathA(MAX_PATH, directory), 0u);
    ASSERT_NE(GetTempFileNameA(directory, "pbc", 0, path), 0u);
    return path;
}

struct BoundaryFixture {
    std::string dict_path = temporary_file();
    std::string spelling_path = temporary_file();
    cxxime::Dict dict{cxxime::UserDictKind::PINYIN};
    std::shared_ptr<const cxxime::PinyinResourceSet> resources;

    void spellings(const std::vector<SpellingSpec>& entries) {
        ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spelling_path, entries));
        resources = cxxime::PinyinResourceSet::create(
            "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spelling_path);
        ASSERT_TRUE(resources != nullptr);
    }

    ~BoundaryFixture() {
        resources.reset();
        dict.close();
        DeleteFileA(dict_path.c_str());
        DeleteFileA(spelling_path.c_str());
    }
};

} // namespace

TEST(PinyinBoundaryCandidates, complete_readings_precede_dense_abbreviation_subtrees) {
    BoundaryFixture fixture;
    std::vector<SpellingSpec> entries = {
        {"jia", "jia", cxxime::kNormalSpelling, 0.0f},
        {"ng", "ng", cxxime::kNormalSpelling, 0.0f},
        {"ji", "ji", cxxime::kNormalSpelling, 0.0f},
        {"ang", "ang", cxxime::kNormalSpelling, 0.0f},
    };
    for (const char* syllable : {"na", "nai", "nan", "nang", "nao", "ne", "nei", "nen", "neng",
                                 "ni", "nian", "niang", "niao", "nie", "nin", "ning", "niu"}) {
        entries.push_back({"n", syllable, cxxime::kAbbreviation, -1.0f});
    }
    for (const char* syllable : {"gai", "gan", "gang", "gao", "ge", "gei", "gen", "geng", "gong",
                                 "gou", "gu", "gua", "guai", "guan", "guang", "gui", "guo"}) {
        entries.push_back({"g", syllable, cxxime::kAbbreviation, -1.0f});
    }
    fixture.spellings(entries);
    cxxime::SyllabifierOptions options;
    options.collect_path_metadata = true;
    const auto result = fixture.resources->segment("jiang", nullptr, options);
    ASSERT_GE(result.paths.size(), 2u);
    ASSERT_LE(result.paths.size(), 256u);
    const auto contains = [&](const std::vector<std::string>& syllables) {
        return std::any_of(result.paths.begin(), result.paths.begin() + 2,
                           [&](const auto& path) { return path.syllables == syllables; });
    };
    ASSERT_TRUE(contains({"jia", "ng"}));
    ASSERT_TRUE(contains({"ji", "ang"}));
    ASSERT_TRUE(std::any_of(result.paths.begin(), result.paths.end(), [](const auto& path) {
        return std::find(path.spelling_types.begin(), path.spelling_types.end(),
                         cxxime::kAbbreviation) != path.spelling_types.end();
    }));
}

TEST(PinyinBoundaryCandidates, alternative_boundaries_keep_their_candidates_across_pages) {
    BoundaryFixture fixture;
    fixture.spellings({
        {"yao", "yao", cxxime::kNormalSpelling, 0.0f},
        {"ya", "ya", cxxime::kNormalSpelling, 0.0f},
        {"o", "o", cxxime::kNormalSpelling, 0.0f},
        {"yi", "yi", cxxime::kNormalSpelling, 0.0f},
        {"piao", "piao", cxxime::kNormalSpelling, 0.0f},
    });
    std::vector<DictionaryEntry> words;
    for (int index = 0; index < 16; ++index) {
        words.emplace_back("yao:yi:piao", "整句" + std::to_string(index), 10000 - index);
    }
    std::set<std::string> expected;
    for (int index = 0; index < 32; ++index) {
        const std::string first = "甲" + std::to_string(index);
        const std::string second = "乙" + std::to_string(index);
        const std::string third = "丙" + std::to_string(index);
        words.emplace_back("yao", first, 1000 - index);
        words.emplace_back("ya", second, 1000 - index);
        words.emplace_back("ya:o", third, 1000 - index);
        expected.insert(first);
        expected.insert(second);
        expected.insert(third);
    }
    ASSERT_TRUE(cxxime::Dict::create_test_dict(fixture.dict_path, words));
    ASSERT_TRUE(fixture.dict.open_dict(fixture.dict_path));
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dict);
    translator.bind_pinyin(fixture.resources, {});
    cxxime::MixedTranslator mixed;
    mixed.set_pinyin_dict(&fixture.dict);
    mixed.bind_pinyin(fixture.resources, {});
    cxxime::ITranslator* translators[] = {&translator, &mixed};
    for (auto* current : translators) {
        for (const bool learning : {false, true}) {
            current->set_candidate_learning_enabled(learning);
            for (const int page_size : {5, 7}) {
                std::set<std::string> observed;
                std::set<std::string> full_choices;
                cxxime::TranslationRequest request;
                request.input = "yaoyipiao";
                request.page_size = page_size;
                request.policy.allow_partial_selection = true;
                for (int page = 0; page < 25; ++page) {
                    request.page_index = page;
                    request.page_offset = page * page_size;
                    const auto result = current->translate(request);
                    if (result.entries.empty()) {
                        break;
                    }
                    for (std::size_t index = 0; index < result.entries.size(); ++index) {
                        const auto& entry = result.entries[index];
                        const auto& action = std::get<cxxime::TextSelectionAction>(entry.selection);
                        const std::size_t position = request.page_offset + index;
                        const bool expected_full = position < 16;
                        ASSERT_EQ(action.consumed_input_bytes == request.input.size(),
                                  expected_full);
                        if (action.consumed_input_bytes < request.input.size()) {
                            ASSERT_TRUE(observed.insert(entry.candidate.text).second);
                            ASSERT_TRUE(action.consumed_input_bytes == 2 ||
                                        action.consumed_input_bytes == 3);
                        } else {
                            ASSERT_TRUE(full_choices.insert(entry.candidate.text).second);
                        }
                    }
                }
                ASSERT_TRUE(observed == expected);
                ASSERT_EQ(full_choices.size(), 16u);
            }
        }
    }
}

TEST(PinyinBoundaryCandidates, branches_without_a_route_to_the_endpoint_are_skipped) {
    BoundaryFixture fixture;
    const std::string input = std::string(24, 'a') + "b";
    fixture.spellings({
        {"a", "a", cxxime::kNormalSpelling, 0.0f},
        {"aa", "an", cxxime::kNormalSpelling, 0.0f},
        {input, "ba", cxxime::kNormalSpelling, 0.0f},
    });
    const auto result = fixture.resources->segment(input);
    ASSERT_EQ(result.paths.size(), 1u);
    ASSERT_EQ(result.paths.front().syllables.size(), 1u);
    ASSERT_EQ(result.paths.front().syllables.front(), "ba");
    ASSERT_TRUE(!result.deadline_exceeded);
}

TEST(PinyinBoundaryCandidates, real_dictionary_partial_yao_remains_selectable) {
    auto dictionary = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dictionary->open_bundle(CXXIME_DATA_DIR "pinyin.dict.bin", {},
                                        CXXIME_DATA_DIR "pinyin.dict.idx",
                                        CXXIME_DATA_DIR "pinyin.topn.bin"));
    cxxime::Config config;
    config.page_size = 7;
    config.candidate_learning = false;
    cxxime::Engine engine;
    ASSERT_TRUE(test::initialize_engine(engine, dictionary, config,
                                        CXXIME_DATA_DIR "pinyin.spellings.bin"));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    for (const char letter : std::string("yaoyipiao")) {
        engine.process_key({static_cast<uint32_t>(letter - 'a' + 'A'), 0, false});
    }
    bool selected = false;
    for (int attempt = 0; attempt < 100 && !selected; ++attempt) {
        const auto entries = engine.context().translation().entries;
        for (std::size_t index = 0; index < entries.size(); ++index) {
            if (entries[index].candidate.text == "舀") {
                const auto& action =
                    std::get<cxxime::TextSelectionAction>(entries[index].selection);
                ASSERT_EQ(action.consumed_input_bytes, 3u);
                ASSERT_TRUE(engine.select_candidate(static_cast<int>(index)));
                selected = true;
                break;
            }
        }
        if (!selected) {
            const int before = engine.context().page_index();
            engine.process_key({VK_NEXT, 0, false});
            if (engine.context().page_index() == before) {
                ASSERT_EQ(engine.last_trace().navigation_outcome,
                          cxxime::CandidateNavigationOutcome::kRetryable);
            }
        }
    }
    ASSERT_TRUE(selected);
    ASSERT_EQ(engine.context().active_input(), "yipiao");
    ASSERT_TRUE(engine.context().committed_text.empty());
    ASSERT_GT(engine.context().candidate_count(), 0);
    const auto& suffix = *engine.context().candidate_entry(0);
    ASSERT_EQ(std::get<cxxime::TextSelectionAction>(suffix.selection).consumed_input_bytes, 6u);
    const std::string suffix_text = suffix.candidate.text;
    ASSERT_EQ(engine.process_key({VK_SPACE, 0, false}), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "舀" + suffix_text);
}
