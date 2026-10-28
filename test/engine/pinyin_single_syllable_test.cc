// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <windows.h>

#include <cxxime/composition_presentation.h>
#include <cxxime/engine.h>
#include <cxxime/mixed_translator.h>

#include "support/test_runtime.h"
#include "support/testutil.h"

namespace {

std::string temporary_file() {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    ASSERT_NE(GetTempPathA(MAX_PATH, directory), 0u);
    ASSERT_NE(GetTempFileNameA(directory, "syl", 0, path), 0u);
    return path;
}

cxxime::KeyEvent key(uint32_t code) {
    cxxime::KeyEvent event;
    event.keycode = code;
    return event;
}

struct SyllableFixture {
    std::string dict_path = temporary_file();
    std::string spellings_path = temporary_file();
    std::shared_ptr<cxxime::Dict> dict =
        std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    std::shared_ptr<const cxxime::PinyinResourceSet> resources;
    cxxime::PinyinTranslator translator;

    SyllableFixture() {
        std::vector<std::tuple<std::string, std::string, int>> entries = {
            {"ya", "呀", 50000},
            {"o", "哦", 50000},
            {"ya:o", "呀哦", 1},
            {"xian", "先", 1000},
            {"xi:an", "西安", 900},
            {"xi", "西", 800},
            {"an", "安", 700},
            {"zi", "字", 600},
            {"yao:yi:piao", "舀一瓢", 50000},
            {"yi:piao", "一瓢", 5000},
            {"xi", "熙", 1},
            {"xian", "鲜", 500},
            {"xi", "鲜", 400},
            {"xian:ren", "仙人", 50000},
            {"xi:an:shi", "西安市", 40000},
        };
        int frequency = 10000;
        for (const char* text :
             {"要", "摇", "药", "妖", "腰", "咬", "遥", "耀", "瑶", "姚", "邀", "钥",
              "夭", "谣", "幺", "窑", "尧", "吆", "肴", "约", "曜", "窈", "杳", "舀"}) {
            entries.emplace_back("yao", text, frequency--);
        }
        ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, entries));
        ASSERT_TRUE(dict->open_dict(dict_path));
        ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
            spellings_path, {
                                {"yao", "yao", cxxime::kNormalSpelling, 0.0f},
                                {"ya", "ya", cxxime::kNormalSpelling, 0.0f},
                                {"o", "o", cxxime::kNormalSpelling, 0.0f},
                                {"xian", "xian", cxxime::kNormalSpelling, 0.0f},
                                {"xi", "xi", cxxime::kNormalSpelling, 0.0f},
                                {"an", "an", cxxime::kNormalSpelling, 0.0f},
                                {"yi", "yi", cxxime::kNormalSpelling, 0.0f},
                                {"piao", "piao", cxxime::kNormalSpelling, 0.0f},
                                {"x", "xi", cxxime::kAbbreviation, -1.0f},
                                {"y", "yao", cxxime::kAbbreviation, -1.0f},
                                {"zian", "xian", cxxime::kFuzzySpelling, -0.5f},
                                {"zi", "zi", cxxime::kNormalSpelling, 0.0f},
                            }));
        resources = cxxime::PinyinResourceSet::create(
            "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spellings_path);
        ASSERT_TRUE(resources != nullptr);
        translator.set_dict(dict.get());
        translator.bind_pinyin(resources, {});
    }

    ~SyllableFixture() {
        translator.bind_pinyin(nullptr, {});
        resources.reset();
        dict->close();
        DeleteFileA(dict_path.c_str());
        DeleteFileA(spellings_path.c_str());
    }
};

std::vector<cxxime::CandidateEntry> collect(cxxime::ITranslator& translator,
                                            const std::string& input, int page_size) {
    cxxime::TranslationRequest request;
    request.input = input;
    request.page_size = page_size;
    request.policy.allow_partial_selection = true;
    std::vector<cxxime::CandidateEntry> entries;
    for (int page = 0; page < 100; ++page) {
        request.page_index = page;
        request.page_offset = static_cast<int>(entries.size());
        const auto result = translator.translate(request);
        ASSERT_TRUE(result.usable());
        entries.insert(entries.end(), result.entries.begin(), result.entries.end());
        if (!cxxime::candidate_extent_may_continue(result.extent,
                                                   static_cast<int>(entries.size()))) {
            return entries;
        }
        ASSERT_TRUE(!result.entries.empty());
    }
    ASSERT_TRUE(false) << "candidate pagination did not finish";
    return entries;
}

const cxxime::TextSelectionAction& action_of(const cxxime::CandidateEntry& entry) {
    const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
    ASSERT_TRUE(action != nullptr);
    return *action;
}

const cxxime::CandidateEntry& find_entry(const std::vector<cxxime::CandidateEntry>& entries,
                                         const std::string& text) {
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.candidate.text == text;
    });
    ASSERT_TRUE(found != entries.end()) << text;
    return *found;
}

void type(cxxime::Engine& engine, const std::string& input) {
    for (char ch : input) {
        ASSERT_EQ(engine.process_key(key(static_cast<uint32_t>(ch - 'a' + 'A'))),
                  cxxime::ProcessResult::ACCEPTED);
    }
}

} // namespace

TEST(PinyinSingleSyllable, full_choices_precede_reachable_partials_for_every_page_size) {
    SyllableFixture fixture;
    for (const std::string input : {"yao", "xian"}) {
        const auto expected = fixture.translator.translate_page(
            input, 0, 128, nullptr, nullptr, nullptr, 0, true, cxxime::PinyinLookupScope::kExact);
        ASSERT_TRUE(expected.extent.complete);
        const auto single_pages = collect(fixture.translator, input, 1);
        for (int page_size : {1, 7, 9}) {
            const auto actual = collect(fixture.translator, input, page_size);
            ASSERT_GT(actual.size(), expected.candidates.size());
            ASSERT_EQ(actual.size(), single_pages.size());
            std::set<std::string> texts;
            bool partial_seen = false;
            bool prediction_seen = false;
            for (std::size_t i = 0; i < actual.size(); ++i) {
                ASSERT_TRUE(cxxime::same_candidate_entry_identity(actual[i], single_pages[i]));
                ASSERT_TRUE(texts.insert(actual[i].candidate.text).second);
                if (i < expected.candidates.size()) {
                    ASSERT_EQ(actual[i].candidate.text, expected.candidates[i].text);
                    ASSERT_EQ(action_of(actual[i]).consumed_input_bytes, input.size());
                    ASSERT_EQ(actual[i].match_kind, cxxime::CandidateMatchKind::kExact);
                } else if (actual[i].match_kind == cxxime::CandidateMatchKind::kPrefix) {
                    ASSERT_TRUE(partial_seen);
                    prediction_seen = true;
                    ASSERT_EQ(action_of(actual[i]).consumed_input_bytes, input.size());
                } else {
                    ASSERT_TRUE(!prediction_seen);
                    partial_seen = true;
                    ASSERT_LT(action_of(actual[i]).consumed_input_bytes, input.size());
                }
            }
            ASSERT_TRUE(partial_seen);
            ASSERT_TRUE(prediction_seen);
        }
    }
}

TEST(PinyinSingleSyllable, ambiguous_full_reading_keeps_its_display_and_commit_action) {
    SyllableFixture fixture;
    const auto entries = collect(fixture.translator, "xian", 7);
    const auto& action = action_of(find_entry(entries, "西安"));
    ASSERT_EQ(action.consumed_input_bytes, 4u);
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input("xian", 4));
    const auto presentation = cxxime::derive_composition_presentation(
        state, fixture.resources.get(), action.consumed_input_bytes, true,
        action.variants[action.primary_variant].syllables);
    ASSERT_EQ(presentation.logical_preedit, "xian");
    ASSERT_EQ(presentation.display_preedit, "xi'an");
    std::string committed;
    ASSERT_TRUE(state.finalize_candidate(action, committed));
    ASSERT_EQ(committed, "西安");
    ASSERT_TRUE(!state.is_composing());
}

TEST(PinyinSingleSyllable, incomplete_and_fuzzy_spellings_still_allow_prefix_selection) {
    SyllableFixture fixture;
    for (const auto& sample : {std::make_pair("xy", "西"), std::make_pair("zian", "字")}) {
        const auto entries = collect(fixture.translator, sample.first, 7);
        const auto& action = action_of(find_entry(entries, sample.second));
        ASSERT_LT(action.consumed_input_bytes, std::string(sample.first).size());
    }
}

TEST(PinyinSingleSyllable, mixed_partials_wait_for_both_full_sources) {
    SyllableFixture fixture;
    const std::string wubi_path = temporary_file();
    std::vector<std::tuple<std::string, std::string, int>> wubi_entries;
    for (const char* input : {"yao", "xian"}) {
        for (int i = 0; i < 40; ++i) {
            wubi_entries.emplace_back(input, "五笔" + std::to_string(i), 1000 - i);
        }
        wubi_entries.emplace_back(input, "鲜", 500);
    }
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_entries));
    cxxime::Dict wubi{cxxime::UserDictKind::WUBI};
    ASSERT_TRUE(wubi.open_dict(wubi_path));
    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(fixture.dict.get());
    translator.set_wubi_dict(&wubi);
    translator.bind_pinyin(fixture.resources, {});
    for (const std::string input : {"yao", "xian"}) {
        const auto single_pages = collect(translator, input, 1);
        for (int page_size : {7, 9}) {
            const auto entries = collect(translator, input, page_size);
            ASSERT_EQ(entries.size(), single_pages.size());
            bool partial_seen = false;
            bool prediction_seen = false;
            int wubi_count = 0;
            std::set<std::string> texts;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                ASSERT_TRUE(cxxime::same_candidate_entry_identity(entry, single_pages[i]));
                ASSERT_TRUE(texts.insert(entry.candidate.text).second);
                if (action_of(entry).consumed_input_bytes < input.size()) {
                    ASSERT_TRUE(!prediction_seen);
                    partial_seen = true;
                } else if (entry.match_kind == cxxime::CandidateMatchKind::kPrefix) {
                    ASSERT_TRUE(partial_seen);
                    prediction_seen = true;
                } else {
                    ASSERT_TRUE(!partial_seen);
                }
                if (entry.candidate.source == cxxime::CandidateSource::kWubi) {
                    ++wubi_count;
                }
            }
            ASSERT_EQ(wubi_count, 41);
            ASSERT_TRUE(partial_seen);
            ASSERT_TRUE(prediction_seen);
            ASSERT_EQ(action_of(find_entry(entries, "鲜")).consumed_input_bytes, input.size());
        }
    }
    wubi.close();
    DeleteFileA(wubi_path.c_str());
}

TEST(PinyinSingleSyllable, incomplete_full_query_never_publishes_the_partial_tail) {
    SyllableFixture fixture;
    cxxime::QueryBudget budget;
    budget.max_exact_scan = 1;
    cxxime::TranslationRequest request;
    request.input = "yao";
    request.page_size = 64;
    request.policy.allow_partial_selection = true;
    request.budget = &budget;
    const auto incomplete = fixture.translator.translate(request);
    ASSERT_TRUE(!incomplete.extent.complete);
    ASSERT_TRUE(cxxime::candidate_extent_may_continue(incomplete.extent,
                                                      static_cast<int>(incomplete.entries.size())));
    for (const auto& entry : incomplete.entries) {
        ASSERT_EQ(action_of(entry).consumed_input_bytes, 3u);
    }

    // A later query with sufficient budget can reach the fallback instead of treating
    // the incomplete prefix as the end of the full-span group.
    request.budget = nullptr;
    const auto complete = fixture.translator.translate(request);
    ASSERT_EQ(action_of(find_entry(complete.entries, "呀")).consumed_input_bytes, 2u);
    ASSERT_EQ(action_of(find_entry(complete.entries, "舀")).consumed_input_bytes, 3u);
}

TEST(PinyinSingleSyllable, truncated_dictionary_does_not_publish_a_temporary_composed_tail) {
    SyllableFixture fixture;
    fixture.dict->close();
    ASSERT_TRUE(cxxime::Dict::create_test_dict(fixture.dict_path, {{"xian", "先", 1000},
                                                                   {"xian", "仙", 900},
                                                                   {"xian", "鲜", 800},
                                                                   {"xi", "西", 700},
                                                                   {"an", "安", 600}}));
    ASSERT_TRUE(fixture.dict->open_dict(fixture.dict_path));
    cxxime::QueryBudget budget;
    budget.max_results_before_merge = 1;
    cxxime::TranslationRequest request;
    request.input = "xian";
    request.page_size = 64;
    request.policy.allow_partial_selection = true;
    request.budget = &budget;
    const auto incomplete = fixture.translator.translate(request);
    ASSERT_TRUE(!incomplete.extent.complete);
    ASSERT_TRUE(!incomplete.entries.empty());
    for (const auto& entry : incomplete.entries) {
        ASSERT_TRUE(entry.candidate.text == "先" || entry.candidate.text == "仙" ||
                    entry.candidate.text == "鲜");
        ASSERT_EQ(action_of(entry).consumed_input_bytes, 4u);
    }
    request.budget = nullptr;
    const auto complete = fixture.translator.translate(request);
    ASSERT_TRUE(complete.extent.complete);
    ASSERT_EQ(action_of(find_entry(complete.entries, "西安")).consumed_input_bytes, 4u);
    ASSERT_EQ(action_of(find_entry(complete.entries, "西")).consumed_input_bytes, 2u);
}

TEST(PinyinSingleSyllable, incomplete_partials_do_not_publish_later_predictions) {
    SyllableFixture fixture;
    fixture.translator.set_sentence_composition_enabled(false);
    cxxime::QueryBudget budget;
    budget.max_exact_scan = 2;
    cxxime::TranslationRequest request;
    request.input = "xian";
    request.page_size = 64;
    request.policy.allow_partial_selection = true;
    request.budget = &budget;
    const auto incomplete = fixture.translator.translate(request);
    ASSERT_TRUE(!incomplete.extent.complete);
    ASSERT_EQ(action_of(find_entry(incomplete.entries, "西安")).consumed_input_bytes, 4u);
    for (const auto& entry : incomplete.entries) {
        ASSERT_TRUE(entry.match_kind != cxxime::CandidateMatchKind::kPrefix);
    }
    request.budget = nullptr;
    const auto complete = fixture.translator.translate(request);
    ASSERT_TRUE(complete.extent.complete);
    ASSERT_EQ(action_of(find_entry(complete.entries, "熙")).consumed_input_bytes, 2u);
    ASSERT_EQ(find_entry(complete.entries, "仙人").match_kind, cxxime::CandidateMatchKind::kPrefix);
}

TEST(PinyinSingleSyllable, scoped_queries_do_not_replace_the_unscoped_cache) {
    SyllableFixture fixture;
    fixture.translator.set_sentence_composition_enabled(false);
    for (auto scope : {cxxime::PinyinLookupScope::kExact, cxxime::PinyinLookupScope::kPrefix}) {
        const auto scoped = fixture.translator.translate_page("xian", 0, 128, nullptr, nullptr,
                                                              nullptr, 0, false, scope);
        ASSERT_TRUE(scoped.extent.complete);
        const auto all = fixture.translator.translate_page("xian", 0, 128);
        for (const char* text : {"先", "西安", "仙人", "西安市"}) {
            ASSERT_TRUE(std::any_of(all.candidates.begin(), all.candidates.end(),
                                    [&](const auto& candidate) { return candidate.text == text; }));
        }
        fixture.translator.set_dict(fixture.dict.get());
    }
}

TEST(PinyinSingleSyllable, unlisted_word_can_be_selected_in_parts_after_the_full_group) {
    SyllableFixture fixture;
    const auto full = fixture.translator.translate_page("xian", 0, 128);
    ASSERT_TRUE(std::none_of(full.candidates.begin(), full.candidates.end(),
                             [](const auto& entry) { return entry.text == "熙安"; }));
    cxxime::Config config;
    config.page_size = 1;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(
        cxxime::EngineRuntimeState::create(config, fixture.dict, nullptr, fixture.resources)));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    type(engine, "xian");
    std::vector<cxxime::CandidateEntry> published;
    for (int page = 0; page < 30; ++page) {
        ASSERT_EQ(engine.context().candidate_count(), 1);
        published.push_back(*engine.context().candidate_entry(0));
        if (published.back().candidate.text == "熙") {
            break;
        }
        ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(engine.context().page_index(), page + 1);
    }
    ASSERT_EQ(published.back().candidate.text, "熙");
    ASSERT_EQ(action_of(find_entry(published, "鲜")).consumed_input_bytes, 4u);
    const auto& action = action_of(published.back());
    ASSERT_EQ(action.consumed_input_bytes, 2u);
    const auto presentation = cxxime::derive_composition_presentation(
        engine.context().composition(), fixture.resources.get(), 2, true,
        action.variants[action.primary_variant].syllables);
    ASSERT_EQ(presentation.logical_preedit, "xian");
    ASSERT_EQ(presentation.display_preedit, "xi'an");
    ASSERT_EQ(presentation.focused_preedit_end_bytes, 2u);

    // Visiting the tail must not change earlier full-span identities, including
    // the text that also exists as a partial candidate.
    for (int i = static_cast<int>(published.size()) - 2; i >= 0; --i) {
        ASSERT_EQ(engine.process_key(key(VK_PRIOR)), cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(cxxime::same_candidate_entry_identity(*engine.context().candidate_entry(0),
                                                          published[i]));
    }
    for (std::size_t i = 1; i < published.size(); ++i) {
        ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(cxxime::same_candidate_entry_identity(*engine.context().candidate_entry(0),
                                                          published[i]));
    }
    ASSERT_TRUE(engine.select_candidate(0));
    ASSERT_EQ(engine.context().active_input(), "an");
    ASSERT_EQ(engine.context().committed_text, "");
    ASSERT_EQ(engine.context().candidate(0)->text, "安");
    ASSERT_TRUE(engine.select_candidate(0));
    ASSERT_EQ(engine.get_commit_text(), "熙安");
    engine.finalize();
}

TEST(PinyinSingleSyllable, typing_more_and_backspacing_change_selection_scope) {
    SyllableFixture fixture;
    cxxime::Config config;
    config.page_size = 7;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(
        cxxime::EngineRuntimeState::create(config, fixture.dict, nullptr, fixture.resources)));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    type(engine, "yaoyipiao");
    bool selected = false;
    for (int page = 0; page < 10 && !selected; ++page) {
        const auto entries = engine.context().translation().entries;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].candidate.text == "舀") {
                ASSERT_EQ(action_of(entries[i]).consumed_input_bytes, 3u);
                ASSERT_TRUE(engine.select_candidate(static_cast<int>(i)));
                selected = true;
                break;
            }
        }
        if (!selected) {
            ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
        }
    }
    ASSERT_TRUE(selected);
    ASSERT_EQ(engine.context().active_input(), "yipiao");
    ASSERT_EQ(engine.context().committed_text, "");
    const auto& suffix = engine.context().translation().entries;
    const auto& final_entry = find_entry(suffix, "一瓢");
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(&final_entry - suffix.data())));
    ASSERT_EQ(engine.get_commit_text(), "舀一瓢");

    type(engine, "yaoyi");
    ASSERT_EQ(engine.process_key(key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().active_input(), "yao");
    for (int page = 0; page < 4; ++page) {
        for (const auto& entry : engine.context().translation().entries) {
            if (entry.candidate.syllables == "yao") {
                ASSERT_EQ(action_of(entry).consumed_input_bytes, 3u);
            }
        }
        if (page < 3) {
            ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
            ASSERT_EQ(engine.context().page_index(), page + 1);
        }
    }
    ASSERT_EQ(
        action_of(find_entry(engine.context().translation().entries, "舀")).consumed_input_bytes,
        3u);
    engine.finalize();
}

TEST(PinyinSingleSyllable, moved_anchor_is_retried_until_its_new_position_is_queried) {
    SyllableFixture fixture;
    cxxime::Config config;
    config.page_size = 1;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(
        cxxime::EngineRuntimeState::create(config, fixture.dict, nullptr, fixture.resources)));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    type(engine, "yao");
    const auto published = *engine.context().candidate_entry(0);
    const auto full = fixture.translator.translate_page("yao", 0, 128, nullptr, nullptr, nullptr, 0,
                                                        true, cxxime::PinyinLookupScope::kExact);
    std::vector<cxxime::ManualCandidateOrderEntry> order;
    for (const auto& candidate : full.candidates) {
        if (candidate.text != published.candidate.text &&
            order.size() < cxxime::MANUAL_CANDIDATE_ORDER_MAX_ENTRIES) {
            order.push_back({candidate.text, candidate.code, candidate.syllables});
        }
    }
    const std::string order_path = temporary_file();
    ASSERT_TRUE(fixture.dict->load_manual_candidate_order(order_path));
    ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save("yao", order));
    bool retried = false;
    for (int attempt = 0; attempt < 8 && engine.context().page_index() == 0; ++attempt) {
        ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
        if (engine.context().page_index() == 0) {
            ASSERT_EQ(engine.last_trace().navigation_outcome,
                      cxxime::CandidateNavigationOutcome::kRetryable);
            ASSERT_EQ(engine.context().highlighted(), 0);
            ASSERT_TRUE(cxxime::same_candidate_entry_identity(*engine.context().candidate_entry(0),
                                                              published));
            retried = true;
        }
    }
    ASSERT_TRUE(retried);
    ASSERT_EQ(engine.context().page_index(), 1);
    ASSERT_TRUE(
        !cxxime::same_candidate_entry_identity(*engine.context().candidate_entry(0), published));
    engine.finalize();
    DeleteFileA(order_path.c_str());
}

TEST(PinyinSingleSyllable, real_dictionary_yao_pages_forward_back_and_commits_yao) {
    auto dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dict->open_dict(CXXIME_DATA_DIR "pinyin.dict.bin"));
    cxxime::Config config;
    config.page_size = 7;
    cxxime::Engine engine;
    ASSERT_TRUE(
        test::initialize_engine(engine, dict, config, CXXIME_DATA_DIR "pinyin.spellings.bin"));
    auto resources =
        cxxime::PinyinResourceSet::create("full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin,
                                          CXXIME_DATA_DIR "pinyin.spellings.bin");
    ASSERT_TRUE(resources != nullptr);
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    type(engine, "yao");
    std::vector<std::vector<cxxime::CandidateEntry>> pages;
    int selected_index = -1;
    for (int page = 0; page < 4 && selected_index < 0; ++page) {
        const auto& entries = engine.context().translation().entries;
        ASSERT_TRUE(!entries.empty());
        pages.push_back(entries);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& action = action_of(entries[i]);
            ASSERT_EQ(action.consumed_input_bytes, 3u);
            const auto presentation = cxxime::derive_composition_presentation(
                engine.context().composition(), resources.get(), action.consumed_input_bytes, true,
                action.variants[action.primary_variant].syllables);
            ASSERT_EQ(presentation.logical_preedit, "yao");
            ASSERT_EQ(presentation.display_preedit, "yao");
            if (entries[i].candidate.text == "舀") {
                selected_index = static_cast<int>(i);
            }
        }
        if (selected_index < 0) {
            ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
            ASSERT_EQ(engine.context().page_index(), page + 1);
        }
    }
    ASSERT_GE(selected_index, 0);
    ASSERT_GT(pages.size(), 1u);
    ASSERT_EQ(engine.process_key(key(VK_PRIOR)), cxxime::ProcessResult::ACCEPTED);
    const auto& previous = engine.context().translation().entries;
    const auto& saved = pages[pages.size() - 2];
    ASSERT_EQ(previous.size(), saved.size());
    for (std::size_t i = 0; i < previous.size(); ++i) {
        ASSERT_TRUE(cxxime::same_candidate_entry_identity(previous[i], saved[i]));
    }
    ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().candidate(selected_index)->text, "舀");
    ASSERT_TRUE(engine.select_candidate(selected_index));
    ASSERT_EQ(engine.get_commit_text(), "舀");
    ASSERT_TRUE(!engine.context().is_composing());
    engine.finalize();
}

TEST(PinyinSingleSyllable, real_dictionary_xian_reaches_manual_xi_an_selection) {
    auto dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dict->open_dict(CXXIME_DATA_DIR "pinyin.dict.bin"));
    cxxime::Config config;
    config.page_size = 7;
    cxxime::Engine engine;
    ASSERT_TRUE(
        test::initialize_engine(engine, dict, config, CXXIME_DATA_DIR "pinyin.spellings.bin"));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    type(engine, "xian");
    bool selected = false;
    std::string selected_text;
    for (int attempt = 0; attempt < 1000 && !selected; ++attempt) {
        const auto entries = engine.context().translation().entries;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& action = action_of(entries[i]);
            ASSERT_TRUE(entries[i].match_kind != cxxime::CandidateMatchKind::kPrefix);
            if (action.consumed_input_bytes == 2 &&
                action.variants[action.primary_variant].syllables == "xi") {
                selected_text = entries[i].candidate.text;
                ASSERT_TRUE(engine.select_candidate(static_cast<int>(i)));
                selected = true;
                break;
            }
        }
        if (!selected) {
            const int previous_page = engine.context().page_index();
            ASSERT_EQ(engine.process_key(key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
            if (engine.context().page_index() == previous_page) {
                ASSERT_TRUE(engine.last_trace().navigation_outcome ==
                            cxxime::CandidateNavigationOutcome::kRetryable)
                    << static_cast<int>(engine.last_trace().navigation_outcome);
                const auto& unchanged = engine.context().translation().entries;
                ASSERT_EQ(unchanged.size(), entries.size());
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    ASSERT_TRUE(cxxime::same_candidate_entry_identity(unchanged[i], entries[i]));
                }
            } else {
                ASSERT_EQ(engine.context().page_index(), previous_page + 1)
                    << "anchor=" << entries.back().candidate.text
                    << " consumed=" << action_of(entries.back()).consumed_input_bytes
                    << " known=" << engine.last_trace().candidate_known_count
                    << " complete=" << engine.last_trace().candidate_extent_complete
                    << " effort=" << engine.last_trace().continuation_effort;
            }
        }
    }
    ASSERT_TRUE(selected);
    ASSERT_EQ(engine.context().active_input(), "an");
    ASSERT_EQ(engine.context().committed_text, "");
    const auto& suffix = engine.context().translation().entries;
    const auto& final_entry = find_entry(suffix, "安");
    ASSERT_EQ(action_of(final_entry).consumed_input_bytes, 2u);
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(&final_entry - suffix.data())));
    ASSERT_EQ(engine.get_commit_text(), selected_text + "安");
    ASSERT_TRUE(!engine.context().is_composing());
    engine.finalize();
}
