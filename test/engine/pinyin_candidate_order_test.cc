// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <string>
#include <vector>

#include <cxxime/composition_learning.h>
#include <cxxime/custom_phrases.h>
#include <cxxime/mixed_translator.h>
#include <cxxime/query_trace.h>
#include <cxxime/wubi_input_policy.h>

#include "support/shuangpin_fallback_fixture.h"

namespace {

std::vector<test::FallbackDictionaryRow> ordering_words(int count = 12) {
    std::vector<test::FallbackDictionaryRow> rows;
    for (int index = 0; index < count; ++index) {
        rows.emplace_back("jun:fu:mi", "完整" + std::to_string(1000 + index), 10000 - 100 * index);
    }
    rows.emplace_back("jun:fu", "军服", 500);
    rows.emplace_back("jun", "军", 100);
    return rows;
}

void configure(test::FallbackFixture& fixture, bool full_pinyin, bool fuzzy) {
    if (full_pinyin) {
        fixture.resources =
            cxxime::PinyinResourceSet::create("full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin,
                                              CXXIME_DATA_DIR "pinyin.spellings.bin");
        ASSERT_TRUE(fixture.resources != nullptr);
        fixture.config.pinyin_scheme = "full_pinyin";
    }
    fixture.config.fuzzy_pinyin = fuzzy;
    ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
        fixture.config, fixture.dict, nullptr, fixture.resources)));
    fixture.engine.set_sentence_composition_enabled(false);
    fixture.translator.bind_pinyin(fixture.resources, {fuzzy});
    fixture.translator.set_sentence_composition_enabled(false);
}

void assert_same_entries(const std::vector<cxxime::CandidateEntry>& actual,
                         const std::vector<cxxime::CandidateEntry>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        ASSERT_TRUE(cxxime::same_candidate_entry_identity(actual[index], expected[index]));
    }
}

void navigate(test::FallbackFixture& fixture, uint32_t key, int visible) {
    cxxime::KeyEvent event;
    event.keycode = key;
    fixture.engine.process_key(event, {}, visible);
}

} // namespace

TEST(PinyinCandidateOrder, full_and_all_shuangpin_schemes_keep_paging_and_selection_actions) {
    for (std::size_t mode = 0; mode <= test::fallback_schemes().size(); ++mode) {
        const bool full_pinyin = mode == test::fallback_schemes().size();
        const auto& scheme = test::fallback_schemes()[full_pinyin ? 0 : mode];
        const std::string input = full_pinyin ? "junfumi" : "jfmi";
        for (bool fuzzy : {false, true}) {
            std::vector<cxxime::CandidateEntry> reference;
            for (int page_size : {3, 7, 9}) {
                test::FallbackFixture fixture(ordering_words(), scheme, page_size);
                configure(fixture, full_pinyin, fuzzy);
                fixture.type(input);
                const auto first = fixture.engine.context().translation().entries;
                ASSERT_EQ(first.size(), static_cast<std::size_t>(page_size));
                ASSERT_EQ(first[0].candidate.text, "完整1000");
                ASSERT_EQ(first[1].candidate.text, "完整1001");
                ASSERT_EQ(first[2].candidate.text, "完整1002");
                std::vector<cxxime::CandidateEntry> collected;
                int previous_offset = -1;
                for (int attempt = 0; attempt < 40; ++attempt) {
                    const auto& page = fixture.engine.context().translation();
                    const int visible = (std::min)(3, static_cast<int>(page.entries.size()));
                    if (page.page_offset != previous_offset) {
                        ASSERT_EQ(page.page_offset, static_cast<int>(collected.size()));
                        collected.insert(collected.end(), page.entries.begin(),
                                         page.entries.begin() + visible);
                        previous_offset = page.page_offset;
                    }
                    if (!cxxime::candidate_extent_may_continue(page.extent,
                                                               page.page_offset + visible)) {
                        break;
                    }
                    navigate(fixture, VK_NEXT, visible);
                    if (fixture.engine.context().page_index() == 1) {
                        const auto second = fixture.engine.context().translation().entries;
                        navigate(fixture, VK_PRIOR, 3);
                        assert_same_entries(fixture.engine.context().translation().entries, first);
                        navigate(fixture, VK_NEXT, 3);
                        assert_same_entries(fixture.engine.context().translation().entries, second);
                    }
                }
                ASSERT_EQ(collected.size(), 14u);
                if (reference.empty()) {
                    reference = collected;
                } else {
                    assert_same_entries(collected, reference);
                }
                fixture.press(VK_ESCAPE);
                fixture.type(input);
                for (int page = 0; page < 4; ++page) {
                    navigate(fixture, VK_NEXT, 3);
                }
                ASSERT_EQ(fixture.engine.context().candidate(0)->text, "军服");
                ASSERT_TRUE(fixture.engine.select_candidate(0));
                ASSERT_EQ(fixture.engine.context().active_input(), "mi");
                ASSERT_TRUE(fixture.engine.context().committed_text.empty());
                ASSERT_EQ(fixture.engine.context().composition().converted_segments().front().text,
                          "军服");
            }
        }
    }
}

TEST(PinyinCandidateOrder,
     ordinary_disabled_and_deleted_actions_cannot_commit_from_retained_pages) {
    for (bool user_word : {false, true}) {
        for (uint32_t key : {static_cast<uint32_t>(VK_SPACE), static_cast<uint32_t>('1'),
                             static_cast<uint32_t>(VK_OEM_COMMA), 0u}) {
            test::FallbackFixture fixture(ordering_words());
            configure(fixture, true, false);
            if (user_word) {
                ASSERT_TRUE(fixture.dict->add_user_entry("用户完整", "junfumi", "jun:fu:mi"));
            }
            fixture.type("junfumi");
            const std::string text = fixture.engine.context().candidate(0)->text;
            if (user_word) {
                ASSERT_EQ(text, "用户完整");
                ASSERT_TRUE(fixture.dict->delete_user_entries({{text, "junfumi"}}));
            } else {
                ASSERT_TRUE(fixture.dict->disable_system_entry(text));
            }
            if (key == 0) {
                ASSERT_TRUE(!fixture.engine.select_candidate(0));
            } else {
                fixture.press(key);
            }
            ASSERT_TRUE(fixture.engine.context().committed_text.empty());
            ASSERT_EQ(fixture.engine.context().active_input(), "junfumi");
            for (const auto& entry : fixture.engine.context().translation().entries) {
                ASSERT_NE(entry.candidate.text, text);
            }
        }
    }
}

TEST(PinyinCandidateOrder, small_fallback_windows_preserve_every_choice_and_read_is_idempotent) {
    test::FallbackFixture fixture(ordering_words());
    configure(fixture, false, false);
    cxxime::QueryBudget budget;
    budget.max_prefix_scan = 1;
    cxxime::TranslationRequest request;
    request.input = "jfmi";
    request.policy.allow_partial_selection = true;
    request.budget = &budget;
    std::vector<cxxime::CandidateEntry> published;
    request.published = &published;
    bool exhausted = false;
    std::vector<cxxime::CandidateEntry> pending;
    for (int attempt = 0; attempt < 150; ++attempt) {
        cxxime::QueryTrace trace;
        request.trace = &trace;
        const auto page = fixture.translator.translate(request);
        if (!pending.empty()) {
            assert_same_entries(page.entries, pending);
        }
        ASSERT_TRUE(trace.span_entry_scan_count <= budget.max_prefix_scan);
        ASSERT_TRUE(trace.span_query_count <= 128);
        request.work = cxxime::QueryWork::kRead;
        assert_same_entries(fixture.translator.translate(request).entries, page.entries);
        request.work = cxxime::QueryWork::kAdvance;
        // A narrow viewport acknowledges one row while the rest of the bounded
        // window remains pending. Later reads must preserve its order.
        if (!page.entries.empty()) {
            const auto& entry = page.entries.front();
            ASSERT_TRUE(std::none_of(published.begin(), published.end(), [&](const auto& previous) {
                return previous.candidate.text == entry.candidate.text;
            }));
            published.push_back(entry);
            pending.assign(page.entries.begin() + 1, page.entries.end());
        }
        if (page.entries.empty() && page.extent.complete) {
            exhausted = true;
            break;
        }
    }
    ASSERT_TRUE(exhausted);
    ASSERT_EQ(published.size(), 14u);
}

TEST(PinyinCandidateOrder, disabling_partials_invalidates_ordinary_actions_and_forward_history) {
    for (uint32_t key : {static_cast<uint32_t>('2'), static_cast<uint32_t>(VK_PRIOR),
                         static_cast<uint32_t>(VK_NEXT), 0u}) {
        test::FallbackFixture fixture(ordering_words(4), test::fallback_schemes().back(), 3);
        configure(fixture, true, false);
        fixture.type("junfumi");
        navigate(fixture, VK_NEXT, 3);
        navigate(fixture, VK_PRIOR, 3);
        navigate(fixture, VK_NEXT, 3);
        ASSERT_EQ(fixture.engine.context().candidate(1)->text, "军服");
        fixture.engine.set_partial_selection_enabled(false);
        if (key == 0) {
            ASSERT_TRUE(!fixture.engine.select_candidate(1));
        } else {
            navigate(fixture, key, 3);
        }
        ASSERT_TRUE(fixture.engine.context().committed_text.empty());
        ASSERT_TRUE(fixture.engine.context().composition().converted_segments().empty());
        ASSERT_EQ(fixture.engine.context().page_offset(), 0);
        for (const auto& entry : fixture.engine.context().translation().entries) {
            ASSERT_EQ(std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes,
                      7u);
        }
    }
}

TEST(PinyinCandidateOrder, custom_changes_keep_valid_rankings_but_remove_obsolete_actions) {
    for (bool full_pinyin : {false, true}) {
        test::FallbackFixture fixture(ordering_words(), test::fallback_schemes().back(), 3);
        configure(fixture, full_pinyin, false);
        const std::string input = full_pinyin ? "junfumi" : "jfmi";
        auto phrases = std::make_shared<cxxime::CustomPhrases>();
        const std::string path = fixture.path + ".phrases";
        ASSERT_TRUE(phrases->load(path));
        ASSERT_TRUE(phrases->add(input, "自定义甲"));
        ASSERT_TRUE(phrases->add(input, "自定义乙"));
        ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
            fixture.config, fixture.dict, nullptr, fixture.resources, nullptr, nullptr, phrases)));
        fixture.engine.set_sentence_composition_enabled(false);
        fixture.type(input);
        const auto first = fixture.engine.context().translation().entries;
        navigate(fixture, VK_NEXT, 3);
        navigate(fixture, VK_PRIOR, 3);
        ASSERT_TRUE(phrases->move({"自定义乙", input}, true));
        if (full_pinyin) {
            navigate(fixture, VK_NEXT, 3);
            navigate(fixture, VK_PRIOR, 3);
            assert_same_entries(fixture.engine.context().translation().entries, first);
        }
        ASSERT_TRUE(phrases->erase({{"自定义甲", input}, {"自定义乙", input}}));
        ASSERT_TRUE(!fixture.engine.select_candidate(0));
        ASSERT_TRUE(fixture.engine.context().committed_text.empty());
        ASSERT_EQ(fixture.engine.context().page_offset(), 0);
        ASSERT_EQ(fixture.engine.context().candidate(0)->text, "完整1000");
        ASSERT_TRUE(DeleteFileA(path.c_str()));
    }
}

TEST(PinyinCandidateOrder, deleting_unresolved_learning_invalidates_a_frozen_ordinary_choice) {
    for (const std::string input : {"junfumi", "jfm"}) {
        test::FallbackFixture fixture({{"ni", "system", 100}});
        configure(fixture, true, false);
        ASSERT_TRUE(fixture.dict->load_candidate_preferences(fixture.path + ".learning"));
        cxxime::Candidate learned;
        learned.text = "历史完整";
        learned.code = "junfumi";
        learned.syllables = "jun:fu:mi";
        learned.source = cxxime::CandidateSource::kPinyin;
        learned.origin = cxxime::CandidateOrigin::kLearned;
        ASSERT_TRUE(fixture.dict->record_candidate_preference(learned, input));
        fixture.config.candidate_learning = true;
        ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
            fixture.config, fixture.dict, nullptr, fixture.resources)));
        fixture.engine.set_sentence_composition_enabled(false);
        fixture.engine.set_trace_enabled(true);
        fixture.type(input);
        ASSERT_EQ(fixture.engine.context().candidate(0)->text, learned.text);
        ASSERT_TRUE(fixture.dict->record_candidate_preference(learned, input));
        navigate(fixture, VK_NEXT, 3);
        ASSERT_NE(fixture.engine.last_trace().navigation_outcome,
                  cxxime::CandidateNavigationOutcome::kSequenceReset);
        ASSERT_EQ(fixture.engine.context().candidate(0)->text, learned.text);
        ASSERT_TRUE(fixture.dict->clear_candidate_preferences());
        ASSERT_TRUE(!fixture.engine.select_candidate(0));
        ASSERT_TRUE(fixture.engine.context().committed_text.empty());
        ASSERT_EQ(fixture.engine.context().active_input(), input);
        for (const auto& entry : fixture.engine.context().translation().entries) {
            ASSERT_NE(entry.candidate.text, learned.text);
        }
    }
}

TEST(PinyinCandidateOrder, mixed_keeps_complete_words_together_before_partial_choices) {
    test::FallbackFixture fixture(ordering_words());
    std::vector<test::FallbackDictionaryRow> rows;
    for (int index = 0; index < 12; ++index) {
        rows.emplace_back("jfmi", "五笔" + std::to_string(1000 + index), 20000 - 100 * index);
    }
    const std::string path = fixture.path + ".wubi";
    ASSERT_TRUE(cxxime::Dict::create_test_dict(path, rows));
    cxxime::Dict wubi{cxxime::UserDictKind::WUBI};
    ASSERT_TRUE(wubi.open_dict(path));
    cxxime::MixedTranslator mixed;
    mixed.set_pinyin_dict(fixture.dict.get());
    mixed.set_wubi_dict(&wubi);
    mixed.bind_pinyin(fixture.resources, {false});
    mixed.set_sentence_composition_enabled(false);
    mixed.set_candidate_preference(cxxime::MixedCandidatePreference::kWubi);
    cxxime::TranslationRequest request;
    request.input = "jfmi";
    request.scheme = cxxime::CompositionScheme::kMixed;
    request.policy.allow_partial_selection = true;
    const auto result = mixed.translate(request);
    ASSERT_EQ(result.entries.size(), 26u);
    std::vector<std::string> full;
    for (const auto& entry : result.entries) {
        if (std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes == 4) {
            full.push_back(entry.candidate.text);
        }
    }
    ASSERT_EQ(full.size(), 24u);
    for (int index = 0; index < 12; ++index) {
        ASSERT_EQ(full[2 * index], "五笔" + std::to_string(1000 + index));
        ASSERT_EQ(full[2 * index + 1], "完整" + std::to_string(1000 + index));
    }
    ASSERT_EQ(result.entries[24].candidate.text, "军服");
    ASSERT_EQ(result.entries[25].candidate.text, "军");
    wubi.close();
    ASSERT_TRUE(DeleteFileA(path.c_str()));
}

TEST(PinyinCandidateOrder, forced_completion_and_mode_switch_do_not_commit_deleted_choices) {
    for (bool full_pinyin : {false, true}) {
        for (bool mode_switch : {false, true}) {
            test::FallbackFixture fixture(ordering_words());
            configure(fixture, full_pinyin, false);
            fixture.config.ascii_switch_key["Shift"] = "candidate";
            ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
                fixture.config, fixture.dict, nullptr, fixture.resources)));
            const std::string input = full_pinyin ? "junfumi" : "jfmi";
            fixture.type(input);
            ASSERT_TRUE(
                fixture.dict->disable_system_entry(fixture.engine.context().candidate(0)->text));
            if (mode_switch) {
                cxxime::KeyEvent event;
                event.keycode = VK_SHIFT;
                event.set_shift();
                fixture.engine.process_key(event);
                event.modifiers = 0;
                event.is_key_up = true;
                ASSERT_EQ(fixture.engine.process_key(event), cxxime::ProcessResult::COMMITTED);
                ASSERT_EQ(fixture.engine.get_commit_text(), input);
            } else {
                ASSERT_EQ(fixture.engine.commit_composition_with_source().first, input);
            }
        }
    }
}

TEST(PinyinCandidateOrder, ordinary_engine_appends_late_partials_after_a_budget_limited_page) {
    test::FallbackFixture fixture(ordering_words(), test::fallback_schemes().back(), 3);
    configure(fixture, true, false);
    fixture.type("junfumi");
    cxxime::QueryBudget budget;
    budget.max_exact_scan = 0;
    cxxime::TranslationRequest request;
    request.input = "junfumi";
    request.page_size = 3;
    request.policy.allow_partial_selection = true;
    request.budget = &budget;
    auto limited = fixture.translator.translate(request);
    ASSERT_EQ(limited.entries.size(), 3u);
    ASSERT_TRUE(!limited.extent.complete);
    for (const auto& entry : limited.entries) {
        ASSERT_EQ(std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes, 7u);
    }
    // Supply the real bounded translator response at Engine's publication boundary.
    // No wall-clock sleeps or production budget overrides are needed to force late P.
    limited.sequence_version = fixture.engine.context().translation().sequence_version;
    limited.validation_version = fixture.engine.context().translation().validation_version;
    const auto first = limited.entries;
    fixture.engine.context().update_translation(std::move(limited));
    navigate(fixture, VK_NEXT, 3);
    ASSERT_EQ(fixture.engine.context().page_offset(), 3);
    const auto second = fixture.engine.context().translation().entries;
    ASSERT_EQ(second[0].candidate.text, "完整1003");
    ASSERT_EQ(second[1].candidate.text, "完整1004");
    ASSERT_EQ(second[2].candidate.text, "完整1005");
    navigate(fixture, VK_PRIOR, 3);
    assert_same_entries(fixture.engine.context().translation().entries, first);
    navigate(fixture, VK_NEXT, 3);
    assert_same_entries(fixture.engine.context().translation().entries, second);
    for (int page = 0; page < 3; ++page) {
        navigate(fixture, VK_NEXT, 3);
    }
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "军服");
    ASSERT_EQ(fixture.engine.context().candidate(1)->text, "军");
}

TEST(PinyinCandidateOrder, stale_choices_do_not_consume_literal_numpad_input_in_full_shape_mode) {
    test::FallbackFixture fixture(ordering_words());
    configure(fixture, true, false);
    fixture.type("junfumi");
    ASSERT_TRUE(fixture.dict->disable_system_entry(fixture.engine.context().candidate(0)->text));
    cxxime::KeyEvent event;
    event.keycode = VK_NUMPAD1;
    cxxime::OutputOptions options;
    options.full_shape = true;
    ASSERT_EQ(fixture.engine.process_key(event, options), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().active_input(), "junfumi1");
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
}

TEST(PinyinCandidateOrder, one_sentence_then_words_survives_paging_and_segment_selection) {
    const std::vector<test::FallbackDictionaryRow> rows = {
        {"jun:fu", "均富", 5000}, {"jun", "军", 100}, {"fu", "服", 100}, {"ji", "机", 50},
        {"fa", "发", 50},         {"mi", "迷", 100},  {"ma", "吗", 100},
    };
    for (std::size_t mode = 0; mode <= test::fallback_schemes().size(); ++mode) {
        const bool full_pinyin = mode == test::fallback_schemes().size();
        const auto& scheme = test::fallback_schemes()[full_pinyin ? 0 : mode];
        const std::string input = full_pinyin ? "junfumima" : "jfmima";
        for (bool fuzzy : {false, true}) {
            for (bool partial : {false, true}) {
                test::FallbackFixture fixture(rows, scheme, 3);
                configure(fixture, full_pinyin, fuzzy);
                fixture.engine.set_sentence_composition_enabled(true);
                fixture.engine.set_partial_selection_enabled(partial);
                fixture.type(input);
                const auto first = fixture.engine.context().translation().entries;
                ASSERT_TRUE(!first.empty());
                ASSERT_EQ(first[0].candidate.text, "均富迷吗");
                ASSERT_EQ(first[0].candidate.origin, cxxime::CandidateOrigin::kComposed);
                if (partial) {
                    ASSERT_EQ(first[1].candidate.text, "均富");
                }
                int sentences = 0;
                int offset = -1;
                bool exhausted = false;
                for (int page = 0; page < 30; ++page) {
                    const auto& current = fixture.engine.context().translation();
                    const int visible = (std::min)(2, static_cast<int>(current.entries.size()));
                    if (offset != current.page_offset) {
                        offset = current.page_offset;
                        for (int index = 0; index < visible; ++index) {
                            const auto& entry = current.entries[index];
                            if (entry.candidate.origin == cxxime::CandidateOrigin::kComposed) {
                                ++sentences;
                            } else {
                                ASSERT_TRUE(partial);
                                ASSERT_TRUE(std::get<cxxime::TextSelectionAction>(entry.selection)
                                                .consumed_input_bytes < input.size());
                            }
                        }
                    }
                    if (!cxxime::candidate_extent_may_continue(current.extent, offset + visible)) {
                        exhausted = true;
                        break;
                    }
                    navigate(fixture, VK_NEXT, visible);
                    if (fixture.engine.context().page_index() == 1) {
                        const auto second = fixture.engine.context().translation().entries;
                        navigate(fixture, VK_PRIOR, 2);
                        assert_same_entries(fixture.engine.context().translation().entries, first);
                        navigate(fixture, VK_NEXT, 2);
                        assert_same_entries(fixture.engine.context().translation().entries, second);
                    }
                }
                ASSERT_TRUE(exhausted);
                ASSERT_EQ(sentences, 1);
                fixture.press(VK_ESCAPE);
                fixture.type(input);
                assert_same_entries(fixture.engine.context().translation().entries, first);
                if (partial) {
                    ASSERT_TRUE(fixture.engine.select_candidate(1));
                    ASSERT_EQ(fixture.engine.context().active_input(), "mima");
                    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "迷吗");
                    fixture.press(VK_SPACE);
                    ASSERT_EQ(fixture.engine.get_commit_text(), "均富迷吗");
                }
            }
        }
    }
}

TEST(PinyinCandidateOrder, persistent_sentences_are_not_limited_as_generated_suggestions) {
    for (bool full_pinyin : {false, true}) {
        test::FallbackFixture fixture({{"jun", "军", 100}, {"fu", "服", 100}, {"mi", "迷", 100}},
                                      test::fallback_schemes().back(), 3);
        configure(fixture, full_pinyin, false);
        const std::string input = full_pinyin ? "junfumi" : "jfmi";
        const std::string path = fixture.path + ".sentences";
        auto learning = std::make_shared<cxxime::CompositionLearningService>();
        ASSERT_TRUE(learning->load(path));
        ASSERT_TRUE(learning->start());
        for (int index = 0; index < 8; ++index) {
            ASSERT_TRUE(
                learning->enqueue({"学习" + std::to_string(index), "junfumi", "jun:fu:mi"}));
        }
        // The persisted text also shadows an automatically generated sentence.
        ASSERT_TRUE(learning->enqueue({"军服迷", "junfumi", "jun:fu:mi"}));
        ASSERT_TRUE(learning->flush());
        fixture.config.candidate_learning = true;
        ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
            fixture.config, fixture.dict, nullptr, fixture.resources, nullptr, learning)));
        fixture.engine.set_sentence_composition_enabled(true);
        fixture.type(input);
        int learned = 0;
        int offset = -1;
        bool exhausted = false;
        for (int page = 0; page < 20; ++page) {
            const auto& current = fixture.engine.context().translation();
            const int visible = static_cast<int>(current.entries.size());
            if (offset != current.page_offset) {
                offset = current.page_offset;
                for (const auto& entry : current.entries) {
                    ASSERT_NE(entry.candidate.origin, cxxime::CandidateOrigin::kComposed);
                    if (entry.candidate.origin == cxxime::CandidateOrigin::kLearnedComposition) {
                        ++learned;
                    }
                }
            }
            if (!cxxime::candidate_extent_may_continue(current.extent, offset + visible)) {
                exhausted = true;
                break;
            }
            navigate(fixture, VK_NEXT, visible);
        }
        ASSERT_TRUE(exhausted);
        ASSERT_EQ(learned, 9);
        fixture.press(VK_ESCAPE);
        fixture.type(input);
        const auto before = fixture.engine.context().translation().entries;
        ASSERT_TRUE(learning->enqueue({"学习0", "junfumi", "jun:fu:mi"}));
        ASSERT_TRUE(learning->flush());
        if (full_pinyin) {
            ASSERT_TRUE(fixture.engine.select_candidate(0));
            ASSERT_EQ(fixture.engine.get_commit_text(), before[0].candidate.text);
        } else {
            // Resumable queries explicitly reset on source-version changes.
            ASSERT_TRUE(!fixture.engine.select_candidate(0));
            ASSERT_TRUE(fixture.engine.context().committed_text.empty());
            const auto current = fixture.engine.context().candidate(0)->text;
            ASSERT_TRUE(fixture.engine.select_candidate(0));
            ASSERT_EQ(fixture.engine.get_commit_text(), current);
        }
        ASSERT_TRUE(learning->freeze_and_stop());
        ASSERT_TRUE(DeleteFileA(path.c_str()));
    }
}

TEST(PinyinCandidateOrder, mixed_fifth_key_never_commits_a_removed_frozen_choice) {
    for (bool deferred : {false, true}) {
        for (bool user_word : {false, true}) {
            test::FallbackFixture fixture({{"ni", "你", 100}});
            configure(fixture, true, false);
            const std::string path = fixture.path + ".wubi";
            const std::string phrase_path = fixture.path + ".phrases";
            std::vector<test::FallbackDictionaryRow> rows;
            if (!user_word) {
                rows.emplace_back("vvvv", "首选", 1000);
            }
            if (!deferred) {
                rows.emplace_back("vvvv", "次选", 100);
            }
            ASSERT_TRUE(cxxime::Dict::create_test_dict(path, rows));
            auto wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
            ASSERT_TRUE(wubi->open_dict(path));
            if (user_word) {
                ASSERT_TRUE(wubi->add_user_entry("首选", "vvvv"));
            }
            auto phrases = std::make_shared<cxxime::CustomPhrases>();
            ASSERT_TRUE(phrases->load(phrase_path));
            if (deferred) {
                ASSERT_TRUE(phrases->add("vvvvx", "长编码"));
            }
            fixture.config.wubi_auto_commit = true;
            fixture.config.wubi_commit_first_on_fifth_key = true;
            ASSERT_TRUE(fixture.engine.apply_runtime_state(cxxime::EngineRuntimeState::create(
                fixture.config, fixture.dict, wubi, fixture.resources, nullptr, nullptr, phrases)));
            fixture.engine.switch_mode(cxxime::InputMode::MIXED);
            fixture.type("vvvv");
            ASSERT_EQ(fixture.engine.context().active_input(), "vvvv");
            ASSERT_EQ(fixture.engine.context().candidate(0)->text, "首选");
            ASSERT_EQ(cxxime::WubiInputPolicy::should_auto_commit(
                          cxxime::CompositionScheme::kMixed, "vvvv",
                          fixture.engine.context().translation(), fixture.config),
                      deferred);
            if (user_word) {
                ASSERT_TRUE(wubi->delete_user_entries({{"首选", "vvvv"}}));
            } else {
                ASSERT_TRUE(wubi->disable_system_entry("首选"));
            }
            fixture.press('A');
            ASSERT_TRUE(fixture.engine.context().committed_text.empty());
            ASSERT_EQ(fixture.engine.context().active_input(), "vvvv");
            for (const auto& entry : fixture.engine.context().translation().entries) {
                ASSERT_NE(entry.candidate.text, "首选");
            }
            fixture.engine.finalize();
            wubi->close();
            ASSERT_TRUE(DeleteFileA(path.c_str()));
            if (deferred) {
                ASSERT_TRUE(DeleteFileA(phrase_path.c_str()));
            }
        }
    }
}
