// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/dict.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/spellings_index.h>
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
    std::shared_ptr<const cxxime::PinyinResourceSet> pinyin_resources;

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
        pinyin_resources = cxxime::PinyinResourceSet::create(
            "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spelling_path);
        return pinyin_resources != nullptr;
    }

    ~CompletionFixture() {
        if (!spelling_path.empty()) {
            DeleteFileA(spelling_path.c_str());
        }
    }
};

} // namespace

TEST(SyllabifierCompletion, search_returns_only_strict_extensions) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    const auto matches = fixture.pinyin_resources->completion_search("ji");
    ASSERT_TRUE(!contains_spelling(matches, "ji"));
    ASSERT_TRUE(contains_spelling(matches, "jie"));
    ASSERT_TRUE(contains_spelling(matches, "jin"));
}

TEST(SyllabifierCompletion, segment_adds_terminal_completion_on_request) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    const std::vector<std::string> exact = {"ni", "hao", "shi", "ji"};
    const std::vector<std::string> completed = {"ni", "hao", "shi", "jie"};
    const auto normal_result = fixture.pinyin_resources->segment("nihaoshiji");
    ASSERT_TRUE(contains_path(normal_result, exact));
    ASSERT_TRUE(!contains_path(normal_result, completed));

    cxxime::SyllabifierOptions completion_options;
    completion_options.enable_terminal_completion = true;
    const auto completion_result =
        fixture.pinyin_resources->segment("nihaoshiji", nullptr, completion_options);
    ASSERT_TRUE(contains_path(completion_result, exact));
    ASSERT_TRUE(contains_path(completion_result, completed));
}

TEST(SyllabifierCompletion, path_metadata_is_collected_only_on_request) {
    CompletionFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const auto baseline = fixture.pinyin_resources->segment("nihao");
    ASSERT_TRUE(!baseline.paths.empty());
    ASSERT_TRUE(baseline.paths[0].spelling_types.empty());
    ASSERT_TRUE(baseline.paths[0].input_lengths.empty());

    cxxime::SyllabifierOptions metadata_options;
    metadata_options.collect_path_metadata = true;
    const auto with_metadata =
        fixture.pinyin_resources->segment("nihao", nullptr, metadata_options);
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

    cxxime::Dict dictionary{cxxime::UserDictKind::PINYIN};
    ASSERT_TRUE(dictionary.open_dict(dictionary_path));
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});

    const auto page = translator.translate_page("nihaoshiji", 0, 10);
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

    cxxime::Dict dictionary{cxxime::UserDictKind::PINYIN};
    ASSERT_TRUE(dictionary.open_dict(dictionary_path));
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});

    const auto page = translator.translate_page("nihaoshiji", 0, 10);
    ASSERT_TRUE(!page.candidates.empty());
    ASSERT_EQ(page.candidates[0].text, "exact-ji");
    ASSERT_EQ(page.candidates.size(), 1u);

    dictionary.close();
    DeleteFileA(dictionary_path.c_str());
}

RUN_ALL_TESTS()
