// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/dict.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/translator.h>

#include "support/testutil.h"

namespace {

std::string make_temp_path(const char* prefix) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, prefix, 0, path) == 0) {
        return {};
    }
    return path;
}

bool contains_spelling(const std::vector<cxxime::SpellingMatch>& matches,
                       const std::string& syllable) {
    return std::any_of(matches.begin(), matches.end(), [&syllable](const auto& match) {
        return match.syllable == syllable;
    });
}

bool contains_path(const cxxime::SegmentResult& result,
                   const std::vector<std::string>& expected) {
    return std::any_of(result.paths.begin(), result.paths.end(), [&expected](const auto& path) {
            return path.syllables == expected;
        });
}

struct CompletionFixture {
    std::string spelling_path = make_temp_path("cxs");
    cxxime::SpellingsIndex spellings;

    bool initialize() {
        if (spelling_path.empty()) {
            return false;
        }
        if (!cxxime::SpellingsIndex::create_test_trie(spelling_path, {
            {"ni", "ni", cxxime::kNormalSpelling, 0.0f},
            {"hao", "hao", cxxime::kNormalSpelling, 0.0f},
            {"shi", "shi", cxxime::kNormalSpelling, 0.0f},
            {"ji", "ji", cxxime::kNormalSpelling, 0.0f},
            {"jie", "jie", cxxime::kNormalSpelling, 0.0f},
            {"jin", "jin", cxxime::kNormalSpelling, 0.0f},
        })) {
            return false;
        }
        return spellings.load(spelling_path);
    }

    ~CompletionFixture() {
        spellings.unload();
        if (!spelling_path.empty()) {
            DeleteFileA(spelling_path.c_str());
        }
    }
};

} // namespace

TEST(SyllabifierCompletion, search_returns_only_strict_extensions) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    const auto matches = fixture.spellings.completion_search("ji");
    ASSERT_TRUE(!contains_spelling(matches, "ji"));
    ASSERT_TRUE(contains_spelling(matches, "jie"));
    ASSERT_TRUE(contains_spelling(matches, "jin"));
}

TEST(SyllabifierCompletion, segment_adds_terminal_completion_on_request) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    cxxime::Syllabifier syllabifier(fixture.spellings);

    const std::vector<std::string> exact = {"ni", "hao", "shi", "ji"};
    const std::vector<std::string> completed = {"ni", "hao", "shi", "jie"};
    const auto normal_result = syllabifier.segment("nihaoshiji");
    ASSERT_TRUE(contains_path(normal_result, exact));
    ASSERT_TRUE(!contains_path(normal_result, completed));

    const auto completion_result = syllabifier.segment("nihaoshiji", nullptr, true);
    ASSERT_TRUE(contains_path(completion_result, exact));
    ASSERT_TRUE(contains_path(completion_result, completed));
}

TEST(SyllabifierCompletion, path_metadata_is_collected_only_on_request) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    cxxime::Syllabifier syllabifier(fixture.spellings);

    const auto baseline = syllabifier.segment("nihao");
    ASSERT_TRUE(!baseline.paths.empty());
    ASSERT_TRUE(baseline.paths[0].spelling_types.empty());
    ASSERT_TRUE(baseline.paths[0].input_lengths.empty());

    const auto with_metadata = syllabifier.segment("nihao", nullptr, false, true);
    ASSERT_TRUE(!with_metadata.paths.empty());
    ASSERT_EQ(with_metadata.paths[0].syllables.size(),
              with_metadata.paths[0].spelling_types.size());
    ASSERT_EQ(with_metadata.paths[0].syllables.size(), with_metadata.paths[0].input_lengths.size());
}

TEST(SyllabifierCompletion, translator_retries_when_exact_path_has_no_word) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const std::string dictionary_path = make_temp_path("cxd");
    ASSERT_TRUE(!dictionary_path.empty());
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, {
        {"ni:hao:shi:jie", "hello-world", 1000},
    }));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open_dict(dictionary_path));
    cxxime::Syllabifier syllabifier(fixture.spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dictionary);
    translator.set_syllabifier(&syllabifier);

    const auto page = translator.translate("nihaoshiji", 0, 10);
    ASSERT_TRUE(!page.candidates.empty());
    ASSERT_EQ(page.candidates[0].text, "hello-world");

    dictionary.close();
    DeleteFileA(dictionary_path.c_str());
}

TEST(SyllabifierCompletion, translator_keeps_valid_exact_path_authoritative) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const std::string dictionary_path = make_temp_path("cxd");
    ASSERT_TRUE(!dictionary_path.empty());
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, {
        {"ni:hao:shi:ji", "exact-ji", 500},
        {"ni:hao:shi:jie", "completed-jie", 1000},
    }));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open_dict(dictionary_path));
    cxxime::Syllabifier syllabifier(fixture.spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dictionary);
    translator.set_syllabifier(&syllabifier);

    const auto page = translator.translate("nihaoshiji", 0, 10);
    ASSERT_TRUE(!page.candidates.empty());
    ASSERT_EQ(page.candidates[0].text, "exact-ji");
    ASSERT_EQ(page.candidates.size(), 1u);

    dictionary.close();
    DeleteFileA(dictionary_path.c_str());
}

RUN_ALL_TESTS()
