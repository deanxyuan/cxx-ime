// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_test_support.h"
TEST(AsciiComposer, shift_l_code_toggles_and_commits) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Shift_R"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Simulate composing state with pinyin
    ASSERT_TRUE(ctx.set_preedit("ni"));
    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press Shift_L (key-down)
    ac.process_key(0xA0, false, ctx);

    // Release Shift_L (key-up) — should commit and toggle to ascii mode
    ac.process_key(0xA0, true, ctx);

    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_TRUE(!ctx.committed_text.empty());
}

TEST(AsciiComposer, shift_r_set_ascii_mode_toggles_no_commit) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Shift_R"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(ctx.set_preedit("ni"));
    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press and release Shift_R
    ac.process_key(0xA1, false, ctx);
    ac.process_key(0xA1, true, ctx);

    // set_ascii_mode toggles but does not commit
    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_TRUE(ctx.committed_text.empty());
}

TEST(AsciiComposer, inline_ascii_binding_preserves_ime_origin) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "inline_ascii";

    cxxime::AsciiComposer composer;
    composer.load_config(config);
    cxxime::Context context;
    ASSERT_TRUE(context.start_composition(cxxime::CompositionScheme::kPinyin, "ni", 1));

    composer.process_key(VK_LSHIFT, false, context);
    composer.process_key(VK_LSHIFT, true, context);

    ASSERT_TRUE(composer.is_ascii_mode());
    ASSERT_TRUE(composer.is_temporary_ascii());
    ASSERT_EQ(context.composition_scheme(), cxxime::CompositionScheme::kInlineAscii);
    ASSERT_TRUE(context.composition_origin().has_value());
    ASSERT_EQ(context.composition_origin()->scheme, cxxime::CompositionScheme::kPinyin);
    ASSERT_EQ(context.composition_origin()->input, "ni");
    ASSERT_EQ(context.composition_origin()->cursor, static_cast<size_t>(1));
}

TEST(AsciiComposer, inline_ascii_binding_keeps_active_inline_mode_stable) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "inline_ascii";

    cxxime::AsciiComposer composer;
    composer.load_config(config);
    cxxime::Context context;
    ASSERT_TRUE(
        context.start_composition(cxxime::CompositionScheme::kInlineAscii, "c++", 3));
    composer.set_ascii_mode(true);

    composer.process_key(VK_LSHIFT, false, context);
    composer.process_key(VK_LSHIFT, true, context);

    ASSERT_TRUE(composer.is_ascii_mode());
    ASSERT_TRUE(!composer.is_temporary_ascii());
    ASSERT_EQ(context.composition_scheme(), cxxime::CompositionScheme::kInlineAscii);
    ASSERT_EQ(context.active_input(), "c++");
}

TEST(AsciiComposer, inline_ascii_origin_survives_cursor_navigation) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "inline_ascii";

    cxxime::AsciiComposer composer;
    composer.load_config(config);
    cxxime::Context context;
    ASSERT_TRUE(context.start_composition(cxxime::CompositionScheme::kPinyin, "ni", 2));

    composer.process_key(VK_LSHIFT, false, context);
    composer.process_key(VK_LSHIFT, true, context);
    cxxime::KeyEvent left;
    left.keycode = VK_LEFT;
    ASSERT_EQ(composer.process_inline_ascii_composition(left, context, true),
              cxxime::InlineAsciiResult::kAccepted);

    ASSERT_EQ(context.composition_scheme(), cxxime::CompositionScheme::kInlineAscii);
    ASSERT_TRUE(context.composition_origin().has_value());
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(1));
}

TEST(AsciiComposer, set_ascii_mode_binding_does_not_create_origin) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_R"] = "set_ascii_mode";

    cxxime::AsciiComposer composer;
    composer.load_config(config);
    cxxime::Context context;
    ASSERT_TRUE(context.start_composition(cxxime::CompositionScheme::kSymbol, "\\bd", 2));

    composer.process_key(VK_RSHIFT, false, context);
    composer.process_key(VK_RSHIFT, true, context);

    ASSERT_TRUE(composer.is_ascii_mode());
    ASSERT_TRUE(!composer.is_temporary_ascii());
    ASSERT_EQ(context.composition_scheme(), cxxime::CompositionScheme::kInlineAscii);
    ASSERT_TRUE(!context.composition_origin().has_value());
    ASSERT_EQ(context.active_input(), "\\bd");
    ASSERT_EQ(context.preedit_cursor(), static_cast<size_t>(2));
}

TEST(AsciiComposer, shift_no_binding_does_nothing) {
    cxxime::Config config;
    // No Shift bindings

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Press and release Shift_L
    ac.process_key(0xA0, false, ctx);
    ac.process_key(0xA0, true, ctx);

    ASSERT_TRUE(!ac.is_ascii_mode());
}

TEST(AsciiComposer, shift_toggle_back_to_chinese) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(!ac.is_ascii_mode());

    // First Shift: Chinese -> English (code toggles)
    ac.process_key(0xA0, false, ctx);
    ac.process_key(0xA0, true, ctx);
    ASSERT_TRUE(ac.is_ascii_mode());

    // Second Shift: English -> Chinese (code toggles back)
    cxxime::Context ctx2;
    ac.process_key(0xA0, false, ctx2);
    ac.process_key(0xA0, true, ctx2);
    ASSERT_TRUE(!ac.is_ascii_mode());
}

TEST(AsciiComposer, set_ascii_mode_is_one_way) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // First Shift: Chinese -> English
    ac.process_key(0xA0, false, ctx);
    ac.process_key(0xA0, true, ctx);
    ASSERT_TRUE(ac.is_ascii_mode());

    // Second Shift: stays English (set_ascii_mode is one-way)
    cxxime::Context ctx2;
    ac.process_key(0xA0, false, ctx2);
    ac.process_key(0xA0, true, ctx2);
    ASSERT_TRUE(ac.is_ascii_mode());
}

TEST(AsciiComposer, alt_l_supported) {
    cxxime::Config config;
    config.ascii_switch_key["Alt_L"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press and release Alt_L
    ac.process_key(0xA4, false, ctx);
    ac.process_key(0xA4, true, ctx);

    ASSERT_TRUE(ac.is_ascii_mode());
}

TEST(AsciiComposer, super_l_supported) {
    cxxime::Config config;
    config.ascii_switch_key["Super_L"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press and release Super_L
    ac.process_key(0x5B, false, ctx);
    ac.process_key(0x5B, true, ctx);

    ASSERT_TRUE(ac.is_ascii_mode());
}

TEST(AsciiComposer, capslock_downgrade) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "inline_ascii";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // CapsLock ON → toggles (downgraded from inline_ascii to clear)
    ASSERT_TRUE(!ac.is_ascii_mode());
    ac.process_key(0x14, false, ctx, true);   // VK_CAPITAL down, CapsLock ON
    ASSERT_TRUE(ac.is_ascii_mode());
    // CapsLock OFF restores the mode from before CapsLock was turned on.
    ac.process_key(0x14, false, ctx, false);  // VK_CAPITAL down, CapsLock OFF
    ASSERT_TRUE(!ac.is_ascii_mode());
}

TEST(AsciiComposer, capslock_clear_resets_pinyin) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Simulate composing state
    ASSERT_TRUE(ctx.set_preedit("nihao"));
    ASSERT_TRUE(ctx.is_composing());

    // Press CapsLock — should clear pinyin and toggle ascii_mode
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON

    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_TRUE(ctx.active_input().empty());
    ASSERT_TRUE(!ctx.is_composing());
}

TEST(Engine, capslock_clear_off_restores_chinese_without_ascii_letter_intercept) {
    std::string dict_path = make_temp_path("test_caps_clear_restore_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"a", "a", 100}});

    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::Engine engine;
    engine.initialize(dict_path);
    engine.reload_config(config);
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent caps_on;
    caps_on.keycode = 0x14;  // VK_CAPITAL
    caps_on.is_key_up = false;
    caps_on.set_caps_lock();
    engine.process_key(caps_on);

    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent caps_off;
    caps_off.keycode = 0x14;
    caps_off.is_key_up = false;
    engine.process_key(caps_off);

    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent letter;
    letter.keycode = 'A';
    letter.is_key_up = false;
    letter.set_caps_lock();

    auto result = engine.process_key(letter);
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().active_input(), "a");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(Engine, capslock_letter_commits_directly) {
    cxxime::Engine engine;
    // No dict needed — CapsLock intercept happens before translation

    // Simulate CapsLock ON by setting the modifier bit
    cxxime::KeyEvent event;
    event.keycode = 'A';
    event.is_key_up = false;
    event.modifiers = 0x08;  // CapsLock bit

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    // CapsLock alone → uppercase
    ASSERT_EQ(engine.get_commit_text(), "A");
}

TEST(Engine, capslock_shift_letter_commits_lowercase) {
    cxxime::Engine engine;

    cxxime::KeyEvent event;
    event.keycode = 'A';
    event.is_key_up = false;
    event.modifiers = 0x09;  // CapsLock (0x08) + Shift (0x01)

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    // Shift+CapsLock → lowercase
    ASSERT_EQ(engine.get_commit_text(), "a");
}

TEST(Engine, ascii_mode_capslock_uppercase) {
    // Bug: Shift → English mode, CapsLock ON, type letter → should be uppercase
    std::string dict_path = make_temp_path("test_ascii_caps_dict.bin");
    std::string spellings_path = make_temp_path("test_ascii_caps_spellings.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"a", "啊", 100}});
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {{"a", "a", 0, 0.0f}}));

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.ascii_composer().set_ascii_mode(true);

    cxxime::KeyEvent event;
    event.keycode = 'N';
    event.is_key_up = false;
    event.modifiers = 0x08;  // CapsLock ON

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "N");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}
