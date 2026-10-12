// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_test_support.h"

namespace {

class ShortcutEngine {
public:
    cxxime::Engine engine;
    const std::string path = make_temp_path("cxxime_shortcut_test.dict");

    explicit ShortcutEngine(cxxime::Config config = {}) {
        ASSERT_TRUE(cxxime::Dict::create_test_dict(path, {{"ni", "你", 100}}));
        ASSERT_TRUE(test::initialize_engine(engine, path, config));
    }
    ~ShortcutEngine() {
        engine.finalize();
        ASSERT_TRUE(DeleteFileA(path.c_str()));
    }
};

} // namespace

TEST(EngineShortcut, modified_commands_preserve_all_composition_schemes) {
    const uint32_t keys[] = {VK_RETURN, VK_TAB,    VK_BACK,  VK_DELETE, VK_ESCAPE, VK_HOME,
                             VK_END,    VK_LEFT,   VK_RIGHT, VK_UP,     VK_DOWN,   VK_PRIOR,
                             VK_NEXT,   VK_INSERT, VK_F1,    VK_F12};
    const uint32_t modifiers[] = {
        cxxime::kKeyModifierShift,
        cxxime::kKeyModifierControl,
        cxxime::kKeyModifierAlt,
        cxxime::kKeyModifierWin,
        cxxime::kKeyModifierControl | cxxime::kKeyModifierShift,
        cxxime::kKeyModifierControl | cxxime::kKeyModifierAlt,
    };
    const cxxime::CompositionScheme schemes[] = {
        cxxime::CompositionScheme::kPinyin,
        cxxime::CompositionScheme::kWubi,
        cxxime::CompositionScheme::kSymbol,
        cxxime::CompositionScheme::kInlineAscii,
    };
    ShortcutEngine fixture;
    for (auto scheme : schemes) {
        auto& context = fixture.engine.context();
        fixture.engine.clear_composition();
        const std::string input = scheme == cxxime::CompositionScheme::kSymbol ? "\\sx" : "ni";
        ASSERT_TRUE(context.start_composition(scheme, input, 1));
        cxxime::CandidatePage page;
        page.candidates = {{"你", "", 100}, {"尼", "", 90}};
        page.highlighted = 1;
        context.update_candidates(std::move(page));
        const auto revision = context.preedit_revision();
        for (uint32_t modifier : modifiers) {
            for (uint32_t key : keys) {
                // Include auto-repeat and release, checking every intermediate response.
                for (bool key_up : {false, false, true}) {
                    ASSERT_EQ(fixture.engine.process_key({key, modifier, key_up}),
                              cxxime::ProcessResult::REJECTED)
                        << key << modifier;
                    ASSERT_EQ(context.active_input(), input);
                    ASSERT_EQ(context.preedit_cursor(), 1u);
                    ASSERT_EQ(context.preedit_revision(), revision);
                    ASSERT_EQ(context.composition_scheme(), scheme);
                    ASSERT_EQ(context.candidate_count(), 2);
                    ASSERT_EQ(context.highlighted(), 1);
                    ASSERT_EQ(context.candidate(0)->text, "你");
                    ASSERT_EQ(context.candidate(1)->text, "尼");
                    ASSERT_TRUE(context.committed_text.empty());
                }
            }
        }
    }
}

TEST(EngineShortcut, host_shortcuts_do_not_start_text_in_idle_chinese_or_english_mode) {
    ShortcutEngine fixture;
    for (bool ascii_mode : {false, true}) {
        fixture.engine.ascii_composer().set_ascii_mode(ascii_mode);
        for (uint32_t modifiers :
             {cxxime::kKeyModifierControl, cxxime::kKeyModifierAlt, cxxime::kKeyModifierWin}) {
            for (uint32_t key : {uint32_t('A'), uint32_t('1'), uint32_t(VK_SPACE),
                                 uint32_t(VK_RETURN), uint32_t(VK_LEFT)}) {
                ASSERT_EQ(fixture.engine.process_key({key, modifiers, false}),
                          cxxime::ProcessResult::REJECTED);
                ASSERT_TRUE(!fixture.engine.context().is_composing());
                ASSERT_TRUE(fixture.engine.context().committed_text.empty());
                ASSERT_EQ(fixture.engine.ascii_composer().is_ascii_mode(), ascii_mode);
            }
        }
    }
}

TEST(EngineShortcut, ime_bindings_and_plain_enter_keep_priority) {
    cxxime::Config config;
    config.input_mode_switch_shortcut = {cxxime::kKeyModifierControl, VK_SPACE};
    ShortcutEngine fixture(config);
    auto& engine = fixture.engine;
    ASSERT_EQ(engine.process_key({VK_SPACE, cxxime::kKeyModifierShift, false}),
              cxxime::ProcessResult::TOGGLE_SHAPE);
    ASSERT_EQ(engine.process_key({VK_OEM_PERIOD, cxxime::kKeyModifierControl, false}),
              cxxime::ProcessResult::TOGGLE_PUNCT);
    ASSERT_EQ(engine.process_key({VK_SPACE, cxxime::kKeyModifierControl, false}),
              cxxime::ProcessResult::SWITCH_INPUT_MODE);
    ASSERT_EQ(engine.process_key({VK_SPACE, cxxime::kKeyModifierControl, false}),
              cxxime::ProcessResult::INPUT_MODE_SHORTCUT_HANDLED);
    ASSERT_EQ(engine.process_key({VK_SPACE, 0, true}),
              cxxime::ProcessResult::INPUT_MODE_SHORTCUT_HANDLED);
    ASSERT_EQ(engine.process_key(
                  {VK_SPACE, cxxime::kKeyModifierControl | cxxime::kKeyModifierWin, false}),
              cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(engine.context().set_preedit("ni"));
    ASSERT_EQ(engine.process_key({VK_RETURN, 0, false}), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "ni");
}

TEST(EngineShortcut, modified_command_cancels_single_modifier_toggle) {
    struct Binding {
        const char* name;
        uint32_t key;
        uint32_t modifier;
    };
    const Binding bindings[] = {
        {"Shift_L", VK_LSHIFT, cxxime::kKeyModifierShift},
        {"Control_L", VK_LCONTROL, cxxime::kKeyModifierControl},
        {"Alt_L", VK_LMENU, cxxime::kKeyModifierAlt},
        {"Super_L", VK_LWIN, cxxime::kKeyModifierWin},
    };
    for (const auto& binding : bindings) {
        cxxime::Config config;
        config.ascii_switch_key[binding.name] = "code";
        ShortcutEngine fixture(config);
        auto& engine = fixture.engine;
        ASSERT_TRUE(engine.context().set_preedit("ni"));
        engine.process_key({binding.key, binding.modifier, false});
        ASSERT_EQ(engine.process_key({VK_RETURN, binding.modifier, false}),
                  cxxime::ProcessResult::REJECTED);
        engine.process_key({binding.key, 0, true});
        ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());
        ASSERT_EQ(engine.context().active_input(), "ni");
        ASSERT_TRUE(engine.context().committed_text.empty());
        // A subsequent standalone tap must still work: no stuck modifier state.
        engine.process_key({binding.key, binding.modifier, false});
        ASSERT_EQ(engine.process_key({binding.key, 0, true}), cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(engine.get_commit_text(), "ni");
        ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());
    }
}
