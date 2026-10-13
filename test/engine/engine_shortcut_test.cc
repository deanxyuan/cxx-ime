// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_test_support.h"

namespace {

class ShortcutEngine {
public:
    cxxime::Engine engine;
    const std::string path = make_temp_path("cxxime_shortcut_test.dict");

    explicit ShortcutEngine(cxxime::Config config = {}, bool with_wubi = false) {
        ASSERT_TRUE(cxxime::Dict::create_test_dict(
            path, {{"ni", "你", 100}, {"ni", "尼", 90}, {"ni", "泥", 80}}));
        ASSERT_TRUE(test::initialize_engine(engine, path, config, {}, with_wubi ? path : ""));
    }
    ~ShortcutEngine() {
        engine.finalize();
        ASSERT_TRUE(DeleteFileA(path.c_str()));
    }
};

} // namespace

TEST(EngineShortcut, text_edits_and_host_commands_have_distinct_ownership_in_each_scheme) {
    constexpr auto ctrl = cxxime::kKeyModifierControl;
    constexpr auto shift = cxxime::kKeyModifierShift;
    constexpr auto alt = cxxime::kKeyModifierAlt;
    constexpr auto win = cxxime::kKeyModifierWin;
    struct Command {
        cxxime::KeyEvent event;
        bool text_edit;
    };
    const Command commands[] = {
        {{'A', ctrl}, true},
        {{'C', ctrl}, true},
        {{'X', ctrl}, true},
        {{'V', ctrl}, true},
        {{'Z', ctrl}, true},
        {{'Y', ctrl}, true},
        {{'V', ctrl | shift}, true},
        {{'Z', ctrl | shift}, true},
        {{VK_INSERT, ctrl}, true},
        {{VK_INSERT, shift}, true},
        {{VK_DELETE, shift}, true},
        {{VK_BACK, ctrl}, true},
        {{VK_DELETE, ctrl}, true},
        {{VK_LEFT, ctrl}, true},
        {{VK_RIGHT, ctrl}, true},
        {{VK_UP, ctrl}, true},
        {{VK_DOWN, ctrl}, true},
        {{VK_HOME, ctrl}, true},
        {{VK_END, ctrl}, true},
        {{VK_PRIOR, ctrl}, true},
        {{VK_NEXT, ctrl}, true},
        {{VK_LEFT, ctrl | shift}, true},
        {{VK_RIGHT, ctrl | shift}, true},
        {{VK_UP, ctrl | shift}, true},
        {{VK_DOWN, ctrl | shift}, true},
        {{VK_HOME, ctrl | shift}, true},
        {{VK_END, ctrl | shift}, true},
        {{VK_PRIOR, ctrl | shift}, true},
        {{VK_NEXT, ctrl | shift}, true},
        {{VK_BACK, ctrl | shift}, true},
        {{VK_DELETE, ctrl | shift}, true},
        {{VK_F1, 0}, false},
        {{VK_F5, 0}, false},
        {{VK_F12, shift}, false},
        {{'S', ctrl}, false},
        {{'P', ctrl}, false},
        {{'F', ctrl}, false},
        {{VK_TAB, ctrl}, false},
        {{VK_RETURN, ctrl}, false},
        {{'A', ctrl | shift}, false},
        {{'C', ctrl | shift}, false},
        {{'X', ctrl | shift}, false},
        {{'Y', ctrl | shift}, false},
        {{VK_TAB, alt}, false},
        {{VK_F4, alt}, false},
        {{'D', win}, false},
        {{'A', ctrl | alt}, false},
        {{'V', ctrl | win}, false},
        {{VK_INSERT, 0}, false},
        {{VK_INSERT, ctrl | shift}, false},
        {{VK_TAB, shift}, false},
        {{VK_TAB, ctrl | shift}, false},
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
        for (const auto& command : commands) {
            // Check auto-repeat and release, not just the eventual state.
            for (bool key_up : {false, false, true}) {
                auto event = command.event;
                event.is_key_up = key_up;
                ASSERT_EQ(fixture.engine.process_key(event), command.text_edit && !key_up
                                                                 ? cxxime::ProcessResult::ACCEPTED
                                                                 : cxxime::ProcessResult::REJECTED)
                    << event.keycode << event.modifiers;
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

TEST(EngineShortcut, host_shortcuts_do_not_start_text_in_idle_chinese_or_english_mode) {
    ShortcutEngine fixture;
    for (bool ascii_mode : {false, true}) {
        fixture.engine.ascii_composer().set_ascii_mode(ascii_mode);
        for (uint32_t modifiers :
             {cxxime::kKeyModifierControl, cxxime::kKeyModifierAlt, cxxime::kKeyModifierWin}) {
            for (uint32_t key : {uint32_t('A'), uint32_t('C'), uint32_t('V'), uint32_t('1'),
                                 uint32_t(VK_SPACE), uint32_t(VK_RETURN), uint32_t(VK_LEFT)}) {
                ASSERT_EQ(fixture.engine.process_key({key, modifiers, false}),
                          cxxime::ProcessResult::REJECTED);
                ASSERT_TRUE(!fixture.engine.context().is_composing());
                ASSERT_TRUE(fixture.engine.context().committed_text.empty());
                ASSERT_EQ(fixture.engine.ascii_composer().is_ascii_mode(), ascii_mode);
            }
        }
    }
}

TEST(EngineShortcut, idle_shift_commands_reach_host_without_changing_language) {
    ShortcutEngine fixture;
    for (bool ascii_mode : {false, true}) {
        fixture.engine.ascii_composer().set_ascii_mode(ascii_mode);
        for (uint32_t key :
             {VK_RETURN, VK_TAB, VK_LEFT, VK_BACK, VK_ESCAPE, VK_INSERT, VK_DELETE, VK_F12}) {
            for (bool up : {false, false, true}) {
                ASSERT_EQ(fixture.engine.process_key({key, cxxime::kKeyModifierShift, up}),
                          cxxime::ProcessResult::REJECTED);
                ASSERT_TRUE(!fixture.engine.context().is_composing());
                ASSERT_TRUE(fixture.engine.context().committed_text.empty());
                ASSERT_EQ(fixture.engine.ascii_composer().is_ascii_mode(), ascii_mode);
            }
        }
    }
}

TEST(EngineShortcut, shift_retains_existing_preedit_navigation_commit_and_cancel) {
    ShortcutEngine fixture;
    auto& engine = fixture.engine;
    for (auto scheme :
         {cxxime::CompositionScheme::kPinyin, cxxime::CompositionScheme::kWubi,
          cxxime::CompositionScheme::kSymbol, cxxime::CompositionScheme::kInlineAscii}) {
        engine.clear_composition();
        const std::string input = scheme == cxxime::CompositionScheme::kSymbol ? "\\sx" : "ni";
        ASSERT_TRUE(engine.context().start_composition(scheme, input, input.size()));
        for (uint32_t key : {VK_HOME, VK_RIGHT, VK_END, VK_LEFT}) {
            ASSERT_EQ(engine.process_key({key, cxxime::kKeyModifierShift}),
                      cxxime::ProcessResult::ACCEPTED);
            ASSERT_EQ(engine.context().active_input(), input);
            const size_t beginning = scheme == cxxime::CompositionScheme::kSymbol ? 1 : 0;
            const size_t expected = key == VK_HOME    ? beginning
                                    : key == VK_RIGHT ? beginning + 1
                                    : key == VK_END   ? input.size()
                                                      : input.size() - 1;
            ASSERT_EQ(engine.context().preedit_cursor(), expected);
            ASSERT_TRUE(engine.context().committed_text.empty());
        }
        ASSERT_EQ(engine.process_key({VK_RETURN, cxxime::kKeyModifierShift}),
                  cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(engine.get_commit_text(), input);
        ASSERT_TRUE(!engine.context().is_composing());
        ASSERT_TRUE(engine.context().start_composition(scheme, input, input.size()));
        ASSERT_EQ(engine.process_key({VK_ESCAPE, cxxime::kKeyModifierShift}),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(!engine.context().is_composing());
        ASSERT_TRUE(engine.context().committed_text.empty());
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

TEST(EngineShortcut, explicit_binding_takes_priority_over_reserved_text_edit) {
    cxxime::Config config;
    config.input_mode_switch_shortcut = {cxxime::kKeyModifierControl, 'X'};
    ShortcutEngine fixture(config);
    ASSERT_TRUE(fixture.engine.context().set_preedit("ni"));
    ASSERT_EQ(fixture.engine.process_key({'X', cxxime::kKeyModifierControl}),
              cxxime::ProcessResult::SWITCH_INPUT_MODE);
    ASSERT_TRUE(!fixture.engine.context().is_composing());
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
    ASSERT_EQ(fixture.engine.process_key({'X', cxxime::kKeyModifierControl}),
              cxxime::ProcessResult::INPUT_MODE_SHORTCUT_HANDLED);
    ASSERT_EQ(fixture.engine.process_key({'X', 0, true}),
              cxxime::ProcessResult::INPUT_MODE_SHORTCUT_HANDLED);
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
        const bool text_edit = binding.modifier == cxxime::kKeyModifierShift ||
                               binding.modifier == cxxime::kKeyModifierControl;
        const uint32_t key = binding.modifier == cxxime::kKeyModifierShift     ? VK_INSERT
                             : binding.modifier == cxxime::kKeyModifierControl ? 'C'
                                                                               : VK_F12;
        ASSERT_EQ(engine.process_key({key, binding.modifier, false}),
                  text_edit ? cxxime::ProcessResult::ACCEPTED : cxxime::ProcessResult::REJECTED);
        ASSERT_EQ(engine.process_key({binding.key, binding.modifier, false}),
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

TEST(EngineShortcut, overlapping_modifiers_never_execute_single_key_bindings) {
    struct Modifier {
        const char* binding;
        uint32_t key;
        uint32_t mask;
    };
    const Modifier modifiers[] = {
        {"Shift_L", VK_LSHIFT, cxxime::kKeyModifierShift},
        {"Shift_R", VK_RSHIFT, cxxime::kKeyModifierShift},
        {"Control_L", VK_LCONTROL, cxxime::kKeyModifierControl},
        {"Control_R", VK_RCONTROL, cxxime::kKeyModifierControl},
        {"Alt_L", VK_LMENU, cxxime::kKeyModifierAlt},
        {"Alt_R", VK_RMENU, cxxime::kKeyModifierAlt},
        {"Super_L", VK_LWIN, cxxime::kKeyModifierWin},
        {"Super_R", VK_RWIN, cxxime::kKeyModifierWin},
    };
    cxxime::Config config;
    for (const auto& modifier : modifiers) {
        config.ascii_switch_key[modifier.binding] = "code";
    }
    ShortcutEngine fixture(config);
    auto& engine = fixture.engine;
    // Idle Chinese, idle English, and active composition have different consequences.
    for (int state : {0, 1, 2}) {
        for (const auto& first : modifiers) {
            for (const auto& second : modifiers) {
                if (first.key == second.key) {
                    continue;
                }
                for (bool first_released_first : {false, true}) {
                    engine.clear();
                    engine.ascii_composer().set_ascii_mode(state == 1);
                    const std::string input = state == 2 ? "ni" : "";
                    if (state == 2) {
                        ASSERT_TRUE(engine.context().set_preedit(input));
                    }
                    const auto revision = engine.context().preedit_revision();
                    auto unchanged = [&](uint32_t key, uint32_t mask, bool up = false) {
                        ASSERT_EQ(engine.process_key({key, mask, up}),
                                  cxxime::ProcessResult::REJECTED)
                            << key << mask;
                        ASSERT_EQ(engine.ascii_composer().is_ascii_mode(), state == 1);
                        ASSERT_EQ(engine.context().active_input(), input);
                        ASSERT_EQ(engine.context().preedit_revision(), revision);
                        ASSERT_TRUE(engine.context().committed_text.empty());
                    };
                    unchanged(first.key, first.mask);
                    unchanged(second.key, first.mask | second.mask);
                    unchanged(second.key, first.mask | second.mask);
                    const auto& released = first_released_first ? first : second;
                    const auto& held = first_released_first ? second : first;
                    unchanged(released.key, held.mask, true);
                    // A repeat while the other key remains held cannot re-arm a tap.
                    unchanged(held.key, held.mask);
                    unchanged(held.key, 0, true);
                    // Once everything is released, a fresh standalone tap still works once.
                    unchanged(first.key, first.mask);
                    unchanged(first.key, first.mask);
                    ASSERT_EQ(engine.process_key({first.key, 0, true}),
                              state == 2 ? cxxime::ProcessResult::COMMITTED
                                         : cxxime::ProcessResult::ACCEPTED);
                    ASSERT_EQ(engine.get_commit_text(), input);
                    ASSERT_EQ(engine.ascii_composer().is_ascii_mode(), state != 1);
                }
            }
        }
    }
}

TEST(EngineShortcut, modifier_snapshots_protect_taps_when_other_key_down_was_not_delivered) {
    cxxime::Config config;
    config.ascii_switch_key["Shift"] = "code";
    ShortcutEngine fixture(config);
    auto& engine = fixture.engine;
    for (uint32_t other :
         {cxxime::kKeyModifierControl, cxxime::kKeyModifierAlt, cxxime::kKeyModifierWin}) {
        for (bool shift_first : {false, true}) {
            engine.clear();
            ASSERT_TRUE(engine.context().set_preedit("ni"));
            const uint32_t other_key = other == cxxime::kKeyModifierControl ? VK_CONTROL
                                       : other == cxxime::kKeyModifierAlt   ? VK_MENU
                                                                            : VK_LWIN;
            const cxxime::KeyEvent events[] = {
                {VK_SHIFT, other | cxxime::kKeyModifierShift},
                {shift_first ? uint32_t(VK_SHIFT) : other_key,
                 shift_first ? other : cxxime::kKeyModifierShift, true},
                {shift_first ? other_key : uint32_t(VK_SHIFT), 0, true},
            };
            for (const auto& event : events) {
                ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::REJECTED);
                ASSERT_EQ(engine.context().active_input(), "ni");
                ASSERT_TRUE(engine.context().committed_text.empty());
                ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());
            }
        }
    }
}

TEST(EngineShortcut, candidate_navigation_keeps_ownership_without_candidates) {
    ShortcutEngine fixture({}, true);
    auto& engine = fixture.engine;
    for (auto mode :
         {cxxime::InputMode::PINYIN, cxxime::InputMode::WUBI, cxxime::InputMode::MIXED}) {
        engine.switch_mode(mode);
        for (auto scheme : {cxxime::CompositionScheme::kPinyin, cxxime::CompositionScheme::kSymbol,
                            cxxime::CompositionScheme::kInlineAscii}) {
            engine.clear_composition();
            auto& context = engine.context();
            const auto active_scheme =
                scheme == cxxime::CompositionScheme::kPinyin && mode == cxxime::InputMode::WUBI
                    ? cxxime::CompositionScheme::kWubi
                    : scheme;
            const std::string input = scheme == cxxime::CompositionScheme::kSymbol ? "\\zz" : "zz";
            ASSERT_TRUE(context.start_composition(active_scheme, input, input.size()));
            ASSERT_EQ(context.candidate_count(), 0);
            const auto revision = context.preedit_revision();
            for (uint32_t modifiers : {0u, cxxime::kKeyModifierShift}) {
                for (uint32_t key : {VK_UP, VK_DOWN, VK_PRIOR, VK_NEXT}) {
                    for (bool up : {false, false, true}) {
                        ASSERT_EQ(engine.process_key({key, modifiers, up}),
                                  up ? cxxime::ProcessResult::REJECTED
                                     : cxxime::ProcessResult::ACCEPTED)
                            << key << modifiers << static_cast<int>(mode);
                        ASSERT_EQ(context.active_input(), input);
                        ASSERT_EQ(context.preedit_cursor(), input.size());
                        ASSERT_EQ(context.preedit_revision(), revision);
                        ASSERT_EQ(context.composition_scheme(), active_scheme);
                        ASSERT_EQ(context.candidate_count(), 0);
                        ASSERT_EQ(context.page_index(), 0);
                        ASSERT_TRUE(context.committed_text.empty());
                    }
                }
            }
        }
    }
    engine.clear_composition();
    for (bool ascii : {false, true}) {
        engine.ascii_composer().set_ascii_mode(ascii);
        for (uint32_t modifiers : {0u, cxxime::kKeyModifierShift}) {
            for (uint32_t key : {VK_UP, VK_DOWN, VK_PRIOR, VK_NEXT}) {
                ASSERT_EQ(engine.process_key({key, modifiers}), cxxime::ProcessResult::REJECTED);
                ASSERT_TRUE(!engine.context().is_composing());
            }
        }
    }
}

TEST(EngineShortcut, candidate_page_boundaries_do_not_commit_or_become_literal_characters) {
    cxxime::Config config;
    config.page_size = 1;
    ShortcutEngine fixture(config, true);
    auto& engine = fixture.engine;
    for (auto mode :
         {cxxime::InputMode::PINYIN, cxxime::InputMode::WUBI, cxxime::InputMode::MIXED}) {
        engine.clear_composition();
        engine.switch_mode(mode);
        type_code(engine, "ni");
        const auto revision = engine.context().preedit_revision();
        auto navigate = [&](uint32_t key, uint32_t modifiers, int page) {
            ASSERT_EQ(engine.process_key({key, modifiers}), cxxime::ProcessResult::ACCEPTED);
            ASSERT_EQ(engine.context().page_index(), page);
            ASSERT_EQ(engine.context().active_input(), "ni");
            ASSERT_EQ(engine.context().preedit_revision(), revision);
            ASSERT_TRUE(engine.context().candidate_count() > 0);
            ASSERT_TRUE(engine.context().committed_text.empty());
            ASSERT_EQ(engine.process_key({key, modifiers, true}), cxxime::ProcessResult::REJECTED);
        };
        navigate(VK_PRIOR, 0, 0);
        navigate(VK_OEM_MINUS, 0, 0);
        navigate(VK_NEXT, cxxime::kKeyModifierControl, 0);
        navigate(VK_NEXT, 0, 1);
        navigate(VK_NEXT, cxxime::kKeyModifierShift, 2);
        navigate(VK_NEXT, 0, 2);
        navigate(VK_OEM_PLUS, 0, 2);
        navigate(VK_PRIOR, cxxime::kKeyModifierShift, 1);
        navigate(VK_OEM_MINUS, 0, 0);
        navigate(VK_OEM_PLUS, 0, 1);
        // Invalidated candidates must not resurrect an older page of the same composition.
        engine.context().clear_translation();
        ASSERT_EQ(engine.process_key({VK_PRIOR, 0}), cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(engine.context().candidate_count(), 0);
        ASSERT_EQ(engine.context().active_input(), "ni");
        ASSERT_TRUE(engine.context().committed_text.empty());
    }
}

TEST(EngineShortcut, printable_page_keys_without_candidates_extend_the_preedit) {
    ShortcutEngine fixture;
    auto& engine = fixture.engine;
    for (auto scheme : {cxxime::CompositionScheme::kPinyin, cxxime::CompositionScheme::kSymbol,
                        cxxime::CompositionScheme::kInlineAscii}) {
        for (uint32_t key : {VK_OEM_MINUS, VK_OEM_PLUS, VK_SUBTRACT, VK_ADD}) {
            engine.clear_composition();
            const std::string input = scheme == cxxime::CompositionScheme::kSymbol ? "\\zz" : "zz";
            ASSERT_TRUE(engine.context().start_composition(scheme, input, input.size()));
            ASSERT_EQ(engine.process_key({key, 0}), cxxime::ProcessResult::ACCEPTED);
            const char appended = key == VK_OEM_PLUS ? '=' : key == VK_ADD ? '+' : '-';
            ASSERT_EQ(engine.context().active_input(), input + appended);
            ASSERT_EQ(engine.context().composition_scheme(),
                      cxxime::CompositionScheme::kInlineAscii);
            ASSERT_TRUE(engine.context().committed_text.empty());
        }
    }
}
