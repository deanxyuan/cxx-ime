// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <windows.h>

#include <cxxime/composition_presentation.h>
#include <cxxime/engine.h>
#include <cxxime/input_limits.h>
#include <cxxime/mixed_translator.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/symbol_table.h>
#include <cxxime/wubi_input_policy.h>
#include <cxxime/wubi_translator.h>

#include "support/testutil.h"

namespace {

std::string make_temp_file(const char* prefix) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, prefix, 0, path) == 0) {
        return {};
    }
    return path;
}

cxxime::KeyEvent make_key(uint32_t keycode, bool shift = false) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    if (shift) {
        event.set_shift();
    }
    return event;
}

struct SegmentedFixture {
    std::string dict_path = make_temp_file("sgd");
    std::string spellings_path = make_temp_file("sgs");
    cxxime::Dict dict;
    cxxime::SpellingsIndex spellings;
    std::unique_ptr<cxxime::Syllabifier> syllabifier;
    cxxime::Config config;
    cxxime::Engine engine;

    bool initialize(bool include_suffix = true, int extra_full_span_count = 0) {
        std::vector<std::tuple<std::string, std::string, int>> entries = {
            {"hua:rui:ji:shu", "华锐技术", 12000},
            {"hua:rui", "华锐", 9000},
            {"hua:rui", "花蕊", 8000},
            {"hua", "华", 4000},
        };
        if (include_suffix) {
            entries.push_back({"ji:shu", "技术", 10000});
        }
        for (int index = 0; index < extra_full_span_count; ++index) {
            entries.push_back(
                {"hua:rui:ji:shu", "full-" + std::to_string(index), 7000 - index});
        }
        if (dict_path.empty() || spellings_path.empty() ||
            !cxxime::Dict::create_test_dict(dict_path, entries) || !dict.open_dict(dict_path) ||
            !cxxime::SpellingsIndex::create_test_trie(
                spellings_path,
                {{"hua", "hua", cxxime::kNormalSpelling, 0.0f},
                 {"rui", "rui", cxxime::kNormalSpelling, 0.0f},
                 {"ji", "ji", cxxime::kNormalSpelling, 0.0f},
                 {"shu", "shu", cxxime::kNormalSpelling, 0.0f}}) ||
            !spellings.load(spellings_path)) {
            return false;
        }
        syllabifier = std::make_unique<cxxime::Syllabifier>(spellings);
        config.page_size = 5;
        if (!engine.initialize(dict, spellings, syllabifier.get(), config)) {
            return false;
        }
        engine.set_query_deadline_ms(0);
        engine.set_partial_selection_enabled(true);
        return true;
    }

    void type(const std::string& code) {
        for (char ch : code) {
            ASSERT_EQ(engine.process_key(make_key(static_cast<uint32_t>(ch - 'a' + 'A'))),
                      cxxime::ProcessResult::ACCEPTED);
        }
    }

    int find(const std::string& text, std::size_t consumed) const {
        const auto& entries = engine.context().translation().entries;
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto* action =
                std::get_if<cxxime::TextSelectionAction>(&entries[index].selection);
            if (entries[index].candidate.text == text && action &&
                action->consumed_input_bytes == consumed) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    ~SegmentedFixture() {
        engine.finalize();
        dict.close();
        spellings.unload();
        if (!dict_path.empty()) {
            DeleteFileA(dict_path.c_str());
        }
        if (!spellings_path.empty()) {
            DeleteFileA(spellings_path.c_str());
        }
    }
};

} // namespace

TEST(SegmentedSelection, keyboard_confirms_prefix_and_finalizes_once) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");

    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(prefix, 0);
    ASSERT_LT(prefix, 9);
    ASSERT_EQ(fixture.engine.process_key(make_key('1' + prefix)),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
    ASSERT_TRUE(fixture.engine.context().is_composing());
    ASSERT_EQ(fixture.engine.context().active_input(), "jishu");
    ASSERT_EQ(fixture.engine.context().composition().converted_segments().size(), 1u);

    const cxxime::CompositionPresentation presentation =
        cxxime::derive_composition_presentation(fixture.engine.context().composition());
    ASSERT_EQ(presentation.logical_preedit, "华锐jishu");
    ASSERT_EQ(presentation.converted_prefix_bytes, std::string("华锐").size());

    const int suffix = fixture.find("技术", 5);
    ASSERT_GE(suffix, 0);
    ASSERT_EQ(fixture.engine.process_key(make_key('1' + suffix)),
              cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "华锐技术");
    ASSERT_TRUE(!fixture.engine.context().is_composing());
}

TEST(SegmentedSelection, mouse_selection_uses_the_same_action_dispatcher) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");

    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(prefix, 0);
    ASSERT_TRUE(fixture.engine.select_candidate(prefix));
    ASSERT_EQ(fixture.engine.context().active_input(), "jishu");
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());

    const int suffix = fixture.find("技术", 5);
    ASSERT_GE(suffix, 0);
    ASSERT_TRUE(fixture.engine.select_candidate(suffix));
    ASSERT_EQ(fixture.engine.get_commit_text(), "华锐技术");
}

TEST(SegmentedSelection, backspace_reopens_the_last_confirmed_segment) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    ASSERT_TRUE(fixture.engine.select_candidate(fixture.find("华锐", 6)));

    ASSERT_EQ(fixture.engine.process_key(make_key(VK_HOME)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.process_key(make_key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(fixture.engine.context().composition().converted_segments().empty());
    ASSERT_EQ(fixture.engine.context().active_input(), "huaruijishu");
    ASSERT_EQ(fixture.engine.context().preedit_cursor(), 6u);
    ASSERT_GE(fixture.find("华锐技术", 11), 0);
}

TEST(SegmentedSelection, enter_commits_converted_prefix_and_raw_suffix) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    ASSERT_TRUE(fixture.engine.select_candidate(fixture.find("华锐", 6)));

    ASSERT_EQ(fixture.engine.process_key(make_key(VK_RETURN)), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "华锐jishu");
}

TEST(SegmentedSelectionLearning, final_candidate_learns_segments_and_whole_composition) {
    const std::string learning_path = make_temp_file("sgl");
    cxxime::CompositionLearningService learning;
    ASSERT_TRUE(learning.load(learning_path));
    ASSERT_TRUE(learning.start());
    {
        SegmentedFixture fixture;
        ASSERT_TRUE(fixture.initialize());
        fixture.config.candidate_learning = true;
        fixture.engine.reload_config(fixture.config);
        fixture.engine.set_composition_learning_service(&learning);
        fixture.type("huaruijishu");

        auto find_consumed = [&](std::size_t consumed) {
            const auto& entries = fixture.engine.context().translation().entries;
            for (std::size_t index = 0; index < entries.size(); ++index) {
                const auto* action =
                    std::get_if<cxxime::TextSelectionAction>(&entries[index].selection);
                if (action && action->consumed_input_bytes == consumed) {
                    return static_cast<int>(index);
                }
            }
            return -1;
        };

        const int prefix_index = find_consumed(6);
        ASSERT_GE(prefix_index, 0);
        const std::string prefix_text =
            fixture.engine.context().translation().entries[prefix_index].candidate.text;
        ASSERT_TRUE(fixture.engine.select_candidate(prefix_index));
        const int suffix_index = find_consumed(5);
        ASSERT_GE(suffix_index, 0);
        const std::string suffix_text =
            fixture.engine.context().translation().entries[suffix_index].candidate.text;
        ASSERT_TRUE(fixture.engine.select_candidate(suffix_index));
        const std::string committed = fixture.engine.get_commit_text();
        ASSERT_EQ(committed, prefix_text + suffix_text);
        ASSERT_TRUE(fixture.dict.has_candidate_preference(prefix_text, "huarui"));
        ASSERT_TRUE(fixture.dict.has_candidate_preference(suffix_text, "jishu"));
    }
    ASSERT_TRUE(learning.freeze_and_stop());
    const auto candidates = learning.lookup_candidates("huaruijishu", 10);
    ASSERT_EQ(candidates.size(), static_cast<std::size_t>(1));
    DeleteFileA(learning_path.c_str());
}

TEST(SegmentedSelectionLearning, raw_commit_learns_only_confirmed_segments) {
    const std::string learning_path = make_temp_file("sgr");
    cxxime::CompositionLearningService learning;
    ASSERT_TRUE(learning.load(learning_path));
    ASSERT_TRUE(learning.start());
    {
        SegmentedFixture fixture;
        ASSERT_TRUE(fixture.initialize());
        fixture.config.candidate_learning = true;
        fixture.engine.reload_config(fixture.config);
        fixture.engine.set_composition_learning_service(&learning);
        fixture.type("huaruijishu");

        int prefix_index = -1;
        for (std::size_t index = 0;
            index < fixture.engine.context().translation().entries.size();
            ++index) {
            const auto* action = std::get_if<cxxime::TextSelectionAction>(
                &fixture.engine.context().translation().entries[index].selection);
            if (action && action->consumed_input_bytes == 6) {
                prefix_index = static_cast<int>(index);
                break;
            }
        }
        ASSERT_GE(prefix_index, 0);
        const std::string prefix_text =
            fixture.engine.context().translation().entries[prefix_index].candidate.text;
        ASSERT_TRUE(fixture.engine.select_candidate(prefix_index));
        ASSERT_EQ(fixture.engine.process_key(make_key(VK_RETURN)),
                  cxxime::ProcessResult::COMMITTED);
        fixture.engine.get_commit_text();
        ASSERT_TRUE(fixture.dict.has_candidate_preference(prefix_text, "huarui"));
    }
    ASSERT_TRUE(learning.freeze_and_stop());
    ASSERT_EQ(learning.entry_count(), static_cast<std::size_t>(0));
    DeleteFileA(learning_path.c_str());
}

TEST(SegmentedSelectionLearning, cancelled_composition_does_not_learn_partial_selection) {
    const std::string learning_path = make_temp_file("sgc");
    cxxime::CompositionLearningService learning;
    ASSERT_TRUE(learning.load(learning_path));
    ASSERT_TRUE(learning.start());
    {
        SegmentedFixture fixture;
        ASSERT_TRUE(fixture.initialize());
        fixture.config.candidate_learning = true;
        fixture.engine.reload_config(fixture.config);
        fixture.engine.set_composition_learning_service(&learning);
        fixture.type("huaruijishu");
        int prefix = -1;
        for (std::size_t index = 0;
            index < fixture.engine.context().translation().entries.size();
             ++index) {
            const auto* action = std::get_if<cxxime::TextSelectionAction>(
                &fixture.engine.context().translation().entries[index].selection);
            if (action && action->consumed_input_bytes == 6) {
                prefix = static_cast<int>(index);
                break;
            }
        }
        ASSERT_GE(prefix, 0);
        const std::string prefix_text =
            fixture.engine.context().translation().entries[prefix].candidate.text;
        ASSERT_TRUE(fixture.engine.select_candidate(prefix));
        ASSERT_EQ(fixture.engine.process_key(make_key(VK_ESCAPE)),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(!fixture.dict.has_candidate_preference(prefix_text, "huarui"));
    }
    ASSERT_TRUE(learning.freeze_and_stop());
    ASSERT_EQ(learning.entry_count(), static_cast<std::size_t>(0));
    DeleteFileA(learning_path.c_str());
}

TEST(SegmentedSelection, punctuation_finalizes_the_remaining_candidate_once) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    ASSERT_TRUE(fixture.engine.select_candidate(fixture.find("华锐", 6)));

    cxxime::PunctMapping punctuation;
    punctuation.half_shape["."] = {cxxime::PunctType::COMMIT, "。", {}, {}};
    cxxime::OutputOptions options;
    options.punct_mapping = &punctuation;
    ASSERT_EQ(fixture.engine.process_key(make_key(VK_OEM_PERIOD), options),
              cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "华锐技术。");
    ASSERT_TRUE(!fixture.engine.context().is_composing());
}

TEST(SegmentedSelection, candidate_shift_finalizes_a_highlighted_prefix_and_raw_suffix) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.config.ascii_switch_key["Shift_L"] = "candidate";
    fixture.engine.reload_config(fixture.config);
    fixture.type("huaruijishu");
    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(prefix, 0);
    fixture.engine.context().translation().highlighted = prefix;

    ASSERT_EQ(fixture.engine.process_key(make_key(VK_LSHIFT)), cxxime::ProcessResult::REJECTED);
    cxxime::KeyEvent shift_up = make_key(VK_LSHIFT);
    shift_up.is_key_up = true;
    ASSERT_EQ(fixture.engine.process_key(shift_up), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "华锐jishu");
    ASSERT_TRUE(fixture.engine.ascii_composer().is_ascii_mode());
}

TEST(SegmentedSelection, inline_ascii_preserves_confirmed_prefix) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    ASSERT_TRUE(fixture.engine.select_candidate(fixture.find("华锐", 6)));

    ASSERT_EQ(fixture.engine.process_key(make_key(VK_OEM_PLUS, true)),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().composition_scheme(),
              cxxime::CompositionScheme::kInlineAscii);
    ASSERT_EQ(fixture.engine.context().active_input(), "jishu+");
    ASSERT_EQ(fixture.engine.process_key(make_key(VK_RETURN)), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "华锐jishu+");
}

TEST(SegmentedSelection, empty_remainder_is_a_valid_transaction_result) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize(false));
    fixture.type("huaruijishu");

    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(prefix, 0);
    ASSERT_TRUE(fixture.engine.select_candidate(prefix));
    ASSERT_TRUE(fixture.engine.context().is_composing());
    ASSERT_EQ(fixture.engine.context().active_input(), "jishu");
    ASSERT_TRUE(fixture.engine.context().translation().entries.empty());
    ASSERT_EQ(fixture.engine.context().translation().status, cxxime::TranslationStatus::kSuccess);
}

TEST(SegmentedSelection, failed_remainder_query_keeps_the_previous_state) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(prefix, 0);
    const uint64_t revision = fixture.engine.context().preedit_revision();
    const int candidate_count = fixture.engine.context().candidate_count();

    fixture.dict.unload_dict();
    ASSERT_TRUE(!fixture.engine.select_candidate(prefix));
    ASSERT_EQ(fixture.engine.context().preedit_revision(), revision);
    ASSERT_EQ(fixture.engine.context().active_input(), "huaruijishu");
    ASSERT_TRUE(fixture.engine.context().composition().converted_segments().empty());
    ASSERT_EQ(fixture.engine.context().candidate_count(), candidate_count);
}

TEST(SegmentedSelection, failed_regular_query_drops_actions_from_the_previous_input) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huarui");
    ASSERT_TRUE(fixture.engine.context().candidate_count() > 0);

    fixture.dict.unload_dict();
    ASSERT_EQ(fixture.engine.process_key(make_key('J')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().active_input(), "huaruij");
    ASSERT_EQ(fixture.engine.context().candidate_count(), 0);
}

TEST(SegmentedSelection, presentation_overflow_keeps_the_previous_state) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(prefix, 0);

    cxxime::CandidateEntry oversized =
        fixture.engine.context().translation().entries[static_cast<std::size_t>(prefix)];
    auto& action = std::get<cxxime::TextSelectionAction>(oversized.selection);
    action.text.assign(254, 'x');
    oversized.candidate.text = action.text;
    fixture.engine.context().translation().entries[static_cast<std::size_t>(prefix)] =
        std::move(oversized);
    const uint64_t revision = fixture.engine.context().preedit_revision();

    ASSERT_TRUE(!fixture.engine.select_candidate(prefix));
    ASSERT_EQ(fixture.engine.context().preedit_revision(), revision);
    ASSERT_EQ(fixture.engine.context().active_input(), "huaruijishu");
    ASSERT_TRUE(fixture.engine.context().composition().converted_segments().empty());
}

TEST(SegmentedSelection, failed_full_selection_keeps_the_previous_highlight) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    const int full = fixture.find("华锐技术", 11);
    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(full, 0);
    ASSERT_GE(prefix, 0);

    fixture.engine.context().translation().highlighted = prefix;
    auto& action = std::get<cxxime::TextSelectionAction>(
        fixture.engine.context().translation().entries[static_cast<std::size_t>(full)].selection);
    action.primary_variant = action.variants.size();

    ASSERT_TRUE(!fixture.engine.select_candidate(full));
    ASSERT_EQ(fixture.engine.context().highlighted(), prefix);
    ASSERT_EQ(fixture.engine.context().active_input(), "huaruijishu");
}

TEST(SegmentedSelection, escape_after_prefix_confirmation_cancels_the_whole_composition) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("huaruijishu");
    ASSERT_TRUE(fixture.engine.select_candidate(fixture.find("华锐", 6)));

    ASSERT_EQ(fixture.engine.process_key(make_key(VK_ESCAPE)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!fixture.engine.context().is_composing());
    ASSERT_TRUE(fixture.engine.context().composition().converted_segments().empty());
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
}

TEST(SegmentedSelection, disabled_policy_preserves_legacy_page) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dict);
    translator.set_syllabifier(fixture.syllabifier.get());
    const cxxime::CandidatePage legacy = translator.translate_page("huaruijishu", 0, 5);

    cxxime::TranslationRequest request;
    request.input = "huaruijishu";
    request.page_size = 5;
    const cxxime::CandidatePage current = translator.translate(request).candidate_page();
    ASSERT_EQ(current.total_count, legacy.total_count);
    ASSERT_EQ(current.highlighted, legacy.highlighted);
    ASSERT_EQ(current.candidates.size(), legacy.candidates.size());
    for (std::size_t index = 0; index < legacy.candidates.size(); ++index) {
        ASSERT_EQ(current.candidates[index].text, legacy.candidates[index].text);
        ASSERT_EQ(current.candidates[index].code, legacy.candidates[index].code);
        ASSERT_EQ(current.candidates[index].syllables, legacy.candidates[index].syllables);
    }
}

TEST(SegmentedSelection, partial_candidates_share_disabled_and_learning_policy) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    ASSERT_TRUE(fixture.dict.disable_system_entry("华锐"));

    cxxime::Candidate preferred;
    preferred.text = "花蕊";
    preferred.code = "huarui";
    preferred.syllables = "hua:rui";
    preferred.source = cxxime::CandidateSource::kPinyin;
    preferred.origin = cxxime::CandidateOrigin::kSystem;
    ASSERT_TRUE(fixture.dict.record_candidate_preference(preferred, "huarui"));

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dict);
    translator.set_syllabifier(fixture.syllabifier.get());
    translator.set_candidate_learning_enabled(true);
    cxxime::TranslationRequest request;
    request.input = "huaruijishu";
    request.page_size = 20;
    request.policy.allow_partial_selection = true;
    const cxxime::TranslationResult result = translator.translate(request);

    int first_prefix_index = -1;
    int preferred_index = -1;
    for (std::size_t index = 0; index < result.entries.size(); ++index) {
        const auto* action =
            std::get_if<cxxime::TextSelectionAction>(&result.entries[index].selection);
        if (!action || action->consumed_input_bytes != 6) {
            continue;
        }
        if (first_prefix_index < 0) {
            first_prefix_index = static_cast<int>(index);
        }
        ASSERT_NE(result.entries[index].candidate.text, "华锐");
        if (result.entries[index].candidate.text == "花蕊") {
            preferred_index = static_cast<int>(index);
        }
    }
    ASSERT_GE(preferred_index, 0);
    ASSERT_EQ(preferred_index, first_prefix_index);
}

TEST(SegmentedSelection, fuzzy_and_abbreviation_paths_use_input_boundaries) {
    const std::string dict_path = make_temp_file("sgf");
    const std::string spellings_path = make_temp_file("sga");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(
        dict_path, {{"zhong:guo:ren", "中国人", 10000}, {"zhong:guo", "中国", 9000}}));
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path,
        {{"zong", "zhong", cxxime::kFuzzySpelling, -0.5f},
         {"guo", "guo", cxxime::kNormalSpelling, 0.0f},
         {"ren", "ren", cxxime::kNormalSpelling, 0.0f},
         {"z", "zhong", cxxime::kAbbreviation, -1.0f},
         {"g", "guo", cxxime::kAbbreviation, -1.0f},
         {"r", "ren", cxxime::kAbbreviation, -1.0f}}));
    cxxime::Dict dict;
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(dict.open_dict(dict_path));
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto prefix_consumed = [&](const std::string& input) {
        cxxime::TranslationRequest request;
        request.input = input;
        request.page_size = 20;
        request.policy.allow_partial_selection = true;
        const cxxime::TranslationResult result = translator.translate(request);
        for (const auto& entry : result.entries) {
            if (entry.candidate.text != "中国") {
                continue;
            }
            const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
            if (action && action->consumed_input_bytes < input.size()) {
                return action->consumed_input_bytes;
            }
        }
        return std::size_t{0};
    };

    ASSERT_EQ(prefix_consumed("zongguoren"), 7u);
    ASSERT_EQ(prefix_consumed("zgr"), 2u);

    spellings.unload();
    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(SegmentedSelection, first_page_inserts_prefix_without_losing_a_full_candidate) {
    const std::string dict_path = make_temp_file("sgp");
    const std::string spellings_path = make_temp_file("sgq");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(
        dict_path,
        {{"hua:rui:ji:shu", "完整一", 12000},
         {"hua:rui:ji:shu", "完整二", 11000},
         {"hua:rui:ji:shu", "完整三", 10000},
         {"hua:rui:ji:shu", "完整四", 9000},
         {"hua:rui", "华锐", 8000},
         {"hua", "华", 7000}}));
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path,
        {{"hua", "hua", cxxime::kNormalSpelling, 0.0f},
         {"rui", "rui", cxxime::kNormalSpelling, 0.0f},
         {"ji", "ji", cxxime::kNormalSpelling, 0.0f},
         {"shu", "shu", cxxime::kNormalSpelling, 0.0f}}));
    cxxime::Dict dict;
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(dict.open_dict(dict_path));
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    const cxxime::CandidatePage legacy = translator.translate_page("huaruijishu", 0, 6);
    ASSERT_GE(legacy.candidates.size(), 4u);
    cxxime::TranslationRequest request;
    request.input = "huaruijishu";
    request.page_size = 3;
    request.policy.allow_partial_selection = true;
    const cxxime::TranslationResult first = translator.translate(request);
    ASSERT_EQ(first.entries[0].candidate.text, legacy.candidates[0].text);
    ASSERT_TRUE(std::any_of(first.entries.begin(), first.entries.end(), [](const auto& entry) {
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
        return entry.candidate.text == "华锐" && action && action->consumed_input_bytes == 6;
    }));

    request.page_index = 1;
    request.page_offset = 3;
    const cxxime::TranslationResult second = translator.translate(request);
    ASSERT_TRUE(!second.entries.empty());
    ASSERT_TRUE(std::any_of(second.entries.begin(), second.entries.end(), [](const auto& entry) {
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
        return action && action->consumed_input_bytes == 3;
    }));
    ASSERT_TRUE(std::any_of(second.entries.begin(), second.entries.end(),
                            [&](const auto& entry) {
                                return entry.candidate.text == legacy.candidates[2].text;
                            }));

    translator.clear_query_cache();
    cxxime::QueryBudget constrained;
    constrained.max_exact_scan = 1;
    cxxime::QueryTrace full_only_trace;
    cxxime::TranslationRequest full_only_request = request;
    full_only_request.page_index = 0;
    full_only_request.page_offset = 0;
    full_only_request.page_size = request.page_size + 1;
    full_only_request.policy.allow_partial_selection = false;
    full_only_request.budget = &constrained;
    full_only_request.trace = &full_only_trace;
    ASSERT_TRUE(!translator.translate(full_only_request).entries.empty());

    translator.clear_query_cache();
    cxxime::QueryTrace constrained_trace;
    request.page_index = 0;
    request.page_offset = 0;
    request.budget = &constrained;
    request.trace = &constrained_trace;
    const cxxime::TranslationResult degraded = translator.translate(request);
    ASSERT_EQ(degraded.status, cxxime::TranslationStatus::kStableDegraded);
    ASSERT_TRUE(!degraded.entries.empty());
    ASSERT_EQ(constrained_trace.span_entry_scan_count,
              full_only_trace.span_entry_scan_count + constrained.max_exact_scan);
    ASSERT_TRUE(constrained_trace.truncated);

    spellings.unload();
    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(SegmentedSelection, mixed_keeps_same_text_with_different_actions) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const std::string wubi_path = make_temp_file("sgw");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {{"huaruijishu", "华锐", 20000}}));
    cxxime::Dict wubi;
    ASSERT_TRUE(wubi.open_dict(wubi_path));

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&fixture.dict);
    translator.set_wubi_dict(&wubi);
    translator.set_syllabifier(fixture.syllabifier.get());
    cxxime::TranslationRequest request;
    request.scheme = cxxime::CompositionScheme::kMixed;
    request.input = "huaruijishu";
    request.page_size = 20;
    request.policy.allow_partial_selection = true;
    const cxxime::TranslationResult result = translator.translate(request);

    std::vector<std::size_t> consumed;
    for (const auto& entry : result.entries) {
        if (entry.candidate.text != "华锐") {
            continue;
        }
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
        ASSERT_TRUE(action != nullptr);
        consumed.push_back(action->consumed_input_bytes);
        ASSERT_TRUE(!entry.annotation.empty());
    }
    std::sort(consumed.begin(), consumed.end());
    ASSERT_EQ(consumed.size(), 2u);
    ASSERT_EQ(consumed[0], 6u);
    ASSERT_EQ(consumed[1], 11u);

    wubi.close();
    DeleteFileA(wubi_path.c_str());
}

TEST(SegmentedSelection, mixed_preserves_usable_provider_when_other_provider_fails) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&fixture.dict);
    translator.set_syllabifier(fixture.syllabifier.get());
    cxxime::TranslationRequest request;
    request.scheme = cxxime::CompositionScheme::kMixed;
    request.input = "huaruijishu";
    request.page_size = 20;
    request.policy.allow_partial_selection = true;

    const cxxime::TranslationResult result = translator.translate(request);
    ASSERT_EQ(result.status, cxxime::TranslationStatus::kStableDegraded);
    ASSERT_TRUE(!result.entries.empty());
}

TEST(SegmentedSelection, mixed_prefix_order_does_not_replace_the_full_span_first_choice) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const std::string order_path = make_temp_file("sgo");
    ASSERT_TRUE(fixture.dict.load_manual_candidate_order(order_path, cxxime::kMaxInputCodeLength));
    ASSERT_TRUE(fixture.dict.replace_manual_candidate_order_and_save(
        "huarui", {{"花蕊", "huarui", "hua:rui"}}));

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&fixture.dict);
    translator.set_syllabifier(fixture.syllabifier.get());
    cxxime::TranslationRequest request;
    request.scheme = cxxime::CompositionScheme::kMixed;
    request.input = "huaruijishu";
    request.page_size = 20;
    request.policy.allow_partial_selection = true;

    const cxxime::TranslationResult result = translator.translate(request);
    ASSERT_TRUE(!result.entries.empty());
    const auto* first_action =
        std::get_if<cxxime::TextSelectionAction>(&result.entries.front().selection);
    ASSERT_TRUE(first_action != nullptr);
    ASSERT_EQ(first_action->consumed_input_bytes, request.input.size());
    ASSERT_EQ(result.entries.front().candidate.text, "华锐技术");

    DeleteFileA(order_path.c_str());
}

TEST(SegmentedSelection, mixed_reserves_longest_partial_on_a_full_first_page) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize(true, 12));

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&fixture.dict);
    translator.set_syllabifier(fixture.syllabifier.get());
    cxxime::TranslationRequest request;
    request.scheme = cxxime::CompositionScheme::kMixed;
    request.input = "huaruijishu";
    request.page_size = 5;
    request.policy.allow_partial_selection = true;

    const cxxime::TranslationResult result = translator.translate(request);
    ASSERT_EQ(result.entries.size(), 5u);
    const auto* first = std::get_if<cxxime::TextSelectionAction>(&result.entries[0].selection);
    ASSERT_TRUE(first != nullptr);
    ASSERT_EQ(first->consumed_input_bytes, request.input.size());
    ASSERT_TRUE(std::any_of(result.entries.begin(), result.entries.end(), [&](const auto& entry) {
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
        return action && action->consumed_input_bytes == 6;
    }));
}

TEST(SegmentedSelection, mixed_merges_sources_for_the_same_text_and_action) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const std::string wubi_path = make_temp_file("sgv");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {{"huaruijishu", "华锐技术", 20000}}));
    cxxime::Dict wubi;
    ASSERT_TRUE(wubi.open_dict(wubi_path));

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&fixture.dict);
    translator.set_wubi_dict(&wubi);
    translator.set_syllabifier(fixture.syllabifier.get());
    cxxime::TranslationRequest request;
    request.scheme = cxxime::CompositionScheme::kMixed;
    request.input = "huaruijishu";
    request.page_size = 20;
    request.policy.allow_partial_selection = true;
    const cxxime::TranslationResult result = translator.translate(request);

    int matching_entries = 0;
    for (const auto& entry : result.entries) {
        if (entry.candidate.text != "华锐技术") {
            continue;
        }
        ++matching_entries;
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
        ASSERT_TRUE(action != nullptr);
        ASSERT_EQ(action->consumed_input_bytes, 11u);
        ASSERT_EQ(action->variants.size(), 2u);
        ASSERT_TRUE(entry.annotation.empty());
    }
    ASSERT_EQ(matching_entries, 1);

    wubi.close();
    DeleteFileA(wubi_path.c_str());
}

TEST(SegmentedSelection, mixed_requeries_the_remainder_with_mixed_policy) {
    SegmentedFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const std::string wubi_path = make_temp_file("sgm");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {{"zzzz", "五笔", 100}}));
    cxxime::Dict wubi;
    ASSERT_TRUE(wubi.open_dict(wubi_path));
    fixture.engine.set_wubi_dict(&wubi);
    fixture.engine.switch_mode(cxxime::InputMode::MIXED);
    fixture.type("huaruijishu");

    const int prefix = fixture.find("华锐", 6);
    ASSERT_GE(prefix, 0);
    ASSERT_TRUE(fixture.engine.select_candidate(prefix));
    ASSERT_EQ(fixture.engine.context().composition_scheme(), cxxime::CompositionScheme::kMixed);
    ASSERT_GE(fixture.find("技术", 5), 0);

    wubi.close();
    DeleteFileA(wubi_path.c_str());
}

TEST(SegmentedSelection, wubi_policy_depends_on_the_visible_candidate_source) {
    cxxime::Config config;
    config.wubi_commit_first_on_fifth_key = true;
    cxxime::TranslationResult result;
    result.total_count = 2;
    result.entries.push_back(cxxime::make_text_candidate_entry(
        cxxime::Candidate{"拼音", {}, 100, cxxime::CandidateSource::kPinyin}, 4));
    ASSERT_EQ(cxxime::WubiInputPolicy::fifth_key_action(cxxime::CompositionScheme::kMixed, "abcd",
                                                        4, result, config, 'E'),
              cxxime::WubiFifthKeyAction::kNone);

    result.entries[0].candidate.source = cxxime::CandidateSource::kWubi;
    ASSERT_EQ(cxxime::WubiInputPolicy::fifth_key_action(cxxime::CompositionScheme::kMixed, "abcd",
                                                        4, result, config, 'E'),
              cxxime::WubiFifthKeyAction::kCommitFirstAndRestart);
}

TEST(SegmentedSelection, symbol_categories_use_typed_replacement_actions) {
    cxxime::SymbolTable table;
    ASSERT_TRUE(table.load(CXXIME_DATA_DIR "symbols.json"));
    cxxime::TranslationRequest request;
    request.scheme = cxxime::CompositionScheme::kSymbol;
    request.page_size = 9;
    const cxxime::TranslationResult categories = table.translate(request);
    ASSERT_TRUE(!categories.entries.empty());
    const auto* replacement =
        std::get_if<cxxime::ReplaceActiveInputAction>(&categories.entries[0].selection);
    ASSERT_TRUE(replacement != nullptr);
    ASSERT_EQ(replacement->scheme, cxxime::CompositionScheme::kSymbol);
    ASSERT_TRUE(!replacement->input.empty());
    ASSERT_EQ(replacement->input.front(), '\\');
}

TEST(SegmentedSelection, wubi_candidates_only_use_full_span_text_actions) {
    const std::string dict_path = make_temp_file("sgx");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {{"abcd", "五笔", 100}}));
    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::WubiTranslator translator;
    translator.set_dict(&dict);
    cxxime::TranslationRequest request;
    request.scheme = cxxime::CompositionScheme::kWubi;
    request.input = "abcd";
    request.page_size = 9;
    request.policy.allow_partial_selection = true;
    const cxxime::TranslationResult result = translator.translate(request);

    ASSERT_TRUE(!result.entries.empty());
    for (const auto& entry : result.entries) {
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
        ASSERT_TRUE(action != nullptr);
        ASSERT_EQ(action->consumed_input_bytes, request.input.size());
    }

    dict.close();
    DeleteFileA(dict_path.c_str());
}

int main() { return test::RunAllTests(); }
