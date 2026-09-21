// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>

#include "support/testutil.h"

TEST(ShuangpinSchemaRuntime, generated_schema_loads_and_segments_through_runtime_index) {
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(CXXIME_SYNTHETIC_SHUANGPIN_PATH));

    const auto initial = spellings.prefix_search("xa");
    ASSERT_TRUE(std::any_of(initial.begin(), initial.end(), [](const auto& match) {
        return match.syllable == "ni" && match.type == cxxime::kNormalSpelling;
    }));
    const auto final = spellings.prefix_search("zb");
    ASSERT_TRUE(std::any_of(final.begin(), final.end(), [](const auto& match) {
        return match.syllable == "hao" && match.type == cxxime::kNormalSpelling;
    }));

    cxxime::Syllabifier syllabifier(spellings);
    const cxxime::SegmentResult result = syllabifier.segment("xazb", nullptr, false, true);
    const auto path = std::find_if(result.paths.begin(), result.paths.end(), [](const auto& item) {
        return item.syllables == std::vector<std::string>{"ni", "hao"};
    });
    ASSERT_TRUE(path != result.paths.end());
    ASSERT_EQ(std::accumulate(path->input_lengths.begin(), path->input_lengths.end(), 0u), 4u);
}

TEST(ShuangpinSchemaRuntime, built_in_schemes_segment_full_and_zero_initial_syllables) {
    struct SchemeCase {
        const char* path;
        const char* nihao;
        const char* zero_initial_hao;
    };
    const SchemeCase cases[] = {
        {CXXIME_MICROSOFT_SHUANGPIN_PATH, "nihk", "ajhk"},
        {CXXIME_XIAOHE_SHUANGPIN_PATH, "nihc", "anhc"},
        {CXXIME_ZIRANMA_SHUANGPIN_PATH, "nihk", "anhk"},
        {CXXIME_SOGOU_SHUANGPIN_PATH, "nihk", "ojhk"},
    };

    for (const auto& item : cases) {
        cxxime::SpellingsIndex spellings;
        ASSERT_TRUE(spellings.load(item.path));
        cxxime::Syllabifier syllabifier(spellings);

        for (const auto& path_case : {
                 std::make_pair(std::string(item.nihao), std::vector<std::string>{"ni", "hao"}),
                 std::make_pair(std::string(item.zero_initial_hao),
                                std::vector<std::string>{"an", "hao"}),
             }) {
            const cxxime::SegmentResult result =
                syllabifier.segment(path_case.first, nullptr, false, true);
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
