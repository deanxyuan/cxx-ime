// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>

#include <windows.h>

#include <cxxime/config.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/key_event.h>
#include <cxxime/output_options.h>

#include "util/testutil.h"

namespace {

char temp_path[MAX_PATH] = {};
bool temp_path_initialized = GetTempPathA(MAX_PATH, temp_path) != 0;

std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

cxxime::KeyEvent make_key(uint32_t keycode, bool shift = false, bool key_up = false) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    event.is_key_up = key_up;
    if (shift) {
        event.set_shift();
    }
    return event;
}

void configure_shift(cxxime::Engine& engine) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Shift_R"] = "set_ascii_mode";
    engine.ascii_composer().load_config(config);
}

bool initialize_engine(cxxime::Engine& engine, const std::string& dict_path) {
    if (!temp_path_initialized) {
        return false;
    }
    if (!cxxime::Dict::create_test_dict(dict_path, {{"d", "的", 100}})) {
        return false;
    }
    if (!engine.initialize(dict_path)) {
        return false;
    }
    engine.set_trace_enabled(false);
    configure_shift(engine);
    return true;
}

void start_uppercase_d(cxxime::Engine& engine) {
    ASSERT_EQ(engine.process_key(make_key(VK_LSHIFT, true)), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(engine.process_key(make_key('D', true)), cxxime::ProcessResult::ACCEPTED);
}

}  // namespace

TEST(TemporaryAscii, shift_letter_starts_without_dictionary_candidates) {
    std::string dict_path = make_temp_path("temporary_ascii_start.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);

    ASSERT_EQ(engine.context().pinyin_buffer, "D");
    ASSERT_TRUE(engine.context().temporary_ascii_composition);
    ASSERT_TRUE(engine.context().candidates.candidates.empty());
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    ASSERT_EQ(engine.process_key(make_key(VK_LSHIFT, false, true)),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "D");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, space_commits_preserved_text_and_stays_in_chinese_mode) {
    std::string dict_path = make_temp_path("temporary_ascii_space.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    ASSERT_EQ(engine.process_key(make_key(VK_SPACE)), cxxime::ProcessResult::COMMITTED);

    auto result = engine.take_commit_text_with_source();
    ASSERT_EQ(result.first, "D");
    ASSERT_EQ(result.second, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_TRUE(!engine.context().temporary_ascii_composition);
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, standalone_left_shift_commits_and_switches_to_english) {
    std::string dict_path = make_temp_path("temporary_ascii_shift.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    ASSERT_EQ(engine.process_key(make_key(VK_LSHIFT, true)), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(engine.process_key(make_key(VK_LSHIFT, false, true)),
              cxxime::ProcessResult::COMMITTED);

    auto result = engine.take_commit_text_with_source();
    ASSERT_EQ(result.first, "D");
    ASSERT_EQ(result.second, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, standalone_right_shift_commits_and_switches_to_english) {
    std::string dict_path = make_temp_path("temporary_ascii_right_shift.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    ASSERT_EQ(engine.process_key(make_key(VK_RSHIFT, true)), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(engine.process_key(make_key(VK_RSHIFT, false, true)),
              cxxime::ProcessResult::COMMITTED);

    auto result = engine.take_commit_text_with_source();
    ASSERT_EQ(result.first, "D");
    ASSERT_EQ(result.second, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, enter_commits_preserved_text) {
    std::string dict_path = make_temp_path("temporary_ascii_enter.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    ASSERT_EQ(engine.process_key(make_key(VK_RETURN)), cxxime::ProcessResult::COMMITTED);

    auto result = engine.take_commit_text_with_source();
    ASSERT_EQ(result.first, "D");
    ASSERT_EQ(result.second, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, supports_editing_and_cancel) {
    std::string dict_path = make_temp_path("temporary_ascii_edit.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    ASSERT_EQ(engine.process_key(make_key('O')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('T')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('2')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "Dota2");

    ASSERT_EQ(engine.process_key(make_key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "Dota");
    ASSERT_EQ(engine.process_key(make_key(VK_ESCAPE)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!engine.context().is_composing());
    ASSERT_TRUE(!engine.context().temporary_ascii_composition);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, external_composition_commit_preserves_case) {
    std::string dict_path = make_temp_path("temporary_ascii_external_commit.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    auto result = engine.commit_composition_with_source();
    ASSERT_EQ(result.first, "D");
    ASSERT_EQ(result.second, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_TRUE(!engine.context().temporary_ascii_composition);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

RUN_ALL_TESTS()
