// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <numeric>
#include <string>
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

TEST(ShuangpinSchemaRuntime, microsoft_zero_initials_consume_their_two_raw_keys) {
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(CXXIME_MICROSOFT_SHUANGPIN_PATH));
    cxxime::Syllabifier syllabifier(spellings);

    const auto assert_path = [&](const std::string& input,
                                 const std::vector<std::string>& syllables) {
        const cxxime::SegmentResult result = syllabifier.segment(input, nullptr, false, true);
        const auto path =
            std::find_if(result.paths.begin(), result.paths.end(),
                         [&](const auto& item) { return item.syllables == syllables; });
        ASSERT_TRUE(path != result.paths.end());
        ASSERT_EQ(std::accumulate(path->input_lengths.begin(), path->input_lengths.end(), 0u),
                  input.size());
    };
    assert_path("aa", {"a"});
    assert_path("ee", {"e"});
    assert_path("oo", {"o"});
    assert_path("aahk", {"a", "hao"});
}

int main() { return test::RunAllTests(); }
