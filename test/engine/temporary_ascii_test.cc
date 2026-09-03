// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>

#include <windows.h>

#include <cxxime/config.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/input_limits.h>
#include <cxxime/key_event.h>
#include <cxxime/output_options.h>

#include "support/testutil.h"

namespace {

char temp_path[MAX_PATH] = {};
bool temp_path_initialized = GetTempPathA(MAX_PATH, temp_path) != 0;

std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

cxxime::KeyEvent make_key(uint32_t keycode, bool shift = false, bool key_up = false,
                          bool caps_lock = false) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    event.is_key_up = key_up;
    if (shift) {
        event.set_shift();
    }
    if (caps_lock) {
        event.set_caps_lock();
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
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    ASSERT_TRUE(engine.context().candidates.candidates.empty());
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    ASSERT_EQ(engine.process_key(make_key(VK_LSHIFT, false, true)),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "D");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, space_cancels_without_committing) {
    std::string dict_path = make_temp_path("temporary_ascii_space.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    ASSERT_EQ(engine.process_key(make_key(VK_SPACE)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!engine.context().is_composing());
    ASSERT_TRUE(engine.context().committed_text.empty());
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kIme);

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

TEST(TemporaryAscii, standalone_right_shift_preserves_inline_and_switches_to_english) {
    std::string dict_path = make_temp_path("temporary_ascii_right_shift.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    start_uppercase_d(engine);
    ASSERT_EQ(engine.process_key(make_key(VK_RSHIFT, true)), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(engine.process_key(make_key(VK_RSHIFT, false, true)),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "D");
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    ASSERT_EQ(engine.process_key(make_key(VK_RETURN)), cxxime::ProcessResult::COMMITTED);
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
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kIme);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, technical_symbols_extend_ime_preedit_without_host_commit) {
    std::string dict_path = make_temp_path("temporary_ascii_ime_origin.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    ASSERT_EQ(engine.process_key(make_key('D')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!engine.context().candidates.candidates.empty());
    ASSERT_EQ(engine.process_key(make_key(VK_OEM_PLUS, true)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key(VK_ADD)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "d++");
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    ASSERT_TRUE(engine.context().composition_origin().has_value());
    ASSERT_EQ(engine.context().composition_origin()->kind, cxxime::CompositionKind::kIme);
    ASSERT_EQ(engine.context().composition_origin()->code, "d");
    ASSERT_TRUE(engine.context().candidates.candidates.empty());

    ASSERT_EQ(engine.process_key(make_key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "d+");
    ASSERT_EQ(engine.process_key(make_key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "d");
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kIme);
    ASSERT_TRUE(!engine.context().composition_origin().has_value());
    ASSERT_TRUE(!engine.context().candidates.candidates.empty());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, shifted_zero_commits_ime_candidate_with_right_parenthesis) {
    std::string dict_path = make_temp_path("temporary_ascii_shift_zero.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    ASSERT_EQ(engine.process_key(make_key('D')), cxxime::ProcessResult::ACCEPTED);
    const std::string expected =
        engine.context().candidates.candidates.front().text + "\xef\xbc\x89";
    cxxime::PunctMapping punctuation;
    punctuation.half_shape[")"] = {cxxime::PunctType::COMMIT, "\xef\xbc\x89", {}, {}};
    cxxime::OutputOptions options;
    options.punct_mapping = &punctuation;

    ASSERT_EQ(engine.process_key(make_key('0', true), options), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.take_commit_text_with_source().first, expected);
    ASSERT_TRUE(!engine.context().is_composing());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, enter_preserves_exact_bytes_and_mode) {
    std::string dict_path = make_temp_path("temporary_ascii_enter_exact.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    ASSERT_EQ(engine.process_key(make_key('D')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key(VK_OEM_PLUS, true)), cxxime::ProcessResult::ACCEPTED);
    cxxime::OutputOptions options;
    options.caps_lock = true;
    options.full_shape = true;
    ASSERT_EQ(engine.process_key(make_key(VK_RETURN, false, false, true), options),
              cxxime::ProcessResult::COMMITTED);
    auto result = engine.take_commit_text_with_source();
    ASSERT_EQ(result.first, "d+");
    ASSERT_EQ(result.second, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, full_shape_commits_candidate_or_raw_code_before_character) {
    std::string dict_path = make_temp_path("temporary_ascii_full_shape.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));
    cxxime::OutputOptions options;
    options.full_shape = true;

    ASSERT_EQ(engine.process_key(make_key('D'), options), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key(VK_OEM_PLUS, true), options),
              cxxime::ProcessResult::COMMITTED);
    auto candidate = engine.take_commit_text_with_source();
    ASSERT_EQ(candidate.first, "的\xef\xbc\x8b");
    ASSERT_EQ(candidate.second, cxxime::CommitSource::kCandidate);

    ASSERT_EQ(engine.process_key(make_key('Z'), options), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key(VK_OEM_PLUS, true), options),
              cxxime::ProcessResult::COMMITTED);
    auto raw = engine.take_commit_text_with_source();
    ASSERT_EQ(raw.first, "z\xef\xbc\x8b");
    ASSERT_EQ(raw.second, cxxime::CommitSource::kRawCodePreserveCase);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, shared_limit_applies_after_state_transition) {
    cxxime::Context context;
    const std::string full(cxxime::kMaxInputCodeLength, 'a');
    ASSERT_TRUE(context.start_composition(cxxime::CompositionKind::kIme, full, full.size()));
    ASSERT_TRUE(context.enter_inline_ascii(true));

    cxxime::AsciiComposer composer;
    ASSERT_EQ(composer.process_inline_ascii_composition(make_key(VK_OEM_PLUS, true), context, true),
              cxxime::InlineAsciiResult::kAccepted);
    ASSERT_EQ(context.pinyin_buffer, full);
    ASSERT_EQ(context.pinyin_buffer.size(), cxxime::kMaxInputCodeLength);
}

TEST(TemporaryAscii, no_candidate_code_owns_all_printable_digits_and_symbols) {
    std::string dict_path = make_temp_path("temporary_ascii_no_candidate.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    struct Case {
        cxxime::KeyEvent event;
        const char* expected;
    };
    const Case cases[] = {
        {make_key('1', true), "z!"},
        {make_key('0'), "z0"},
        {make_key(VK_NUMPAD2), "z2"},
        {make_key(VK_SUBTRACT), "z-"},
        {make_key(VK_DECIMAL), "z."},
    };
    for (const Case& item : cases) {
        engine.clear();
        ASSERT_EQ(engine.process_key(make_key('Z')), cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(engine.context().candidates.candidates.empty());
        ASSERT_EQ(engine.process_key(item.event), cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(engine.context().pinyin_buffer, item.expected);
        ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    }

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
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kIme);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(TemporaryAscii, active_preedit_precedes_persistent_ascii_and_application_shortcuts) {
    std::string dict_path = make_temp_path("temporary_ascii_active_preedit.bin");
    cxxime::Engine engine;
    ASSERT_TRUE(initialize_engine(engine, dict_path));

    ASSERT_EQ(engine.process_key(make_key('D')), cxxime::ProcessResult::ACCEPTED);
    engine.ascii_composer().set_ascii_mode(true);
    ASSERT_EQ(engine.process_key(make_key(VK_OEM_PLUS, true)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "d+");
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);

    cxxime::KeyEvent shortcut = make_key('C');
    shortcut.set_ctrl();
    ASSERT_EQ(engine.process_key(shortcut), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "d+");
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

RUN_ALL_TESTS()
