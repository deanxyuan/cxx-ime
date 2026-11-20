// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <cxxime/custom_phrases.h>
#include <cxxime/mixed_translator.h>
#include <cxxime/query_trace.h>
#include <cxxime/pinyin_input_graph.h>

#include "support/shuangpin_fallback_fixture.h"

namespace {

std::string word_text(int index) {
    // Equal-width suffixes keep stored text order aligned with frequency order.
    return "词" + std::to_string(1000 + index);
}

std::vector<test::FallbackDictionaryRow> homophones(int count) {
    std::vector<test::FallbackDictionaryRow> rows;
    for (int index = 0; index < count; ++index) {
        rows.emplace_back("jun:fu", word_text(index), 100000 - 100 * index);
    }
    return rows;
}

std::vector<test::FallbackDictionaryRow> composition_initial_words() {
    // Fourteen j readings times nine f readings give 126 valid combinations.
    return {
        {"ji", "机", 1400},   {"jia", "家", 1300}, {"jian", "坚", 1200}, {"jiang", "江", 1100},
        {"jiao", "交", 1000}, {"jie", "接", 900},  {"jin", "金", 800},   {"jing", "京", 700},
        {"jiong", "窘", 600}, {"jiu", "九", 500},  {"ju", "居", 400},    {"juan", "娟", 300},
        {"jue", "觉", 200},   {"jun", "军", 100},  {"fa", "发", 900},    {"fan", "帆", 800},
        {"fang", "方", 700},  {"fei", "飞", 600},  {"fen", "分", 500},   {"feng", "风", 400},
        {"fo", "佛", 300},    {"fou", "否", 200},  {"fu", "服", 100},
    };
}

void assert_same_page(const cxxime::TranslationResult& actual,
                      const cxxime::TranslationResult& expected) {
    ASSERT_EQ(actual.page_index, expected.page_index);
    ASSERT_EQ(actual.page_offset, expected.page_offset);
    ASSERT_EQ(actual.entries.size(), expected.entries.size());
    for (std::size_t index = 0; index < actual.entries.size(); ++index) {
        ASSERT_TRUE(
            cxxime::same_candidate_entry_identity(actual.entries[index], expected.entries[index]));
        const auto& left = actual.entries[index];
        const auto& right = expected.entries[index];
        ASSERT_EQ(left.candidate.source, right.candidate.source);
        ASSERT_EQ(left.candidate.origin, right.candidate.origin);
        ASSERT_EQ(left.candidate.code, right.candidate.code);
        ASSERT_EQ(left.candidate.syllables, right.candidate.syllables);
        ASSERT_EQ(left.candidate.input_code, right.candidate.input_code);
        ASSERT_EQ(left.candidate.source_frequency, right.candidate.source_frequency);
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&left.selection);
        if (action) {
            const auto& other = std::get<cxxime::TextSelectionAction>(right.selection);
            ASSERT_EQ(action->primary_variant, other.primary_variant);
            ASSERT_EQ(action->variants.size(), other.variants.size());
            for (std::size_t variant = 0; variant < action->variants.size(); ++variant) {
                ASSERT_TRUE(cxxime::same_candidate_variant(action->variants[variant],
                                                           other.variants[variant]));
                ASSERT_EQ(action->variants[variant].frequency, other.variants[variant].frequency);
                ASSERT_EQ(action->variants[variant].source_frequency,
                          other.variants[variant].source_frequency);
                ASSERT_EQ(action->variants[variant].learning_target,
                          other.variants[variant].learning_target);
            }
        }
    }
}

std::vector<std::string> collect_pages(test::FallbackFixture& fixture, int visible_limit,
                                       const std::string& partial_text = {}) {
    std::vector<std::string> result;
    int previous_offset = -1;
    bool exhausted = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto& current = fixture.engine.context().translation();
        const int visible = (std::min)(visible_limit, static_cast<int>(current.entries.size()));
        if (current.page_offset != previous_offset) {
            ASSERT_EQ(current.page_offset, static_cast<int>(result.size()));
            for (int index = 0; index < visible; ++index) {
                const auto& entry = current.entries[index];
                ASSERT_TRUE(std::find(result.begin(), result.end(), entry.candidate.text) ==
                            result.end());
                result.push_back(entry.candidate.text);
                ASSERT_EQ(
                    std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes,
                    entry.candidate.text == partial_text ? std::size_t{1} : std::size_t{2});
            }
            previous_offset = current.page_offset;
        }
        if (!cxxime::candidate_extent_may_continue(current.extent, current.page_offset + visible)) {
            exhausted = true;
            break;
        }
        // Search must not acknowledge prefetched or previously displayed items.
        fixture.engine.translate_for_search("jf", 7);
        cxxime::KeyEvent next;
        next.keycode = VK_NEXT;
        fixture.engine.process_key(next, {}, visible);
    }
    ASSERT_TRUE(exhausted);
    return result;
}

} // namespace

TEST(ShuangpinFallbackPaging, mixed_keeps_known_candidates_and_host_limited_prefetch) {
    // Twenty words finish within the first batch; forty-five need another batch.
    for (int count : {20, 45}) {
        test::FallbackFixture fixture(homophones(count));
        auto wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
        ASSERT_TRUE(wubi->open_dict(fixture.path));
        ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
            fixture.config, fixture.dict, wubi, fixture.resources)));
        fixture.engine.switch_mode(cxxime::InputMode::MIXED);
        fixture.type("jf");
        const auto found = collect_pages(fixture, count == 20 ? 7 : 3);
        ASSERT_EQ(found.size(), static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            ASSERT_EQ(found[index], word_text(index));
        }
    }
}

TEST(ShuangpinFallbackPaging, real_mixed_sources_keep_one_sequence_across_page_sizes) {
    enum class Collision { kNone, kCustomPhrase, kSameFullWord, kPartialWord };
    for (Collision collision : {Collision::kNone, Collision::kCustomPhrase,
                                Collision::kSameFullWord, Collision::kPartialWord}) {
        std::vector<std::string> reference;
        for (int size : {3, 7, 9}) {
            auto pinyin_rows = homophones(45);
            std::vector<test::FallbackDictionaryRow> wubi_rows;
            for (int index = 0; index < 40; ++index) {
                wubi_rows.emplace_back("jf", "五笔" + std::to_string(1000 + index),
                                       100000 - 100 * index);
            }
            if (collision == Collision::kSameFullWord) {
                wubi_rows.emplace_back("jf", word_text(0), 200000);
            }
            if (collision == Collision::kPartialWord) {
                pinyin_rows.emplace_back("jun", "五笔1000", 100);
            }
            test::FallbackFixture fixture(pinyin_rows, test::fallback_schemes().back(), size);
            const std::string wubi_path = fixture.path + ".wubi";
            const std::string phrase_path = fixture.path + ".phrases";
            ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_rows));
            auto wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
            ASSERT_TRUE(wubi->open_dict(wubi_path));
            auto phrases = std::make_shared<cxxime::CustomPhrases>();
            ASSERT_TRUE(phrases->load(phrase_path));
            if (collision == Collision::kCustomPhrase) {
                ASSERT_TRUE(phrases->add("jf", word_text(0)));
            }
            ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
                fixture.config, fixture.dict, wubi, fixture.resources, nullptr, nullptr, phrases)));
            fixture.engine.switch_mode(cxxime::InputMode::MIXED);
            fixture.type("jf");
            const auto found = collect_pages(
                fixture, size, collision == Collision::kPartialWord ? "五笔1000" : "");
            ASSERT_EQ(found.size(), std::size_t{85});
            for (int index = 0; index < 45; ++index) {
                ASSERT_TRUE(std::find(found.begin(), found.end(), word_text(index)) != found.end());
            }
            for (int index = 0; index < 40; ++index) {
                ASSERT_TRUE(std::find(found.begin(), found.end(),
                                      "五笔" + std::to_string(1000 + index)) != found.end());
            }
            if (reference.empty()) {
                reference = found;
            } else {
                ASSERT_TRUE(found == reference);
            }
            fixture.engine.finalize();
            wubi->close();
            ASSERT_TRUE(DeleteFileA(wubi_path.c_str()));
            DeleteFileA(phrase_path.c_str());
        }
    }
}

TEST(ShuangpinFallbackPaging, custom_phrase_releases_the_hidden_partial_action) {
    auto rows = homophones(90);
    rows.emplace_back("jun", "军", 100);
    test::FallbackFixture fixture(rows);
    auto phrases = std::make_shared<cxxime::CustomPhrases>();
    const std::string phrase_path = fixture.path + ".phrases";
    ASSERT_TRUE(phrases->load(phrase_path));
    ASSERT_TRUE(phrases->add("jf", "军"));
    ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
        fixture.config, fixture.dict, nullptr, fixture.resources, nullptr, nullptr, phrases)));
    fixture.type("jf");
    ASSERT_EQ(fixture.engine.context().translation().entries.front().candidate.source,
              cxxime::CandidateSource::kCustomPhrase);
    const auto found = collect_pages(fixture, 7);
    ASSERT_EQ(found.size(), std::size_t{91});
    ASSERT_EQ(found.front(), "军");
    for (int index = 0; index < 90; ++index) {
        ASSERT_EQ(found[index + 1], word_text(index));
    }
    DeleteFileA(phrase_path.c_str());
}

TEST(ShuangpinFallbackPaging, returning_to_published_pages_replays_the_same_actions) {
    test::FallbackFixture fixture(homophones(90));
    fixture.type("jf");
    std::vector<cxxime::TranslationResult> pages = {fixture.engine.context().translation()};
    for (int page = 1; page <= 2; ++page) {
        fixture.press(VK_NEXT);
        ASSERT_EQ(fixture.engine.context().page_index(), page);
        pages.push_back(fixture.engine.context().translation());
    }
    for (int page = 1; page >= 0; --page) {
        fixture.press(VK_PRIOR);
        assert_same_page(fixture.engine.context().translation(), pages[page]);
    }
    for (int page = 1; page <= 2; ++page) {
        fixture.engine.translate_for_search("jf", 7);
        fixture.press(VK_NEXT);
        assert_same_page(fixture.engine.context().translation(), pages[page]);
    }
    fixture.press(VK_NEXT);
    ASSERT_EQ(fixture.engine.context().page_index(), 3);
    ASSERT_EQ(fixture.engine.context().translation().entries.front().candidate.text, word_text(21));
}

TEST(ShuangpinFallbackPaging, tracing_does_not_change_bounded_windows_or_continuation) {
    for (bool mixed_mode : {false, true}) {
        test::FallbackFixture fixture(homophones(60));
        const std::string wubi_path = fixture.path + ".wubi";
        std::vector<test::FallbackDictionaryRow> wubi_rows = {{"jf", word_text(0), 200000}};
        for (int index = 0; index < 20; ++index) {
            wubi_rows.emplace_back("jf", "五笔" + std::to_string(index), 10000 - index);
        }
        ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_rows));
        cxxime::Dict wubi(cxxime::UserDictKind::WUBI);
        ASSERT_TRUE(wubi.open_dict(wubi_path));
        cxxime::MixedTranslator mixed;
        mixed.set_pinyin_dict(fixture.dict.get());
        mixed.set_wubi_dict(&wubi);
        mixed.bind_pinyin(fixture.resources, {false});
        cxxime::ITranslator& translator =
            mixed_mode ? static_cast<cxxime::ITranslator&>(mixed) : fixture.translator;
        std::vector<cxxime::TranslationResult> reference;
        for (int trace_mode : {0, 1, 2}) {
            translator.clear_query_cache();
            cxxime::QueryBudget budget;
            budget.max_prefix_scan = 3;
            std::vector<cxxime::CandidateEntry> published;
            cxxime::TranslationRequest request;
            request.input = "jf";
            request.budget = &budget;
            request.published = &published;
            bool exhausted = false;
            for (std::size_t window = 0; window < 256; ++window) {
                cxxime::QueryTrace trace;
                request.trace =
                    trace_mode == 1 || (trace_mode == 2 && window % 2 != 0) ? &trace : nullptr;
                const auto result = translator.translate(request);
                if (request.trace) {
                    ASSERT_LE(trace.span_entry_scan_count, 3u);
                    ASSERT_LE(trace.span_query_count, 128u);
                    ASSERT_LE(trace.fallback_work_steps, 4096u);
                }
                if (trace_mode == 0) {
                    reference.push_back(result);
                } else {
                    ASSERT_TRUE(window < reference.size());
                    const auto& expected = reference[window];
                    assert_same_page(result, expected);
                    ASSERT_EQ(result.extent.known_count, expected.extent.known_count);
                    ASSERT_EQ(result.extent.state, expected.extent.state);
                    ASSERT_EQ(result.extent.complete, expected.extent.complete);
                    ASSERT_EQ(result.status, expected.status);
                    ASSERT_EQ(result.incremental, expected.incremental);
                    ASSERT_EQ(result.highlighted, expected.highlighted);
                    for (std::size_t index = 0; index < result.entries.size(); ++index) {
                        ASSERT_EQ(result.entries[index].candidate.frequency,
                        expected.entries[index].candidate.frequency);
                        ASSERT_EQ(result.entries[index].hint, expected.entries[index].hint);
                    }
                }
                const std::size_t accepted = (std::min)(std::size_t{3}, result.entries.size());
                published.insert(published.end(), result.entries.begin(),
                                 result.entries.begin() + accepted);
                if (result.extent.complete && accepted == result.entries.size()) {
                    exhausted = true;
                    ASSERT_EQ(window + 1, reference.size());
                    break;
                }
            }
            ASSERT_TRUE(exhausted);
            ASSERT_EQ(published.size(), mixed_mode ? std::size_t{80} : std::size_t{60});
        }
        wubi.close();
        ASSERT_TRUE(DeleteFileA(wubi_path.c_str()));
    }
}

TEST(ShuangpinFallbackPaging, explicit_page_offsets_do_not_consume_composition_work) {
    test::FallbackFixture fixture(homophones(20));
    auto pending = fixture.query("jf");
    ASSERT_TRUE(!pending.entries.empty());
    const auto first = fixture.translator.translate_page("jf", 0, 3);
    const auto second = fixture.translator.translate_page("jf", 1, 3);
    ASSERT_EQ(first.page_index, 0);
    ASSERT_EQ(first.page_offset, 0);
    ASSERT_EQ(second.page_index, 1);
    ASSERT_EQ(second.page_offset, 3);
    ASSERT_EQ(first.candidates.size(), std::size_t{3});
    ASSERT_EQ(second.candidates.size(), std::size_t{3});
    for (int index = 0; index < 3; ++index) {
        ASSERT_EQ(first.candidates[index].text, word_text(index));
        ASSERT_EQ(second.candidates[index].text, word_text(index + 3));
    }
    const auto offset = fixture.translator.translate_page("jf", 4, 3, nullptr, nullptr, nullptr, 5);
    ASSERT_EQ(offset.page_index, 4);
    ASSERT_EQ(offset.page_offset, 5);
    ASSERT_EQ(offset.candidates.front().text, word_text(5));
    const auto exhausted = fixture.translator.translate_page("jf", 10, 3);
    ASSERT_TRUE(exhausted.candidates.empty());
    ASSERT_TRUE(exhausted.extent.complete);
    assert_same_page(fixture.query("jf"), pending);
}

TEST(ShuangpinFallbackPaging, returning_history_reflows_for_a_narrower_host_viewport) {
    test::FallbackFixture fixture(homophones(45));
    fixture.type("jf");
    fixture.press(VK_NEXT);
    fixture.press(VK_NEXT);
    ASSERT_EQ(fixture.engine.context().page_index(), 2);
    fixture.press(VK_PRIOR);
    fixture.press(VK_PRIOR);
    ASSERT_EQ(fixture.engine.context().page_index(), 0);
    const auto found = collect_pages(fixture, 3);
    ASSERT_EQ(found.size(), std::size_t{45});
    for (int index = 0; index < 45; ++index) {
        ASSERT_EQ(found[index], word_text(index));
    }
}

TEST(ShuangpinFallbackPaging, returning_history_reflows_for_a_wider_host_viewport) {
    test::FallbackFixture fixture(homophones(30));
    fixture.type("jf");
    cxxime::KeyEvent key;
    key.keycode = VK_NEXT;
    fixture.engine.process_key(key, {}, 3);
    ASSERT_EQ(fixture.engine.context().page_offset(), 3);
    key.keycode = VK_PRIOR;
    fixture.engine.process_key(key, {}, 3);
    ASSERT_EQ(fixture.engine.context().page_offset(), 0);
    key.keycode = VK_NEXT;
    fixture.engine.process_key(key, {}, 7);
    const auto& current = fixture.engine.context().translation();
    ASSERT_EQ(current.page_index, 1);
    ASSERT_EQ(current.page_offset, 7);
    ASSERT_TRUE(!current.entries.empty());
    for (std::size_t index = 0; index < current.entries.size(); ++index) {
        ASSERT_EQ(current.entries[index].candidate.text, word_text(7 + static_cast<int>(index)));
    }
}

TEST(ShuangpinFallbackPaging, disabling_a_word_invalidates_both_directions_of_page_history) {
    test::FallbackFixture fixture(homophones(30));
    fixture.type("jf");
    fixture.press(VK_NEXT);
    fixture.press(VK_NEXT);
    ASSERT_EQ(fixture.engine.context().page_index(), 2);
    ASSERT_TRUE(fixture.dict->disable_system_entry(word_text(0)));
    fixture.press(VK_PRIOR);
    ASSERT_EQ(fixture.engine.context().page_index(), 0);
    ASSERT_EQ(fixture.engine.context().page_offset(), 0);
    ASSERT_TRUE(fixture.find(word_text(0), 2) == nullptr);
    const auto found = collect_pages(fixture, 7);
    ASSERT_EQ(found.size(), std::size_t{29});
    for (int index = 1; index < 30; ++index) {
        ASSERT_EQ(found[index - 1], word_text(index));
    }
}

TEST(ShuangpinFallbackPaging, deleting_the_only_user_word_invalidates_an_exhausted_page) {
    test::FallbackFixture fixture({{"ni", "你", 100}});
    ASSERT_TRUE(fixture.dict->add_user_entry("军服", "junfu", "jun:fu"));
    fixture.type("jf");
    ASSERT_TRUE(fixture.find("军服", 2) != nullptr);
    ASSERT_TRUE(fixture.engine.context().translation().extent.complete);
    ASSERT_TRUE(fixture.dict->delete_user_entries({{"军服", "junfu"}}));
    fixture.press(VK_NEXT);
    ASSERT_TRUE(fixture.engine.context().translation().entries.empty());
    ASSERT_EQ(fixture.engine.context().page_index(), 0);
    fixture.press(VK_SPACE);
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
    ASSERT_EQ(fixture.engine.context().active_input(), "jf");
}

TEST(ShuangpinFallbackPaging, personal_resolution_obeys_the_shared_scan_budget) {
    enum class Source { kPreference, kManual, kMixedManual };
    for (Source source : {Source::kPreference, Source::kManual, Source::kMixedManual}) {
        const auto rows = homophones(160);
        test::FallbackFixture fixture(rows);
        fixture.translator.set_sentence_composition_enabled(false);
        fixture.translator.set_candidate_learning_enabled(true);
        const std::string target = word_text(159);
        if (source != Source::kPreference) {
            ASSERT_TRUE(fixture.dict->load_manual_candidate_order(fixture.path + ".manual"));
            ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save(
                "junfu", {{target, "junfu", "jun:fu"}}));
        } else {
            cxxime::Candidate candidate;
            candidate.text = target;
            candidate.code = "junfu";
            candidate.syllables = "jun:fu";
            ASSERT_TRUE(fixture.dict->record_candidate_preference(candidate, "junfu"));
        }
        auto wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
        ASSERT_TRUE(wubi->open_dict(fixture.path));
        cxxime::MixedTranslator mixed;
        mixed.set_pinyin_dict(fixture.dict.get());
        mixed.set_wubi_dict(wubi.get());
        mixed.bind_pinyin(fixture.resources, {false});
        mixed.set_sentence_composition_enabled(false);
        mixed.set_candidate_learning_enabled(true);
        cxxime::ITranslator& translator = source == Source::kMixedManual
                                              ? static_cast<cxxime::ITranslator&>(mixed)
                                              : fixture.translator;
        cxxime::QueryBudget budget;
        budget.max_prefix_scan = 1;
        std::vector<cxxime::CandidateEntry> published;
        cxxime::TranslationRequest request;
        request.input = "jf";
        request.budget = &budget;
        request.published = &published;
        bool exhausted = false;
        uint32_t cumulative_scans = 0;
        for (int window = 0; window < 1000; ++window) {
            cxxime::QueryTrace trace;
            request.trace = &trace;
            auto result = translator.translate(request);
            ASSERT_TRUE(trace.span_query_count <= 128);
            ASSERT_TRUE(trace.span_entry_scan_count <= 1);
            cumulative_scans += trace.span_entry_scan_count;
            for (auto& entry : result.entries) {
                if (entry.candidate.text == target) {
                    // Both direct recall and personal resolution must account
                    // for traversing this homophone range to its last record.
                    ASSERT_TRUE(cumulative_scans >= rows.size());
                }
                ASSERT_TRUE(
                    std::none_of(published.begin(), published.end(), [&](const auto& previous) {
                        return previous.candidate.text == entry.candidate.text;
                    }));
                published.push_back(std::move(entry));
            }
            if (result.extent.complete) {
                exhausted = true;
                break;
            }
        }
        ASSERT_TRUE(exhausted);
        ASSERT_EQ(published.size(), rows.size());
    }
}

TEST(ShuangpinFallbackPaging, composition_does_not_require_single_character_dictionary_entries) {
    test::FallbackFixture fixture({{"jun:fu", "军服", 200}, {"mi:ma", "密码", 100}});
    const std::string input = "jfmima";
    const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, input, false);
    ASSERT_TRUE(graph.fallback);
    ASSERT_TRUE(!graph.align({}, "jun:fu:mi:ma", input.size()).empty());
    const auto result = fixture.query(input, false);
    ASSERT_EQ(result.entries.size(), std::size_t{1});
    const auto& entry = result.entries.front();
    ASSERT_EQ(entry.candidate.text, "军服密码");
    ASSERT_EQ(entry.candidate.origin, cxxime::CandidateOrigin::kComposed);
    const auto& action = std::get<cxxime::TextSelectionAction>(entry.selection);
    ASSERT_EQ(action.consumed_input_bytes, input.size());
    ASSERT_EQ(action.variants.front().code, "junfumima");
    ASSERT_EQ(action.variants.front().syllables, "jun:fu:mi:ma");
}

TEST(ShuangpinFallbackPaging, generated_recommendations_do_not_depend_on_window_boundaries) {
    // None is stored as a whole word, so this isolates the finite composed set.
    auto rows = composition_initial_words();
    rows.emplace_back("mi", "迷", 1000);
    std::vector<cxxime::CandidateEntry> reference;
    for (uint32_t scan_limit : {2048u, 1u, 5u}) {
        test::FallbackFixture fixture(rows);
        cxxime::QueryBudget budget;
        budget.max_prefix_scan = scan_limit;
        cxxime::TranslationRequest request;
        request.input = "jfmi";
        request.budget = &budget;
        request.policy.allow_partial_selection = true;
        std::vector<cxxime::CandidateEntry> published;
        std::vector<cxxime::CandidateEntry> full;
        request.published = &published;
        bool exhausted = false;
        for (int window = 0; window < 1000; ++window) {
            cxxime::QueryTrace trace;
            request.trace = &trace;
            auto result = fixture.translator.translate(request);
            ASSERT_TRUE(trace.span_query_count <= 128);
            ASSERT_TRUE(trace.span_entry_scan_count <= scan_limit);
            ASSERT_TRUE(trace.composition_state_count <= 1024);
            for (auto& entry : result.entries) {
                if (std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes ==
                    request.input.size()) {
                    ASSERT_EQ(entry.candidate.origin, cxxime::CandidateOrigin::kComposed);
                    full.push_back(entry);
                }
                ASSERT_TRUE(
                    std::none_of(published.begin(), published.end(), [&](const auto& previous) {
                        return previous.candidate.text == entry.candidate.text;
                    }));
                published.push_back(std::move(entry));
            }
            ASSERT_TRUE(full.size() <= 1);
            if (result.extent.complete) {
                exhausted = true;
                break;
            }
            if (window == 0 && scan_limit != 2048) {
                // An expired window pauses existing work without relying on
                // machine speed or a sleep to interrupt at the desired time.
                budget.deadline.enabled = true;
                budget.deadline.expires_at = cxxime::QueryDeadline::Clock::time_point{};
                cxxime::QueryTrace expired_trace;
                request.trace = &expired_trace;
                const auto paused = fixture.translator.translate(request);
                ASSERT_TRUE(paused.entries.empty());
                ASSERT_TRUE(!paused.extent.complete);
                ASSERT_EQ(expired_trace.span_query_count, 0u);
                ASSERT_EQ(expired_trace.span_entry_scan_count, 0u);
                budget.deadline = {};
            }
        }
        ASSERT_TRUE(exhausted);
        ASSERT_EQ(full.size(), std::size_t{1});
        ASSERT_GT(published.size(), full.size());
        if (reference.empty()) {
            reference = std::move(full);
        } else {
            ASSERT_EQ(full.size(), reference.size());
            for (std::size_t index = 0; index < reference.size(); ++index) {
                ASSERT_TRUE(
                    cxxime::same_candidate_entry_identity(full[index], reference[index]));
                ASSERT_EQ(full[index].candidate.syllables,
                          reference[index].candidate.syllables);
                ASSERT_EQ(full[index].candidate.frequency,
                          reference[index].candidate.frequency);
            }
        }
    }
}

TEST(ShuangpinFallbackPaging, fuzzy_combinations_do_not_close_the_normal_recommendation_set) {
    for (bool has_normal_words : {true, false}) {
        auto rows = composition_initial_words();
        if (has_normal_words) {
            rows.emplace_back("li", "里", 10);
        }
        rows.emplace_back("ni", "你", 100000000);
        test::FallbackFixture fixture(rows);
        fixture.translator.bind_pinyin(fixture.resources, {true});
        const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, "jfli", true);
        ASSERT_TRUE(graph.fallback);
        const auto normal = graph.align({}, "jun:fu:li", 4);
        const auto fuzzy = graph.align({}, "jun:fu:ni", 4);
        ASSERT_EQ(normal.size(), std::size_t{1});
        ASSERT_EQ(fuzzy.size(), std::size_t{1});
        ASSERT_EQ(normal.front().spelling_types.back(), cxxime::kNormalSpelling);
        ASSERT_EQ(fuzzy.front().spelling_types.back(), cxxime::kFuzzySpelling);

        cxxime::TranslationRequest request;
        request.input = "jfli";
        std::vector<cxxime::CandidateEntry> published;
        request.published = &published;
        bool exhausted = false;
        for (int window = 0; window < 100; ++window) {
            auto result = fixture.translator.translate(request);
            for (auto& entry : result.entries) {
                ASSERT_EQ(entry.candidate.origin, cxxime::CandidateOrigin::kComposed);
                const auto& syllables = entry.candidate.syllables;
                const std::string last = syllables.substr(syllables.rfind(':') + 1);
                ASSERT_EQ(last, has_normal_words ? "li" : "ni");
                ASSERT_TRUE(
                    std::none_of(published.begin(), published.end(), [&](const auto& previous) {
                        return previous.candidate.text == entry.candidate.text;
                    }));
                published.push_back(std::move(entry));
            }
            if (result.extent.complete) {
                exhausted = true;
                break;
            }
        }
        ASSERT_TRUE(exhausted);
        // Abundant high-frequency fuzzy nodes must not stop the search before
        // the normal search completes. Without normal words, fuzzy recall survives.
        ASSERT_EQ(published.size(), std::size_t{1});
    }
}
