// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include <cxxime/pinyin_resource.h>

#include "support/testutil.h"

TEST(ShuangpinSchemaRuntime, generated_schema_loads_and_segments_through_runtime_index) {
    auto pinyin_resources = cxxime::PinyinResourceSet::create(
        "synthetic", cxxime::PinyinSchemeKind::kShuangpin, CXXIME_SYNTHETIC_SHUANGPIN_PATH,
        cxxime::PinyinSpellingRequirement::kRequired);
    ASSERT_TRUE(pinyin_resources != nullptr);

    const auto initial = pinyin_resources->prefix_search("xa");
    ASSERT_TRUE(std::any_of(initial.begin(), initial.end(), [](const auto& match) {
        return match.syllable == "ni" && match.type == cxxime::kNormalSpelling;
    }));
    const auto final = pinyin_resources->prefix_search("zb");
    ASSERT_TRUE(std::any_of(final.begin(), final.end(), [](const auto& match) {
        return match.syllable == "hao" && match.type == cxxime::kNormalSpelling;
    }));

    cxxime::SyllabifierOptions options;
    options.collect_path_metadata = true;
    const cxxime::SegmentResult result = pinyin_resources->segment("xazb", nullptr, options);
    const auto path = std::find_if(result.paths.begin(), result.paths.end(), [](const auto& item) {
        return item.syllables == std::vector<std::string>{"ni", "hao"};
    });
    ASSERT_TRUE(path != result.paths.end());
    ASSERT_EQ(std::accumulate(path->input_lengths.begin(), path->input_lengths.end(), 0u), 4u);
}

TEST(ShuangpinSchemaRuntime, built_in_schemes_segment_full_and_zero_initial_syllables) {
    struct SchemeCase {
        const char* id;
        const char* path;
        const char* nihao;
        const char* zero_initial_hao;
    };
    const SchemeCase cases[] = {
        {"microsoft", CXXIME_MICROSOFT_SHUANGPIN_PATH, "nihk", "ajhk"},
        {"xiaohe", CXXIME_XIAOHE_SHUANGPIN_PATH, "nihc", "anhc"},
        {"ziranma", CXXIME_ZIRANMA_SHUANGPIN_PATH, "nihk", "anhk"},
        {"sogou", CXXIME_SOGOU_SHUANGPIN_PATH, "nihk", "ojhk"},
    };

    for (const auto& item : cases) {
        auto pinyin_resources = cxxime::PinyinResourceSet::create(
            item.id, cxxime::PinyinSchemeKind::kShuangpin, item.path,
            cxxime::PinyinSpellingRequirement::kRequired);
        ASSERT_TRUE(pinyin_resources != nullptr);

        for (const auto& path_case : {
                 std::make_pair(std::string(item.nihao), std::vector<std::string>{"ni", "hao"}),
                 std::make_pair(std::string(item.zero_initial_hao),
                                std::vector<std::string>{"an", "hao"}),
             }) {
            cxxime::SyllabifierOptions options;
            options.collect_path_metadata = true;
            const cxxime::SegmentResult result =
                pinyin_resources->segment(path_case.first, nullptr, options);
            const auto path = std::find_if(result.paths.begin(), result.paths.end(),
                                           [&](const auto& candidate) {
                                               return candidate.syllables == path_case.second;
                                           });
            ASSERT_TRUE(path != result.paths.end());
            ASSERT_EQ(std::accumulate(path->input_lengths.begin(), path->input_lengths.end(), 0u),
                      path_case.first.size());
        }
    }
}

int main() { return test::RunAllTests(); }
