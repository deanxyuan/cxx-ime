// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <cxxime/pinyin_input_graph.h>
#include <cxxime/query_trace.h>

#include "pinyin_graph_query.h"
#include "support/shuangpin_fallback_fixture.h"

namespace {

void expect_graph(const cxxime::SyllableGraph& actual, const cxxime::SyllableGraph& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    auto left = actual.begin();
    for (const auto& vertex : expected) {
        ASSERT_EQ(left->first, vertex.first);
        ASSERT_EQ(left->second.size(), vertex.second.size());
        auto group = left->second.begin();
        for (const auto& edges : vertex.second) {
            ASSERT_EQ(group->first, edges.first);
            ASSERT_EQ(group->second.size(), edges.second.size());
            for (std::size_t index = 0; index < edges.second.size(); ++index) {
                ASSERT_EQ(group->second[index].syllable, edges.second[index].syllable);
                ASSERT_EQ(group->second[index].type, edges.second[index].type);
                ASSERT_EQ(group->second[index].credibility, edges.second[index].credibility);
            }
            ++group;
        }
        ++left;
    }
}

void expect_result(const cxxime::TranslationResult& actual,
                   const cxxime::TranslationResult& expected) {
    ASSERT_EQ(static_cast<int>(actual.status), static_cast<int>(expected.status));
    ASSERT_EQ(static_cast<int>(actual.extent.state), static_cast<int>(expected.extent.state));
    ASSERT_EQ(actual.extent.complete, expected.extent.complete);
    ASSERT_EQ(actual.extent.known_count, expected.extent.known_count);
    ASSERT_EQ(actual.entries.size(), expected.entries.size());
    for (std::size_t index = 0; index < actual.entries.size(); ++index) {
        const auto& left = actual.entries[index];
        const auto& right = expected.entries[index];
        ASSERT_EQ(left.candidate.text, right.candidate.text);
        ASSERT_EQ(left.candidate.code, right.candidate.code);
        ASSERT_EQ(left.candidate.syllables, right.candidate.syllables);
        ASSERT_EQ(left.candidate.input_code, right.candidate.input_code);
        ASSERT_EQ(left.candidate.frequency, right.candidate.frequency);
        ASSERT_EQ(static_cast<int>(left.candidate.origin),
                  static_cast<int>(right.candidate.origin));
        const auto* left_action = std::get_if<cxxime::TextSelectionAction>(&left.selection);
        const auto* right_action = std::get_if<cxxime::TextSelectionAction>(&right.selection);
        ASSERT_EQ(left_action != nullptr, right_action != nullptr);
        if (left_action) {
            ASSERT_EQ(left_action->consumed_input_bytes, right_action->consumed_input_bytes);
        }
    }
}

void check_spelling_views(const cxxime::PinyinResourceSet& resources, const std::string& input) {
    cxxime::SpellingQueryCache cache;
    for (std::size_t length = 1; length <= input.size(); ++length) {
        const auto prefix = input.substr(0, length);
        for (bool fuzzy : {false, true}) {
            for (bool abbreviation : {false, true}) {
                for (bool completion : {false, true}) {
                    cxxime::SyllabifierOptions options;
                    options.enable_fuzzy = fuzzy;
                    options.enable_abbreviation = abbreviation;
                    options.enable_terminal_completion = completion;
                    expect_graph(resources.build_graph(prefix, options, &cache),
                                 resources.build_graph(prefix, options));
                }
            }
            for (bool forced : {false, true}) {
                const auto warm =
                    cxxime::PinyinInputGraph::build(resources, prefix, fuzzy, forced, &cache);
                const auto cold = cxxime::PinyinInputGraph::build(resources, prefix, fuzzy, forced);
                expect_graph(warm.edges, cold.edges);
                ASSERT_EQ(warm.reachable_end, cold.reachable_end);
                ASSERT_EQ(warm.fallback, cold.fallback);
                // Old candidate alignment may request the previous forced view
                // after the current normal/completion views already ran.
                if (length > 1) {
                    const auto old_input = input.substr(0, length - 1);
                    const auto old =
                        cxxime::PinyinInputGraph::build(resources, old_input, fuzzy, true, &cache);
                    const auto rebuilt =
                        cxxime::PinyinInputGraph::build(resources, old_input, fuzzy, true);
                    expect_graph(old.edges, rebuilt.edges);
                }
            }
        }
    }
    const auto lookups = cache.lookup_count();
    cxxime::PinyinInputGraph::build(resources, input, true, true, &cache);
    ASSERT_EQ(cache.lookup_count(), lookups);
    ASSERT_TRUE(cache.reuse_count() > 0);
    // A middle edit must not inherit positional facts from the old suffix.
    const auto changed = input.substr(0, 1) + "a" + input.substr(1);
    expect_graph(cxxime::PinyinInputGraph::build(resources, changed, true, true, &cache).edges,
                cxxime::PinyinInputGraph::build(resources, changed, true, true).edges);
}

} // namespace

TEST(PinyinQueryReuse, spelling_views_match_rebuild_at_every_append_in_all_schemes) {
    for (const auto& scheme : test::fallback_schemes()) {
        const auto resources = cxxime::PinyinResourceSet::create(
            scheme.id, cxxime::PinyinSchemeKind::kShuangpin, scheme.path);
        ASSERT_TRUE(resources != nullptr);
        check_spelling_views(*resources, "buyrzonuliderfmf");
        check_spelling_views(*resources, "sldfjdsljfdsdffsddfsss");
    }
    const auto resources =
        cxxime::PinyinResourceSet::create("full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin,
                                          std::string(CXXIME_DATA_DIR) + "pinyin.spellings.bin");
    ASSERT_TRUE(resources != nullptr);
    check_spelling_views(*resources, "buyuanyizuonulidrenmen");
    check_spelling_views(*resources, "hangao");
}

TEST(PinyinQueryReuse, closed_trie_prefixes_skip_queries_but_open_compressed_edges_extend) {
    test::FallbackFixture fixture({{"zhong", "中", 100}});
    const auto path = fixture.path + ".spellings";
    // Raw key, canonical reading, spelling type, credibility.
    using Spelling = std::tuple<std::string, std::string, int, float>;
    const std::vector<Spelling> spellings = {
        {"z", "zhong", cxxime::kAbbreviation, -1.0f},
        {"zhong", "zhong", cxxime::kNormalSpelling, 0.0f},
        {"a", "a", cxxime::kNormalSpelling, 0.0f},
        {"ang", "ang", cxxime::kNormalSpelling, 0.0f},
    };
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(path, spellings));
    auto resources = cxxime::PinyinResourceSet::create("full_pinyin",
                                                       cxxime::PinyinSchemeKind::kFullPinyin, path);
    ASSERT_TRUE(resources != nullptr);
    check_spelling_views(*resources, "zhongang");
    cxxime::SpellingQueryCache cache;
    cxxime::SyllabifierOptions options;
    options.enable_terminal_completion = true;
    resources->build_graph("zhong", options, &cache);
    const auto before = cache.lookup_count();
    expect_graph(resources->build_graph("zhonga", options, &cache),
                 resources->build_graph("zhonga", options));
    // Only the new raw start is queried (prefix + completion).
    ASSERT_EQ(cache.lookup_count() - before, std::size_t{2});
    resources.reset();
    ASSERT_TRUE(DeleteFileA(path.c_str()));
}

TEST(PinyinQueryReuse, append_matches_cold_queries_with_the_same_visible_prefix) {
    const std::vector<test::FallbackDictionaryRow> words = {
        {"bu:yuan", "不愿", 1000}, {"zuo", "做", 1000}, {"nu:li", "奴隶", 1000},
        {"de", "的", 1000},        {"dui", "对", 5000}, {"ren", "人", 1000},
        {"ren:men", "人们", 1000}, {"min", "民", 100},
    };
    for (const auto& scheme : test::fallback_schemes()) {
        test::FallbackFixture fixture(words, scheme);
        cxxime::PinyinTranslator cold;
        cold.set_dict(fixture.dict.get());
        cold.bind_pinyin(fixture.resources, {false});
        std::optional<cxxime::CandidateContinuation> previous;
        cxxime::TranslationRequest request;
        const std::string input = "buyrzonuliderfmf";
        for (std::size_t length = 1; length <= input.size(); ++length) {
            request.input = input.substr(0, length);
            request.input_revision = length;
            request.continuation = previous ? &*previous : nullptr;
            cold.clear_query_cache();
            const auto actual = fixture.translator.translate(request);
            expect_result(actual, cold.translate(request));
            // Search must not replace the active input's spelling state.
            const auto search = fixture.translator.translate_page("nihao");
            (void)search;
            auto search_request = request;
            search_request.purpose = cxxime::TranslationPurpose::kSearch;
            expect_result(fixture.translator.translate(search_request),
                          cold.translate(search_request));
            previous.reset();
            for (const auto& entry : actual.entries) {
                const auto* selection = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
                if (selection && selection->consumed_input_bytes == length) {
                    previous = cxxime::CandidateContinuation{request.input, entry.candidate};
                    break;
                }
            }
        }
        // Reopening the same Dict object changes its generation and all ranges.
        const auto old_root = fixture.dict->syllable_root();
        auto replacement = words;
        replacement[4] = {"bu:yuan", "补员", 20000};
        ASSERT_TRUE(cxxime::Dict::create_test_dict(fixture.path, replacement));
        ASSERT_TRUE(fixture.dict->open_dict(fixture.path));
        ASSERT_EQ(fixture.dict->syllable_root().end, old_root.end);
        auto search_request = request;
        search_request.purpose = cxxime::TranslationPurpose::kSearch;
        search_request.continuation = nullptr;
        cold.clear_query_cache();
        expect_result(fixture.translator.translate(search_request), cold.translate(search_request));
        request.input = "bu";
        request.continuation = nullptr;
        ++request.input_revision;
        cold.clear_query_cache();
        expect_result(fixture.translator.translate(request), cold.translate(request));
    }
}

TEST(PinyinQueryReuse, dense_query_finishes_after_exceeding_window_memo_capacity) {
    const std::vector<std::pair<std::string, std::string>> syllables = {
        {"ha", "哈"},    {"hai", "海"}, {"han", "汉"}, {"hang", "行"}, {"hao", "好"},
        {"he", "和"},    {"hei", "黑"}, {"hen", "很"}, {"heng", "横"}, {"hong", "红"},
        {"hou", "后"},   {"hu", "胡"},  {"hua", "花"}, {"huai", "怀"}, {"huan", "欢"},
        {"huang", "黄"}, {"hui", "会"}, {"hun", "混"}, {"huo", "火"},
    };
    std::vector<test::FallbackDictionaryRow> rows;
    for (const auto& first : syllables) {
        for (const auto& second : syllables) {
            rows.emplace_back(first.first + ':' + second.first, first.second + second.second, 100);
        }
    }
    test::FallbackFixture fixture(rows);
    const auto resources =
        cxxime::PinyinResourceSet::create("full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin,
                                          std::string(CXXIME_DATA_DIR) + "pinyin.spellings.bin");
    ASSERT_TRUE(resources != nullptr);
    const auto graph = cxxime::PinyinInputGraph::build(*resources, "hh", false, true);
    cxxime::PinyinGraphQuery query(*fixture.dict, graph, false, false, false, nullptr);
    cxxime::QueryBudget budget;
    budget.max_prefix_scan = 4096;
    cxxime::TranslationRequest request;
    request.input = "hh";
    request.purpose = cxxime::TranslationPurpose::kSearch;
    request.page_size = 1000;
    request.budget = &budget;
    bool full_window = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        cxxime::QueryTrace trace;
        request.trace = &trace;
        const auto result = query.query(request);
        full_window = full_window || trace.span_query_count > 128;
        if (result.extent.complete) {
            ASSERT_EQ(result.entries.size(), rows.size());
            ASSERT_TRUE(full_window);
            return;
        }
    }
    ASSERT_TRUE(false);
}

TEST(PinyinQueryReuse, failed_visible_prefix_retries_with_the_same_budget_and_trace_independence) {
    test::FallbackFixture fixture({{"jun", "军", 100}, {"fu", "服", 100}});
    const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, "jf", false);
    cxxime::CandidateContinuation previous;
    previous.input = "j";
    previous.candidate.text = "君";
    previous.candidate.code = "jun";
    previous.candidate.syllables = "jun";
    previous.candidate.source = cxxime::CandidateSource::kPinyin;
    const auto prefix =
        cxxime::PinyinContinuation::build(*fixture.resources, graph, false, &previous);
    ASSERT_TRUE(!prefix.accepts(0, 1, "军"));
    for (uint32_t limit : {1u, 128u}) {
        uint32_t unconstrained_ranges = 0;
        for (bool constrained : {false, true}) {
            cxxime::PinyinGraphQuery cold(*fixture.dict, graph, false, true, false, nullptr,
                                          constrained ? prefix : cxxime::PinyinContinuation{},
                                          cxxime::PinyinGraphQueryMode::kSentence);
            cxxime::PinyinGraphQuery untraced(*fixture.dict, graph, false, true, false, nullptr,
                                              constrained ? prefix : cxxime::PinyinContinuation{},
                                              cxxime::PinyinGraphQueryMode::kSentence);
            cxxime::TranslationRequest request;
            request.input = "jf";
            cxxime::QueryBudget budget;
            budget.max_prefix_scan = limit;
            request.budget = &budget;
            uint32_t ranges = 0;
            for (int attempt = 0; attempt < 100; ++attempt) {
                cxxime::QueryTrace cold_trace;
                request.trace = &cold_trace;
                const auto expected = cold.query(request);
                request.trace = nullptr;
                expect_result(untraced.query(request), expected);
                ranges += cold_trace.span_query_count;
                if (!expected.entries.empty()) {
                    ASSERT_EQ(expected.entries.front().candidate.text, "军服");
                    break;
                }
                ASSERT_TRUE(attempt < 99);
            }
            if (constrained) {
                ASSERT_TRUE(ranges > unconstrained_ranges);
            } else {
                unconstrained_ranges = ranges;
            }
        }
    }
}
