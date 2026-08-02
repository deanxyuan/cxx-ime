// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cctype>
#include <string>

#include <windows.h>

#include <cxxime/ascii_composer.h>
#include <cxxime/config.h>
#include <cxxime/context.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/key_event.h>
#include <cxxime/processor.h>
#include <cxxime/wubi_processor.h>

#include "util/testutil.h"

namespace {

char temp_path[MAX_PATH] = {};
bool temp_path_initialized = GetTempPathA(MAX_PATH, temp_path) != 0;

std::string make_temp_path(const char* name) { return std::string(temp_path) + "\\" + name; }

cxxime::KeyEvent make_key(uint32_t keycode, bool shift = false) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    if (shift) {
        event.set_shift();
    }
    return event;
}

template <typename Processor>
void type_code(Processor& processor, cxxime::Context& context, const char* code) {
    for (const char* ch = code; *ch != '\0'; ++ch) {
        ASSERT_EQ(processor.process_key(make_key(static_cast<uint32_t>(
                          std::toupper(static_cast<unsigned char>(*ch)))),
                      context),
                  cxxime::ProcessResult::ACCEPTED);
    }
}

template <typename Processor>
void verify_insert_delete_and_boundaries() {
    Processor processor;
    cxxime::Context context;
    type_code(processor, context, "nih");

    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(3));
    ASSERT_EQ(processor.process_key(make_key(VK_LEFT), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(2));
    ASSERT_EQ(processor.process_key(make_key('A'), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer, "niah");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(3));

    ASSERT_EQ(processor.process_key(make_key(VK_BACK), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer, "nih");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(2));
    ASSERT_EQ(processor.process_key(make_key(VK_DELETE), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer, "ni");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(2));

    ASSERT_EQ(processor.process_key(make_key(VK_HOME), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(0));
    ASSERT_EQ(processor.process_key(make_key(VK_LEFT), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(processor.process_key(make_key(VK_BACK), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer, "ni");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(0));

    ASSERT_EQ(processor.process_key(make_key(VK_END), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.preedit_cursor(), context.pinyin_buffer.size());
    ASSERT_EQ(processor.process_key(make_key(VK_RIGHT), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(processor.process_key(make_key(VK_DELETE), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer, "ni");
    ASSERT_EQ(context.preedit_cursor(), context.pinyin_buffer.size());
}

} // namespace

TEST(PreeditEdit, pinyin_supports_insert_delete_and_boundaries) {
    verify_insert_delete_and_boundaries<cxxime::PinyinProcessor>();
}

TEST(PreeditEdit, wubi_supports_insert_delete_and_boundaries) {
    verify_insert_delete_and_boundaries<cxxime::WubiProcessor>();
}

TEST(PreeditEdit, mixed_mode_uses_the_edited_buffer) {
    ASSERT_TRUE(temp_path_initialized);
    const std::string pinyin_path = make_temp_path("preedit_edit_mixed_pinyin.bin");
    const std::string wubi_path = make_temp_path("preedit_edit_mixed_wubi.bin");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, {{"a", "pinyin", 100}}));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {{"a", "wubi", 100}}));

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::MIXED);
    cxxime::Config config;
    config.wubi_auto_commit = false;
    engine.reload_config(config);

    ASSERT_EQ(engine.process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    const size_t candidate_count_before = engine.context().candidates.candidates.size();
    ASSERT_TRUE(candidate_count_before > 0);
    ASSERT_EQ(engine.process_key(make_key(VK_LEFT)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().preedit_cursor(), static_cast<size_t>(0));
    ASSERT_EQ(engine.context().candidates.candidates.size(), candidate_count_before);
    ASSERT_EQ(engine.last_trace().exact_scan_count, 0u);
    ASSERT_EQ(engine.last_trace().prefix_scan_count, 0u);
    ASSERT_EQ(engine.last_trace().user_scan_count, 0u);
    ASSERT_EQ(engine.last_trace().mixed_scan_count, 0u);

    ASSERT_EQ(engine.process_key(make_key(VK_END)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('B')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('C')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key(VK_LEFT)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('X')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "abxc");
    ASSERT_EQ(engine.context().preedit_cursor(), static_cast<size_t>(3));

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(PreeditEdit, temporary_ascii_uses_the_same_cursor_operations) {
    cxxime::AsciiComposer composer;
    cxxime::Context context;

    ASSERT_TRUE(composer.process_temporary_ascii_composition(make_key('D', true), context, true));
    ASSERT_TRUE(composer.process_temporary_ascii_composition(make_key('O'), context, true));
    ASSERT_TRUE(composer.process_temporary_ascii_composition(make_key('T'), context, true));
    ASSERT_TRUE(composer.process_temporary_ascii_composition(make_key(VK_LEFT), context, true));
    ASSERT_TRUE(composer.process_temporary_ascii_composition(make_key('A'), context, true));
    ASSERT_EQ(context.pinyin_buffer, "Doat");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(3));

    ASSERT_TRUE(composer.process_temporary_ascii_composition(make_key(VK_BACK), context, true));
    ASSERT_TRUE(composer.process_temporary_ascii_composition(make_key(VK_DELETE), context, true));
    ASSERT_EQ(context.pinyin_buffer, "Do");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(2));
}

TEST(PreeditEdit, commit_and_reset_restore_cursor_to_zero) {
    cxxime::Context context;
    context.set_preedit("nih");
    ASSERT_TRUE(context.move_preedit_cursor_left());
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(2));
    ASSERT_EQ(context.commit(), "nih");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(0));

    context.set_preedit("abc");
    ASSERT_TRUE(context.move_preedit_cursor_home());
    context.reset();
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(0));
}

RUN_ALL_TESTS()
