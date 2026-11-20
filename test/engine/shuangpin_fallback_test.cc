// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include <cxxime/composition_learning.h>
#include <cxxime/query_trace.h>
#include <cxxime/pinyin_input_graph.h>

#include "support/shuangpin_fallback_fixture.h"

TEST(ShuangpinFallback, all_schemes_mix_initials_with_complete_syllables_and_commit) {
    for (const auto& scheme : test::fallback_schemes()) {
        test::FallbackFixture fixture({{"jun", "军", 100}, {"fu", "服", 100}, {"mi", "迷", 100}},
                                      scheme);
        // The same left-to-right parse is valid in all four schemas. In
        // contrast, Xiaohe can parse jfni as j'fn'i because fn also spells fiao.
        const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, "jfmi", false);
        ASSERT_TRUE(graph.fallback);
        const auto paths = graph.align({}, "jun:fu:mi", 4);
        ASSERT_EQ(paths.size(), std::size_t{1});
        ASSERT_TRUE(paths.front().input_lengths == std::vector<uint16_t>({1, 1, 2}));
        fixture.type("jfmi");
        const auto* entry = fixture.find("军服迷", 4);
        ASSERT_TRUE(entry != nullptr);
        const auto& action = std::get<cxxime::TextSelectionAction>(entry->selection);
        ASSERT_EQ(action.variants.front().input_code, "junfumi");
        ASSERT_EQ(action.variants.front().syllables, "jun:fu:mi");
        const auto presentation = cxxime::derive_composition_presentation(
            fixture.engine.context().composition(), fixture.resources.get(), 4, true, "jun:fu:mi",
            true, false);
        ASSERT_EQ(presentation.display_preedit, "j'f'mi");
        fixture.press(VK_BACK);
        fixture.press('I');
        ASSERT_TRUE(fixture.find("军服迷", 4) != nullptr);
        fixture.press(VK_SPACE);
        ASSERT_EQ(fixture.engine.get_commit_text(), "军服迷");
        ASSERT_TRUE(!fixture.engine.context().is_composing());
    }
}

TEST(ShuangpinFallback, direct_words_keep_origin_and_disabled_first_does_not_hide_second) {
    test::FallbackFixture fixture(
        {{"jun:fu", "军服", 1000}, {"jun:fu", "均富", 900}, {"jun", "军", 100}, {"fu", "服", 100}});
    ASSERT_TRUE(fixture.dict->disable_system_entry("军服"));
    fixture.type("jf");
    const auto* candidate = fixture.find("均富", 2);
    ASSERT_TRUE(candidate != nullptr);
    ASSERT_EQ(candidate->candidate.origin, cxxime::CandidateOrigin::kSystem);
    ASSERT_TRUE(fixture.find("军服", 2) == nullptr);
}

TEST(ShuangpinFallback, normal_pair_wins_even_over_high_frequency_initial_words) {
    test::FallbackFixture fixture({{"jin", "今", 1}, {"jun:ni", "军你", 1000000}});
    fixture.type("jn");
    ASSERT_TRUE(fixture.find("今", 2) != nullptr);
    ASSERT_TRUE(fixture.find("军你", 2) == nullptr);
    ASSERT_TRUE(!cxxime::PinyinInputGraph::build(*fixture.resources, "jn", false).fallback);
}

TEST(ShuangpinFallback, illegal_middle_preserves_suffix_and_empty_space_does_not_commit) {
    test::FallbackFixture fixture({{"ni", "你", 100}, {"ni:hao", "你好", 1000}});
    fixture.type("ni;hk");
    const auto* entry = fixture.find("你", 2);
    ASSERT_TRUE(entry != nullptr);
    const auto& entries = fixture.engine.context().translation().entries;
    ASSERT_TRUE(fixture.engine.select_candidate(static_cast<int>(entry - entries.data())));
    ASSERT_EQ(fixture.engine.context().active_input(), ";hk");
    fixture.press(VK_SPACE);
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
    ASSERT_EQ(fixture.engine.context().active_input(), ";hk");
    ASSERT_TRUE(fixture.engine.translate_for_search("ni;hk", 7).candidates.empty());
}

TEST(ShuangpinFallback, user_only_canonical_word_does_not_need_a_system_prefix) {
    test::FallbackFixture fixture({{"ni", "你", 100}});
    ASSERT_TRUE(fixture.dict->add_user_entry("军服", "junfu", "jun:fu"));
    fixture.type("jf");
    const auto* entry = fixture.find("军服", 2);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->candidate.origin, cxxime::CandidateOrigin::kUser);
    ASSERT_EQ(std::get<cxxime::TextSelectionAction>(entry->selection).variants.front().input_code,
              "junfu");
}

TEST(ShuangpinFallback, empty_user_readings_align_to_the_graph_instead_of_segment_best) {
    test::FallbackFixture fixture({{"ni", "你", 100}});
    ASSERT_TRUE(fixture.dict->add_user_entry("军服", "junfu"));
    fixture.type("jf");
    const auto* entry = fixture.find("军服", 2);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->candidate.syllables, "jun:fu");
    const int unpinned_frequency = entry->candidate.frequency;
    ASSERT_TRUE(fixture.dict->load_manual_candidate_order(fixture.path + ".manual"));
    ASSERT_TRUE(
        fixture.dict->replace_manual_candidate_order_and_save("junfu", {{"军服", "junfu", {}}}));
    fixture.press(VK_NEXT);
    entry = fixture.find("军服", 2);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->candidate.origin, cxxime::CandidateOrigin::kUser);
    ASSERT_EQ(entry->candidate.syllables, "jun:fu");
    ASSERT_TRUE(entry->candidate.frequency > unpinned_frequency);
}

TEST(ShuangpinFallback, personal_word_uses_the_longest_matching_partial_endpoint) {
    const std::string input = "oo;";
    test::FallbackFixture fixture({{"ni", "你", 100}});
    ASSERT_TRUE(fixture.dict->add_user_entry("哦", "o", "o"));
    const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, input, false);
    ASSERT_TRUE(graph.fallback);
    ASSERT_TRUE(graph.align({}, "o", 1).empty());
    ASSERT_TRUE(!graph.align({}, "o", 2).empty());
    // Exercise reading endpoints and raw suffix consumption independently of
    // the processor's code-key and punctuation routing.
    const auto result = fixture.query(input);
    ASSERT_EQ(result.entries.size(), std::size_t{1});
    const auto& entry = result.entries.front();
    ASSERT_EQ(entry.candidate.text, "哦");
    const auto& action = std::get<cxxime::TextSelectionAction>(entry.selection);
    ASSERT_EQ(action.consumed_input_bytes, std::size_t{2});
    cxxime::CompositionState composition;
    ASSERT_TRUE(composition.set_active_input(input, input.size()));
    ASSERT_TRUE(composition.confirm_prefix(action));
    ASSERT_EQ(composition.active().input, input.substr(2));
    ASSERT_EQ(composition.converted_segments().front().raw_input, "oo");
}

TEST(ShuangpinFallback, a_dense_short_prefix_does_not_starve_the_longer_selection_boundary) {
    std::vector<test::FallbackDictionaryRow> rows;
    for (int index = 0; index < 40; ++index) {
        rows.emplace_back("jun", "短词" + std::to_string(1000 + index), 100000 - 100 * index);
    }
    rows.emplace_back("jun:fu", "军服", 1);
    test::FallbackFixture fixture(rows);
    const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, "jf;", false);
    ASSERT_TRUE(graph.fallback);
    ASSERT_TRUE(!graph.align({}, "jun", 1).empty());
    ASSERT_TRUE(!graph.align({}, "jun:fu", 2).empty());
    // Both are partial actions before an invalid suffix. The first fixed work
    // window must expose the longer word without exhausting all short homophones.
    const auto result = fixture.query("jf;");
    const auto longer =
        std::find_if(result.entries.begin(), result.entries.end(),
                     [](const auto& entry) { return entry.candidate.text == "军服"; });
    ASSERT_TRUE(longer != result.entries.end());
    ASSERT_EQ(std::get<cxxime::TextSelectionAction>(longer->selection).consumed_input_bytes,
              std::size_t{2});
    ASSERT_TRUE(std::any_of(result.entries.begin(), result.entries.end(), [](const auto& entry) {
        return std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes == 1;
    }));
}

TEST(ShuangpinFallback, personal_ranking_does_not_leak_between_canonical_query_keys) {
    for (bool manual : {false, true}) {
        test::FallbackFixture fixture({{"jun:fu", "军服", 1000}, {"jun:fu", "均富", 1}});
        fixture.translator.set_candidate_learning_enabled(true);
        cxxime::Candidate preferred;
        preferred.text = "均富";
        preferred.code = "junfu";
        preferred.syllables = "jun:fu";
        if (manual) {
            ASSERT_TRUE(fixture.dict->load_manual_candidate_order(fixture.path + ".manual"));
            ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save(
                "jun", {{"均富", "junfu", "jun:fu"}}));
        } else {
            ASSERT_TRUE(fixture.dict->record_candidate_preference(preferred, "jun"));
        }
        auto result = fixture.query("jf");
        ASSERT_EQ(result.entries.size(), std::size_t{2});
        ASSERT_EQ(result.entries.front().candidate.text, "军服");
        if (manual) {
            ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save(
                "junfu", {{"均富", "junfu", "jun:fu"}}));
        } else {
            ASSERT_TRUE(fixture.dict->record_candidate_preference(preferred, "junfu"));
        }
        result = fixture.query("jf");
        ASSERT_EQ(result.entries.front().candidate.text, "均富");
    }
}

TEST(ShuangpinFallback, normal_reading_precedes_fuzzy_frequency_in_direct_and_composed_words) {
    const std::vector<std::vector<test::FallbackDictionaryRow>> dictionaries = {
        {{"jun:fu:li", "军服里", 100}, {"jun:fu:ni", "军服你", 100000000}},
        {{"jun", "军", 100}, {"fu", "服", 100}, {"li", "里", 100}, {"ni", "你", 100000000}},
    };
    for (bool composed : {false, true}) {
        test::FallbackFixture fixture(dictionaries[composed ? 1 : 0]);
        fixture.translator.bind_pinyin(fixture.resources, {true});
        const auto result = fixture.query("jfli");
        const auto normal =
            std::find_if(result.entries.begin(), result.entries.end(),
                         [](const auto& entry) { return entry.candidate.text == "军服里"; });
        const auto fuzzy =
            std::find_if(result.entries.begin(), result.entries.end(),
                         [](const auto& entry) { return entry.candidate.text == "军服你"; });
        ASSERT_TRUE(normal != result.entries.end());
        if (composed) {
            ASSERT_EQ(normal->candidate.origin, cxxime::CandidateOrigin::kComposed);
            ASSERT_TRUE(fuzzy == result.entries.end());
        } else {
            ASSERT_TRUE(fuzzy != result.entries.end());
            ASSERT_TRUE(normal < fuzzy);
        }
    }
}

TEST(ShuangpinFallback, merging_the_same_text_keeps_normal_primary_and_both_readings) {
    test::FallbackFixture fixture({{"jun:fu:li", "同词", 100}, {"jun:fu:ni", "同词", 100000000}});
    fixture.translator.bind_pinyin(fixture.resources, {true});
    fixture.translator.set_sentence_composition_enabled(false);
    const auto result = fixture.query("jfli", false);
    ASSERT_EQ(result.entries.size(), std::size_t{1});
    const auto& entry = result.entries.front();
    ASSERT_EQ(entry.candidate.text, "同词");
    ASSERT_EQ(entry.candidate.syllables, "jun:fu:li");
    const auto& action = std::get<cxxime::TextSelectionAction>(entry.selection);
    ASSERT_EQ(action.consumed_input_bytes, std::size_t{4});
    ASSERT_EQ(action.variants.size(), std::size_t{2});
    ASSERT_TRUE(action.primary_variant < action.variants.size());
    ASSERT_EQ(action.variants[action.primary_variant].syllables, "jun:fu:li");
    for (const std::string reading : {"jun:fu:li", "jun:fu:ni"}) {
        ASSERT_EQ(std::count_if(action.variants.begin(), action.variants.end(),
                                [&](const auto& variant) { return variant.syllables == reading; }),
                  1);
    }
}

TEST(ShuangpinFallback, actual_dictionary_intersection_reaches_late_jun_fu_branch) {
    const std::vector<std::string> readings = {
        "ji",   "jia",   "jian", "jiang", "jiao", "jie", "jin",
        "jing", "jiong", "jiu",  "ju",    "juan", "jue", "jun",
    };
    std::vector<test::FallbackDictionaryRow> rows;
    for (std::size_t index = 0; index < readings.size(); ++index) {
        rows.emplace_back(readings[index], "字" + std::to_string(index), 100);
        rows.emplace_back(readings[index] + ":ni", "干扰" + std::to_string(index), 100);
    }
    rows.emplace_back("jun:fu", "军服", 200);
    test::FallbackFixture fixture(rows);
    fixture.type("jf");
    ASSERT_TRUE(fixture.find("军服", 2) != nullptr);
}

TEST(ShuangpinFallback, pending_empty_window_resumes_without_losing_candidates) {
    test::FallbackFixture fixture({{"jun:fu", "军服", 100}});
    cxxime::QueryBudget budget;
    budget.max_prefix_scan = 0;
    auto result = fixture.query("jf", true, &budget);
    ASSERT_TRUE(result.usable());
    ASSERT_TRUE(result.entries.empty());
    ASSERT_TRUE(result.incremental);
    ASSERT_TRUE(!result.extent.complete);
    budget.max_prefix_scan = 2048;
    result = fixture.query("jf", true, &budget);
    ASSERT_TRUE(std::any_of(result.entries.begin(), result.entries.end(),
                            [](const auto& entry) { return entry.candidate.text == "军服"; }));
}

TEST(ShuangpinFallback, pending_page_down_fills_page_zero_without_an_empty_history_page) {
    test::FallbackFixture fixture({{"jun:fu", "军服", 100}});
    fixture.type("jf");
    auto pending = fixture.engine.context().translation();
    ASSERT_TRUE(pending.incremental);
    pending.entries.clear();
    pending.highlighted = -1;
    pending.status = cxxime::TranslationStatus::kStableDegraded;
    pending.extent = cxxime::make_candidate_extent(0, 0, true);
    fixture.engine.context().update_translation(std::move(pending));
    fixture.press(VK_NEXT);
    ASSERT_TRUE(fixture.find("军服", 2) != nullptr);
    ASSERT_EQ(fixture.engine.context().page_index(), 0);
    ASSERT_EQ(fixture.engine.context().page_offset(), 0);
    fixture.press(VK_PRIOR);
    ASSERT_TRUE(fixture.find("军服", 2) != nullptr);
    ASSERT_EQ(fixture.engine.context().page_index(), 0);
}

TEST(ShuangpinFallback, personal_sources_are_independent_and_version_changes_invalidate_work) {
    test::FallbackFixture fixture({{"ni", "你", 100}});
    ASSERT_TRUE(fixture.dict->add_user_entry("军服", "junfu", "jun:fu"));
    ASSERT_TRUE(fixture.dict->load_manual_candidate_order(fixture.path + ".manual"));
    ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save(
        "junfu", {{"军服", "junfu", "jun:fu"}}));
    cxxime::Candidate preference;
    preference.text = "均富";
    preference.code = "junfu";
    preference.syllables = "jun:fu";
    ASSERT_TRUE(fixture.dict->record_candidate_preference(preference, "junfu"));
    {
        std::ofstream output(fixture.path + ".learning", std::ios::binary);
        output << "军服你\tjunfuni\tjun:fu:ni\t1\t1\n";
    }
    cxxime::CompositionLearningService learning;
    ASSERT_TRUE(learning.load(fixture.path + ".learning"));
    fixture.translator.set_candidate_learning_enabled(true);
    fixture.translator.set_composition_learning_service(&learning);
    auto result = fixture.query("jf");
    ASSERT_TRUE(!result.entries.empty());
    ASSERT_EQ(result.entries.front().candidate.text, "军服");
    ASSERT_TRUE(std::any_of(result.entries.begin(), result.entries.end(),
                            [](const auto& item) { return item.candidate.text == "均富"; }));
    result = fixture.query("jfni");
    ASSERT_TRUE(std::any_of(result.entries.begin(), result.entries.end(), [](const auto& item) {
        return item.candidate.text == "军服你" &&
               std::get<cxxime::TextSelectionAction>(item.selection).consumed_input_bytes == 4;
    }));
    fixture.translator.set_candidate_learning_enabled(false);
    result = fixture.query("jfni");
    ASSERT_TRUE(std::none_of(result.entries.begin(), result.entries.end(),
                             [](const auto& item) { return item.candidate.text == "军服你"; }));
}

TEST(ShuangpinFallback, many_ranges_and_small_scan_windows_progress_with_bounded_state) {
    std::vector<test::FallbackDictionaryRow> rows;
    for (const std::string initial : {"ji", "jia", "jian", "jiang", "jiao", "jie", "jin", "jing",
                                      "jiong", "jiu", "ju", "juan", "jue", "jun"}) {
        for (const std::string final :
             {"fa", "fan", "fang", "fei", "fen", "feng", "fo", "fou", "fu"}) {
            rows.emplace_back(initial + ':' + final, "词" + initial + final, 100);
        }
    }
    test::FallbackFixture fixture(rows);
    cxxime::QueryBudget budget;
    budget.max_prefix_scan = 5;
    cxxime::TranslationRequest request;
    request.input = "jf";
    request.budget = &budget;
    std::vector<cxxime::CandidateEntry> published;
    request.published = &published;
    bool exhausted = false;
    for (int window = 0; window < 500; ++window) {
        cxxime::QueryTrace trace;
        request.trace = &trace;
        auto result = fixture.translator.translate(request);
        ASSERT_TRUE(trace.span_query_count <= budget.max_prefix_scan);
        ASSERT_TRUE(trace.span_entry_scan_count <= 5);
        ASSERT_TRUE(trace.fallback_buffer_peak <= 7 * 32);
        ASSERT_TRUE(trace.fallback_frame_peak <= 4 * 3);
        for (const auto& entry : result.entries) {
            ASSERT_TRUE(std::none_of(published.begin(), published.end(), [&](const auto& previous) {
                return cxxime::same_candidate_entry_identity(previous, entry);
            }));
            published.push_back(entry);
        }
        if (result.extent.complete) {
            exhausted = true;
            break;
        }
    }
    ASSERT_TRUE(exhausted);
    ASSERT_EQ(published.size(), rows.size());
}

TEST(ShuangpinFallback, homophones_resume_past_the_generated_set_and_keep_page_order) {
    std::vector<test::FallbackDictionaryRow> rows;
    for (int index = 0; index < 90; ++index) {
        rows.emplace_back("jun:fu", "词" + std::to_string(index), 1000 - index);
    }
    std::vector<std::string> reference;
    for (int size : {3, 7, 9}) {
        test::FallbackFixture fixture(rows, test::fallback_schemes().back(), size);
        fixture.type("jf");
        std::vector<std::string> found;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto& translation = fixture.engine.context().translation();
            for (const auto& entry : translation.entries) {
                ASSERT_TRUE(std::find(found.begin(), found.end(), entry.candidate.text) ==
                            found.end());
                found.push_back(entry.candidate.text);
                ASSERT_EQ(
                    std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes,
                    std::size_t{2});
            }
            if (!cxxime::candidate_extent_may_continue(
                    translation.extent, translation.page_offset + translation.entries.size())) {
                break;
            }
            // An unrelated search must not consume this composition's pending batch.
            fixture.engine.translate_for_search("ni", 7);
            const int offset = translation.page_offset;
            fixture.press(VK_NEXT);
            if (fixture.engine.context().page_offset() == offset) {
                break;
            }
        }
        ASSERT_EQ(found.size(), std::size_t{90});
        if (reference.empty()) {
            reference = found;
        } else {
            ASSERT_TRUE(found == reference);
        }
    }
}

TEST(ShuangpinFallback, zero_initial_selection_uses_the_established_pair_and_keeps_raw_suffix) {
    test::FallbackFixture fixture(
        {{"o", "哦", 100}, {"o:a", "哦啊", 100}, {"o:o:a", "哦哦啊", 1000000}});
    fixture.type("ooa");
    ASSERT_TRUE(fixture.find("哦哦啊", 3) == nullptr);
    const auto* whole = fixture.find("哦啊", 3);
    ASSERT_TRUE(whole != nullptr);
    const auto presentation = cxxime::derive_composition_presentation(
        fixture.engine.context().composition(), fixture.resources.get(), 3, true, "o:a", true);
    ASSERT_EQ(presentation.display_preedit, "oo'a");
    fixture.engine.clear();
    fixture.type("ooab");
    const auto* prefix = fixture.find("哦", 2);
    ASSERT_TRUE(prefix != nullptr);
    ASSERT_TRUE(fixture.find("哦", 1) == nullptr);
    ASSERT_TRUE(fixture.find("哦啊", 3) == nullptr);
    const auto& entries = fixture.engine.context().translation().entries;
    ASSERT_TRUE(fixture.engine.select_candidate(static_cast<int>(prefix - entries.data())));
    ASSERT_EQ(fixture.engine.context().active_input(), "ab");
    ASSERT_EQ(fixture.engine.context().composition().converted_segments().front().raw_input, "oo");
}

TEST(ShuangpinFallback, appending_initials_keeps_the_previous_text_across_word_boundaries) {
    for (const auto& scheme : test::fallback_schemes()) {
        test::FallbackFixture fixture({{"wo", "我", 90000},
                                       {"jiu", "就", 80000},
                                       {"jiu:fan", "就范", 50000},
                                       {"jing:fang", "警方", 900000}},
                                      scheme);
        fixture.type("woj");
        ASSERT_TRUE(fixture.engine.context().candidate(0) != nullptr);
        ASSERT_EQ(fixture.engine.context().candidate(0)->text, "我就");
        fixture.type("f");
        ASSERT_EQ(fixture.engine.context().candidate(0)->text, "我就范");
        fixture.press(VK_SPACE);
        ASSERT_EQ(fixture.engine.get_commit_text(), "我就范");
        ASSERT_TRUE(!fixture.engine.context().is_composing());
    }
}

TEST(ShuangpinFallback, completing_normal_spelling_keeps_dictionary_predictions) {
    for (const auto& scheme : test::fallback_schemes()) {
        test::FallbackFixture fixture({{"ni:hao", "你好", 10000}}, scheme);
        fixture.type("n");
        ASSERT_TRUE(fixture.engine.context().candidate(0) != nullptr);
        ASSERT_EQ(fixture.engine.context().candidate(0)->text, "你好");
        fixture.type("i");
        ASSERT_TRUE(fixture.engine.context().candidate(0) != nullptr);
        ASSERT_EQ(fixture.engine.context().candidate(0)->text, "你好");
        fixture.press(VK_SPACE);
        ASSERT_EQ(fixture.engine.get_commit_text(), "你好");
        ASSERT_TRUE(!fixture.engine.context().is_composing());
    }
}

TEST(ShuangpinFallback, real_dictionary_appending_keys_keeps_boundaries_and_full_first_choices) {
    auto dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dict->open_dict(std::string(CXXIME_DATA_DIR) + "pinyin.dict.bin"));
    const std::string input = "sldfjdsljfdsdff";
    const std::vector<std::string> common_displays = {
        "sl'df'jd'sl",           "sl'df'jd'sl'j",           "sl'df'jd'sl'j'f",
        "sl'df'jd'sl'j'f'd",     "sl'df'jd'sl'j'f'ds",      "sl'df'jd'sl'j'f'ds'd",
        "sl'df'jd'sl'j'f'ds'df", "sl'df'jd'sl'j'f'ds'df'f",
    };
    const std::vector<std::string> xiaohe_displays = {
        "s'ld'fj'ds'l",          "s'ld'fj'ds'lj",           "s'ld'fj'ds'lj'f",
        "s'ld'fj'ds'lj'f'd",     "s'ld'fj'ds'lj'f'ds",      "s'ld'fj'ds'lj'f'ds'd",
        "s'ld'fj'ds'lj'f'ds'df", "s'ld'fj'ds'lj'f'ds'df'f",
    };
    const std::vector<std::string> xiaohe_fuzzy_displays = {
        "sl'df'j'ds'l",          "sl'df'j'ds'lj",           "sl'df'j'ds'lj'f",
        "sl'df'j'ds'lj'f'd",     "sl'df'j'ds'lj'f'ds",      "sl'df'j'ds'lj'f'ds'd",
        "sl'df'j'ds'lj'f'ds'df", "sl'df'j'ds'lj'f'ds'df'f",
    };
    for (const auto& scheme : test::fallback_schemes()) {
        const auto resources = cxxime::PinyinResourceSet::create(
            scheme.id, cxxime::PinyinSchemeKind::kShuangpin, scheme.path);
        ASSERT_TRUE(resources != nullptr);
        for (bool fuzzy : {false, true}) {
            cxxime::Config config;
            config.pinyin_scheme = scheme.id;
            const auto& displays = config.pinyin_scheme == "xiaohe_shuangpin"
                                       ? (fuzzy ? xiaohe_fuzzy_displays : xiaohe_displays)
                                       : common_displays;
            config.fuzzy_pinyin = fuzzy;
            config.candidate_learning = false;
            config.page_size = 7;
            cxxime::Engine engine;
            ASSERT_TRUE(engine.initialize(
                cxxime::EngineRuntimeState::create(config, dict, nullptr, resources)));
            engine.set_partial_selection_enabled(true);
            // Keep the production range/scan/work budgets, without a machine-speed assertion.
            engine.set_query_deadline_ms(0);
            const auto press = [&](uint32_t key) {
                cxxime::KeyEvent event;
                event.keycode = key;
                engine.process_key(event);
            };
            const auto check_first = [&](std::size_t size) {
                const auto& entries = engine.context().translation().entries;
                ASSERT_TRUE(!entries.empty());
                const auto* action =
                    std::get_if<cxxime::TextSelectionAction>(&entries.front().selection);
                ASSERT_TRUE(action != nullptr);
                ASSERT_EQ(action->consumed_input_bytes, size)
                    << "scheme=" << scheme.id << "fuzzy=" << fuzzy
                    << "input=" << input.substr(0, size)
                    << "candidate=" << entries.front().candidate.text;
                const auto presentation = cxxime::derive_composition_presentation(
                    engine.context().composition(), resources.get(), size, true,
                    entries.front().candidate.syllables, true, fuzzy);
                ASSERT_EQ(presentation.display_preedit, displays[size - 8]);
            };
            for (std::size_t index = 0; index < input.size(); ++index) {
                press(static_cast<uint32_t>(input[index] - 'a' + 'A'));
                if (index >= 7) {
                    check_first(index + 1);
                }
            }
            press(VK_BACK);
            check_first(input.size() - 1);
            press('F');
            check_first(input.size());
            const auto first_page = engine.context().page_index();
            const auto first_offset = engine.context().page_offset();
            press(VK_NEXT);
            ASSERT_TRUE(engine.context().page_index() > first_page);
            ASSERT_TRUE(engine.context().page_offset() > first_offset);
            const auto& next_entries = engine.context().translation().entries;
            ASSERT_TRUE(!next_entries.empty());
            for (const auto& entry : next_entries) {
                const auto& action = std::get<cxxime::TextSelectionAction>(entry.selection);
                const auto presentation = cxxime::derive_composition_presentation(
                    engine.context().composition(), resources.get(), action.consumed_input_bytes,
                    true, entry.candidate.syllables, true, fuzzy);
                ASSERT_EQ(presentation.display_preedit, displays.back());
            }
            press(VK_PRIOR);
            ASSERT_EQ(engine.context().page_index(), first_page);
            ASSERT_EQ(engine.context().page_offset(), first_offset);
            check_first(input.size());
            const auto selected = engine.context().translation().entries.front().candidate.text;
            press(VK_SPACE);
            ASSERT_EQ(engine.get_commit_text(), selected);
            ASSERT_TRUE(!engine.context().is_composing());
        }
    }
}
