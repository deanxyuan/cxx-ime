// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <memory>
#include <string>
#include <variant>

#include <windows.h>

#include <cxxime/composition_presentation.h>
#include <cxxime/engine.h>

#include "support/test_runtime.h"
#include "support/testutil.h"

namespace {

void press(cxxime::Engine& engine, uint32_t keycode) {
    cxxime::KeyEvent key;
    key.keycode = keycode;
    ASSERT_EQ(engine.process_key(key), cxxime::ProcessResult::ACCEPTED);
}

void type(cxxime::Engine& engine, const std::string& input) {
    for (char ch : input) {
        press(engine, static_cast<uint32_t>(ch - 'a' + 'A'));
    }
}

std::size_t find(cxxime::Engine& engine, const std::string& text, std::size_t consumed) {
    const auto& entries = engine.context().translation().entries;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entries[i].selection);
        if (entries[i].candidate.text == text && action &&
            action->consumed_input_bytes == consumed) {
            return i;
        }
    }
    ASSERT_TRUE(false);
    return 0;
}

std::string temp_path(const char* prefix) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    ASSERT_TRUE(GetTempPathA(MAX_PATH, directory) != 0);
    ASSERT_TRUE(GetTempFileNameA(directory, prefix, 0, path) != 0);
    return path;
}

struct CompletionEngineFixture {
    std::string dict_path = temp_path("ptd");
    std::string spellings_path = temp_path("pts");
    std::shared_ptr<cxxime::Dict> dict =
        std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    std::shared_ptr<const cxxime::PinyinResourceSet> resources;
    cxxime::Engine engine;

    CompletionEngineFixture() {
        ASSERT_TRUE(cxxime::Dict::create_test_dict(
            dict_path, {{"ni", "你", 1000}, {"ha", "哈", 100}, {"hao", "好", 10000}}));
        ASSERT_TRUE(dict->open_dict(dict_path));
        ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
            spellings_path, {{"ni", "ni", cxxime::kNormalSpelling, 0.0f},
                             {"ha", "ha", cxxime::kNormalSpelling, 0.0f},
                             {"hao", "hao", cxxime::kNormalSpelling, 0.0f},
                             {"h", "ha", cxxime::kAbbreviation, -1.0f},
                             {"h", "hao", cxxime::kAbbreviation, -1.0f}}));
        resources = cxxime::PinyinResourceSet::create(
            "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spellings_path);
        cxxime::Config config;
        config.candidate_learning = false;
        ASSERT_TRUE(engine.initialize(
            cxxime::EngineRuntimeState::create(config, dict, nullptr, resources)));
        engine.set_partial_selection_enabled(true);
        engine.set_query_deadline_ms(0);
    }

    ~CompletionEngineFixture() {
        engine.finalize();
        dict->close();
        DeleteFileA(dict_path.c_str());
        DeleteFileA(spellings_path.c_str());
    }
};

void assert_sentence_prefix(cxxime::Engine& engine, const std::string& input) {
    ASSERT_EQ(engine.context().active_input(), input);
    ASSERT_TRUE(engine.context().translation().usable());
    const auto& entries = engine.context().translation().entries;
    ASSERT_TRUE(!entries.empty());
    const auto* first_action = std::get_if<cxxime::TextSelectionAction>(&entries.front().selection);
    ASSERT_TRUE(first_action != nullptr);
    ASSERT_EQ(first_action->consumed_input_bytes, input.size()) << input;
    ASSERT_GT(entries.front().candidate.text.size(), std::string("中国人民").size()) << input;
    ASSERT_TRUE(std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
        return action && action->consumed_input_bytes == input.size() &&
               entry.candidate.text.find("中国人民") == 0 &&
               entry.candidate.text.size() > std::string("中国人民").size();
    }));
}

} // namespace

TEST(PinyinTerminalCompletion, selection_and_reopening_preserve_the_actual_keys) {
    CompletionEngineFixture fixture;
    auto& engine = fixture.engine;
    type(engine, "nih");
    const auto index = find(engine, "你好", 3);
    const auto& entry = engine.context().translation().entries[index];
    ASSERT_EQ(entry.candidate.origin, cxxime::CandidateOrigin::kComposed);
    const auto& action = std::get<cxxime::TextSelectionAction>(entry.selection);
    ASSERT_EQ(action.variants[action.primary_variant].syllables, "ni:hao");
    const auto presentation = cxxime::derive_composition_presentation(
        engine.context().composition(), fixture.resources.get(), 3, true,
        action.variants[action.primary_variant].syllables, true);
    ASSERT_EQ(presentation.logical_preedit, "nih");
    ASSERT_EQ(presentation.display_preedit, "ni'h");

    press(engine, 'A');
    ASSERT_EQ(engine.context().translation().entries[0].candidate.text, "你哈");
    press(engine, VK_BACK);
    find(engine, "你好", 3);
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(find(engine, "你", 2))));
    ASSERT_EQ(engine.context().active_input(), "h");
    ASSERT_TRUE(engine.context().committed_text.empty());
    press(engine, VK_LEFT);
    press(engine, VK_BACK);
    ASSERT_EQ(engine.context().active_input(), "nih");
    ASSERT_TRUE(engine.context().composition().converted_segments().empty());
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(find(engine, "你好", 3))));
    ASSERT_EQ(engine.get_commit_text(), "你好");
    ASSERT_TRUE(!engine.context().is_composing());

    type(engine, "nih");
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(find(engine, "你", 2))));
    type(engine, "ao");
    ASSERT_EQ(engine.context().active_input(), "hao");
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(find(engine, "好", 3))));
    ASSERT_EQ(engine.get_commit_text(), "你好");
    ASSERT_TRUE(!engine.context().is_composing());
}

TEST(PinyinTerminalCompletion, production_sentence_keeps_full_span_choices_while_typing) {
    cxxime::Config config;
    config.pinyin_scheme = "full_pinyin";
    config.page_size = 7;
    config.candidate_learning = false;
    cxxime::Engine engine;
    ASSERT_TRUE(test::initialize_engine(engine, CXXIME_DATA_DIR "pinyin.dict.bin", config,
                                        CXXIME_DATA_DIR "pinyin.spellings.bin"));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    const std::string input = "zhongguorenminyongyuanzhanqilaile";
    const std::size_t checked_start = std::string("zhongguorenminyon").size();
    for (std::size_t i = 0; i < input.size(); ++i) {
        press(engine, static_cast<uint32_t>(input[i] - 'a' + 'A'));
        if (i + 1 >= checked_start) {
            assert_sentence_prefix(engine, input.substr(0, i + 1));
        }
    }
    const std::string sentence = "中国人民永远站起来了";
    find(engine, sentence, input.size());
    for (std::size_t remaining = input.size(); remaining > checked_start; --remaining) {
        press(engine, VK_BACK);
        assert_sentence_prefix(engine, input.substr(0, remaining - 1));
    }
    type(engine, input.substr(checked_start));
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(find(engine, sentence, input.size()))));
    ASSERT_EQ(engine.get_commit_text(), sentence);
    ASSERT_TRUE(!engine.context().is_composing());
}
