// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <cxxime/pinyin_input_graph.h>

#include "support/shuangpin_fallback_fixture.h"

namespace {

void expect_boundaries(const cxxime::PinyinInputGraph& graph,
                       const std::vector<uint16_t>& lengths) {
    ASSERT_EQ(graph.edges.size(), lengths.size());
    std::size_t position = 0;
    for (uint16_t length : lengths) {
        const auto vertex = graph.edges.find(position);
        ASSERT_TRUE(vertex != graph.edges.end());
        ASSERT_EQ(vertex->second.size(), std::size_t{1});
        const auto& group = *vertex->second.begin();
        ASSERT_EQ(group.first, position + length);
        ASSERT_TRUE(!group.second.empty());
        position += length;
    }
    ASSERT_EQ(graph.reachable_end, position);
}

} // namespace

TEST(ShuangpinGraph, appended_initials_preserve_complete_pairs_in_every_scheme) {
    const std::vector<std::vector<uint16_t>> expected = {
        {1}, {2}, {2, 1}, {2, 2}, {2, 2, 1}, {2, 2, 2}, {2, 2, 2, 1}, {2, 2, 2, 1, 1},
    };
    for (const auto& scheme : test::fallback_schemes()) {
        test::FallbackFixture fixture({{"mi", "米", 1}}, scheme);
        for (bool fuzzy : {false, true}) {
            for (std::size_t size = 1; size <= expected.size(); ++size) {
                const auto input = std::string("minimijf").substr(0, size);
                expect_boundaries(cxxime::PinyinInputGraph::build(*fixture.resources, input, fuzzy),
                                  expected[size - 1]);
            }
            // A trailing initial can still become a complete pair; earlier pairs stay fixed.
            expect_boundaries(
                cxxime::PinyinInputGraph::build(*fixture.resources, "minimijm", fuzzy),
                {2, 2, 2, 2});
        }
    }
}

TEST(ShuangpinGraph, zero_initial_boundaries_apply_before_normal_and_fallback_queries) {
    for (const auto& scheme : {test::fallback_schemes().front(), test::fallback_schemes().back()}) {
        test::FallbackFixture fixture({{"o", "哦", 1}}, scheme);
        for (bool fuzzy : {false, true}) {
            expect_boundaries(cxxime::PinyinInputGraph::build(*fixture.resources, "o", fuzzy), {1});
            expect_boundaries(cxxime::PinyinInputGraph::build(*fixture.resources, "oo", fuzzy),
                              {2});
            const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, "ooa", fuzzy);
            ASSERT_TRUE(!graph.fallback);
            expect_boundaries(graph, {2, 1});
            const auto paths = graph.align({}, "o:a", 3, 32);
            ASSERT_TRUE(!paths.empty());
            for (const auto& path : paths) {
                ASSERT_TRUE(path.input_lengths == std::vector<uint16_t>({2, 1}));
            }
            ASSERT_TRUE(graph.align({}, "o:o:a", 3).empty());
            ASSERT_TRUE(graph.align({}, "o", 1).empty());
            ASSERT_TRUE(!graph.align({}, "o", 2).empty());
            // Covering a later b must not steal the second o from the established pair.
            const auto blocked = cxxime::PinyinInputGraph::build(*fixture.resources, "ooab", fuzzy);
            expect_boundaries(blocked, {2});
            cxxime::SyllabifierOptions options;
            options.enable_fuzzy = fuzzy;
            options.enable_terminal_completion = true;
            options.collect_path_metadata = true;
            const auto segmented = fixture.resources->segment("ooa", nullptr, options);
            ASSERT_TRUE(!segmented.paths.empty());
            for (const auto& path : segmented.paths) {
                ASSERT_TRUE(path.input_lengths == std::vector<uint16_t>({2, 1}));
            }
            options.enable_terminal_completion = false;
            const auto exact = fixture.resources->build_graph("ooa", options);
            ASSERT_EQ(exact.size(), std::size_t{1});
            ASSERT_EQ(exact.at(0).begin()->first, std::size_t{2});
            // Choosing a complete span must not remove allowed same-span completion readings.
            const auto single = cxxime::PinyinInputGraph::build(*fixture.resources, "o", fuzzy);
            ASSERT_TRUE(std::any_of(
                single.edges.at(0).at(1).begin(), single.edges.at(0).at(1).end(),
                [](const auto& edge) { return edge.type == cxxime::kCompletionSpelling; }));
        }
    }
}

TEST(ShuangpinGraph, dictionary_misses_cannot_change_the_left_to_right_boundaries) {
    test::FallbackFixture fixture({{"jun:fu:ni", "军服你", 1000000}}, test::fallback_schemes()[1]);
    const auto selected = cxxime::PinyinInputGraph::build(*fixture.resources, "jfni", false);
    ASSERT_TRUE(selected.fallback);
    expect_boundaries(selected, {1, 2, 1});
    ASSERT_TRUE(!selected.align({}, "jun:fiao:chi", 4).empty());
    ASSERT_TRUE(selected.align({}, "jun:fu:ni", 4).empty());
    const auto result = fixture.query("jfni", false);
    ASSERT_TRUE(result.entries.empty());
    ASSERT_TRUE(result.extent.complete);
}

TEST(ShuangpinGraph, mapped_initials_are_independent_of_fuzzy_and_ignored_fragments_stay_absent) {
    for (const auto& scheme : test::fallback_schemes()) {
        test::FallbackFixture fixture({{"jun", "军", 1}}, scheme);
        for (const auto& mapping : std::vector<std::pair<std::string, std::string>>{
                 {"v", "zhong"}, {"i", "chi"}, {"u", "shi"}}) {
            const auto matches = fixture.resources->prefix_search(mapping.first, false);
            ASSERT_TRUE(std::any_of(matches.begin(), matches.end(), [&](const auto& match) {
                return match.type == cxxime::kAbbreviation && match.syllable == mapping.second;
            }));
            ASSERT_TRUE(std::none_of(matches.begin(), matches.end(), [&](const auto& match) {
                return match.syllable == mapping.first;
            }));
        }
        const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, "jf", false);
        ASSERT_TRUE(graph.fallback);
        ASSERT_TRUE(!graph.align({}, "jun:fu", 2).empty());
        ASSERT_TRUE(graph.align({}, "jun:fu", 1).empty());
    }
}

TEST(ShuangpinGraph, partial_display_uses_consumption_endpoint_before_unparseable_suffix) {
    test::FallbackFixture fixture({{"ni", "你", 1}});
    fixture.type("ni;hk");
    const auto presentation = cxxime::derive_composition_presentation(
        fixture.engine.context().composition(), fixture.resources.get(), 2, true, "ni", true);
    ASSERT_EQ(presentation.display_preedit, "ni';hk");
    ASSERT_EQ(presentation.focused_preedit_end_bytes, std::size_t{2});
}

TEST(ShuangpinGraph, normal_graph_segmentation_preserves_paths_and_display_boundaries) {
    for (const auto& scheme : test::fallback_schemes()) {
        test::FallbackFixture fixture({{"ni:hao", "你好", 100}}, scheme);
        for (bool fuzzy : {false, true}) {
            for (const std::string input : {"ni", "nih", "nihk"}) {
                auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, input, fuzzy);
                ASSERT_TRUE(!graph.fallback);
                cxxime::SyllabifierOptions options;
                options.enable_fuzzy = fuzzy;
                options.enable_terminal_completion = true;
                options.collect_path_metadata = true;
                const auto expected = fixture.resources->segment(input, nullptr, options);
                const auto reused =
                    fixture.resources->segment_graph(std::move(graph.edges), nullptr, true);
                ASSERT_EQ(reused.truncated, expected.truncated);
                ASSERT_EQ(reused.deadline_exceeded, expected.deadline_exceeded);
                ASSERT_EQ(reused.paths.size(), expected.paths.size());
                for (std::size_t index = 0; index < expected.paths.size(); ++index) {
                    ASSERT_TRUE(reused.paths[index].syllables == expected.paths[index].syllables);
                    ASSERT_TRUE(reused.paths[index].spelling_types ==
                                expected.paths[index].spelling_types);
                    ASSERT_TRUE(reused.paths[index].input_lengths ==
                                expected.paths[index].input_lengths);
                    ASSERT_EQ(reused.paths[index].credibility, expected.paths[index].credibility);
                }
            }
            fixture.engine.clear();
            fixture.type("nihk");
            for (bool completion : {false, true}) {
                const auto presentation = cxxime::derive_composition_presentation(
                    fixture.engine.context().composition(), fixture.resources.get(), 2, true, "ni",
                    completion, fuzzy);
                ASSERT_EQ(presentation.logical_preedit, "nihk");
                ASSERT_EQ(presentation.display_preedit, "ni'hk");
                ASSERT_EQ(presentation.focused_preedit_end_bytes, std::size_t{2});
            }
        }
    }
}
