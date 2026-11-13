// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <windows.h>

#include <cxxime/composition_presentation.h>
#include <cxxime/dictionary_manifest.h>
#include <cxxime/engine.h>

#include "support/test_runtime.h"
#include "support/testutil.h"

namespace {

cxxime::ProcessResult press(cxxime::Engine& engine, uint32_t code) {
    cxxime::KeyEvent event;
    event.keycode = code;
    return engine.process_key(event);
}

void type(cxxime::Engine& engine, const std::string& input) {
    for (char ch : input) {
        ASSERT_EQ(press(engine, static_cast<uint32_t>(ch - 'a' + 'A')),
                  cxxime::ProcessResult::ACCEPTED);
    }
}

std::string temporary_file() {
    char directory[MAX_PATH] = {};
    char filename[MAX_PATH] = {};
    ASSERT_TRUE(GetTempPathA(MAX_PATH, directory) != 0);
    ASSERT_TRUE(GetTempFileNameA(directory, "psp", 0, filename) != 0);
    return filename;
}

int find_prefix(cxxime::Engine& engine, std::size_t consumed) {
    const auto& entries = engine.context().translation().entries;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto* action = std::get_if<cxxime::TextSelectionAction>(&entries[index].selection);
        if (action && action->text == "关" && action->consumed_input_bytes == consumed) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

struct SpaceFixture {
    std::string dictionary_path = temporary_file();
    std::string preference_path = temporary_file();
    std::string wubi_path = temporary_file();
    std::shared_ptr<cxxime::Dict> dictionary =
        std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    std::shared_ptr<cxxime::Dict> wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
    cxxime::Engine engine;
    std::string input;
    std::string suffix;

    SpaceFixture(const cxxime::PinyinSchemeDescriptor& scheme, cxxime::InputMode mode) {
        ASSERT_TRUE(cxxime::Dict::create_test_dict(
            dictionary_path, {{"guan:rui", "关瑞", 1000}, {"guan", "关", 900}}));
        ASSERT_TRUE(dictionary->open_dict(dictionary_path));
        ASSERT_TRUE(dictionary->load_candidate_preferences(preference_path));
        ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {{"d", "在", 1000}}));
        ASSERT_TRUE(wubi->open_dict(wubi_path));
        cxxime::Config config;
        config.pinyin_scheme = scheme.id;
        config.candidate_learning = true;
        ASSERT_TRUE(test::initialize_engine(engine, dictionary, config,
                                            std::string(CXXIME_DATA_DIR) + scheme.spelling_filename,
                                            wubi));
        engine.switch_mode(mode);
        ASSERT_EQ(engine.mode(), mode);
        engine.set_partial_selection_enabled(true);
        input = scheme.kind == cxxime::PinyinSchemeKind::kFullPinyin ? "guanrui" : "grrv";
        suffix = scheme.kind == cxxime::PinyinSchemeKind::kFullPinyin ? "rui" : "rv";
    }

    void select_prefix() {
        type(engine, input);
        const int index = find_prefix(engine, input.size() - suffix.size());
        ASSERT_GE(index, 0);
        ASSERT_TRUE(engine.select_candidate(index));
        ASSERT_EQ(engine.context().active_input(), suffix);
        ASSERT_TRUE(engine.context().translation().entries.empty());
        ASSERT_TRUE(engine.context().committed_text.empty());
    }

    ~SpaceFixture() {
        engine.finalize();
        dictionary->close();
        wubi->close();
        DeleteFileA(dictionary_path.c_str());
        DeleteFileA(preference_path.c_str());
        DeleteFileA(wubi_path.c_str());
    }
};

void assert_preserved_space(SpaceFixture& fixture) {
    auto& engine = fixture.engine;
    const auto revision = engine.context().preedit_revision();
    const auto cursor = engine.context().preedit_cursor();
    const auto preedit =
        cxxime::derive_composition_presentation(engine.context().composition()).logical_preedit;
    for (int repeat = 0; repeat < 3; ++repeat) {
        ASSERT_EQ(press(engine, VK_SPACE), cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(engine.context().preedit_revision(), revision);
        ASSERT_EQ(engine.context().preedit_cursor(), cursor);
        ASSERT_EQ(
            cxxime::derive_composition_presentation(engine.context().composition()).logical_preedit,
            preedit);
        ASSERT_TRUE(engine.context().committed_text.empty());
        ASSERT_EQ(fixture.dictionary->candidate_preference_count(), 0u);
        ASSERT_TRUE(engine.context().take_commit_learning_plan().empty());
    }
}

} // namespace

TEST(PinyinSpace, unconverted_remainder_survives_space_and_remains_editable_in_every_scheme) {
    for (const auto& scheme : cxxime::built_in_pinyin_schemes()) {
        for (auto mode : {cxxime::InputMode::PINYIN, cxxime::InputMode::MIXED}) {
            SpaceFixture fixture(scheme, mode);
            fixture.select_prefix();
            // A valid syllable with no standalone dictionary entry is not cancellation.
            assert_preserved_space(fixture);
            ASSERT_EQ(press(fixture.engine, VK_LEFT), cxxime::ProcessResult::ACCEPTED);
            assert_preserved_space(fixture);
            ASSERT_EQ(press(fixture.engine, VK_END), cxxime::ProcessResult::ACCEPTED);
            for (std::size_t index = 0; index < fixture.suffix.size(); ++index) {
                ASSERT_EQ(press(fixture.engine, VK_BACK), cxxime::ProcessResult::ACCEPTED);
            }
            type(fixture.engine, "av");
            ASSERT_TRUE(fixture.engine.context().translation().entries.empty());
            assert_preserved_space(fixture);
            ASSERT_EQ(press(fixture.engine, VK_RETURN), cxxime::ProcessResult::COMMITTED);
            ASSERT_EQ(fixture.engine.get_commit_text(), "关av");
            ASSERT_TRUE(!fixture.engine.context().is_composing());
            ASSERT_EQ(fixture.dictionary->candidate_preference_count(), 1u);
            ASSERT_EQ(press(fixture.engine, VK_SPACE), cxxime::ProcessResult::REJECTED);
            ASSERT_TRUE(fixture.engine.get_commit_text().empty());
        }
    }
}

TEST(PinyinSpace, deleting_the_remainder_commits_only_confirmed_text_once) {
    for (const auto& scheme : cxxime::built_in_pinyin_schemes()) {
        SpaceFixture fixture(scheme, cxxime::InputMode::PINYIN);
        fixture.select_prefix();
        for (std::size_t index = 0; index < fixture.suffix.size(); ++index) {
            ASSERT_EQ(press(fixture.engine, VK_BACK), cxxime::ProcessResult::ACCEPTED);
        }
        ASSERT_TRUE(fixture.engine.context().active_input().empty());
        ASSERT_TRUE(fixture.engine.context().is_composing());
        const auto plan = cxxime::make_raw_learning_plan(fixture.engine.context().composition());
        ASSERT_TRUE(!plan.composition.has_value());
        ASSERT_EQ(plan.candidate_preferences.size(), 1u);
        ASSERT_EQ(press(fixture.engine, VK_SPACE), cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(fixture.engine.get_commit_text(), "关");
        ASSERT_EQ(fixture.dictionary->candidate_preference_count(), 1u);
        ASSERT_EQ(press(fixture.engine, VK_SPACE), cxxime::ProcessResult::REJECTED);
        ASSERT_TRUE(fixture.engine.get_commit_text().empty());
        ASSERT_EQ(fixture.dictionary->candidate_preference_count(), 1u);
    }
}

TEST(PinyinSpace, escape_explicitly_cancels_after_ignored_spaces_without_learning) {
    SpaceFixture fixture(cxxime::default_pinyin_scheme(), cxxime::InputMode::PINYIN);
    fixture.select_prefix();
    assert_preserved_space(fixture);
    ASSERT_EQ(press(fixture.engine, VK_ESCAPE), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!fixture.engine.context().is_composing());
    ASSERT_TRUE(fixture.engine.get_commit_text().empty());
    ASSERT_EQ(fixture.dictionary->candidate_preference_count(), 0u);
}

TEST(PinyinSpace, production_bundle_keeps_selection_on_canonical_boundaries) {
    cxxime::DictionaryManifest manifest;
    std::string error;
    ASSERT_TRUE(cxxime::load_dictionary_manifest(CXXIME_DATA_DIR "dictionary_manifest.json",
                                                 manifest, &error))
        << error;
    std::vector<std::string> roles = {"pinyin_dict", "pinyin_idx", "pinyin_topn"};
    for (const auto& scheme : cxxime::built_in_pinyin_schemes()) {
        roles.push_back(scheme.manifest_role);
    }
    ASSERT_TRUE(cxxime::validate_dictionary_manifest_files(manifest, roles, &error)) << error;
    auto dictionary = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dictionary->open_bundle(CXXIME_DATA_DIR "pinyin.dict.bin", {},
                                        CXXIME_DATA_DIR "pinyin.dict.idx",
                                        CXXIME_DATA_DIR "pinyin.topn.bin"));
    for (const auto& scheme : cxxime::built_in_pinyin_schemes()) {
        cxxime::Config config;
        config.pinyin_scheme = scheme.id;
        config.page_size = 7;
        config.candidate_learning = false;
        cxxime::Engine engine;
        ASSERT_TRUE(test::initialize_engine(
            engine, dictionary, config, std::string(CXXIME_DATA_DIR) + scheme.spelling_filename));
        engine.set_partial_selection_enabled(true);
        const bool full = scheme.kind == cxxime::PinyinSchemeKind::kFullPinyin;
        type(engine, full ? "guanrui" : "grrv");
        for (const auto& entry : engine.context().translation().entries) {
            const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
            ASSERT_TRUE(action != nullptr);
            ASSERT_NE(action->consumed_input_bytes, full ? 6u : 3u);
        }
        const int index = find_prefix(engine, full ? 4u : 2u);
        ASSERT_GE(index, 0);
        ASSERT_TRUE(engine.select_candidate(index));
        ASSERT_EQ(engine.context().active_input(), full ? "rui" : "rv");
        ASSERT_GT(engine.context().candidate_count(), 0);
        const std::string selected = engine.context().candidate(0)->text;
        ASSERT_EQ(press(engine, VK_SPACE), cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(engine.get_commit_text(), "关" + selected);
        ASSERT_EQ(press(engine, VK_SPACE), cxxime::ProcessResult::REJECTED);
        ASSERT_TRUE(engine.get_commit_text().empty());
    }
}
