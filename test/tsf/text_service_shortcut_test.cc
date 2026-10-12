// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include <cxxime/key_event.h>

#include "text_service_test_support.h"

namespace {

void set_modifiers(uint32_t modifiers) {
    BYTE state[256] = {};
    state[VK_SHIFT] = (modifiers & cxxime::kKeyModifierShift) ? 0x80 : 0;
    state[VK_CONTROL] = (modifiers & cxxime::kKeyModifierControl) ? 0x80 : 0;
    state[VK_MENU] = (modifiers & cxxime::kKeyModifierAlt) ? 0x80 : 0;
    state[VK_LWIN] = (modifiers & cxxime::kKeyModifierWin) ? 0x80 : 0;
    ASSERT_TRUE(SetKeyboardState(state));
}

// Exercise native TSF callbacks and real IPC with controlled server responses.
// EngineShortcut separately verifies which responses actual engine input produces.
class ShortcutHost {
public:
    tsf_test::KeyboardState keyboard;
    cxxime::IPCResponse response = {};
    int key_calls = 0;
    uint32_t received_modifiers = 0;
    bool received_key_up = false;
    tsf_test::Fixture fixture;

    explicit ShortcutHost(bool composing) {
        keyboard.caps(false);
        fixture.view.foreground_window = true;
        fixture.host.active_view = &fixture.view;
        response.status = cxxime::IPCStatus::OK;
        response.ime_status.set_chinese_mode(true);
        if (composing) {
            BOOL eaten = FALSE;
            ASSERT_TRUE(
                TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
            ASSERT_EQ(eaten, TRUE);
            strcpy_s(response.preedit, "n");
            response.preedit_cursor = 1;
            response.composing = true;
        }
        response.key_disposition = cxxime::KeyDisposition::PassToHost;
        fixture.server.server.set_handler([this](const cxxime::IPCRequest& request) {
            if (request.command == cxxime::IPCCommand::PROCESS_KEY) {
                ++key_calls;
                received_modifiers = request.modifiers;
                received_key_up = request.is_key_up != 0;
                return response;
            }
            return cxxime::IPCResponse{};
        });
    }

    BOOL down(uint32_t key, bool test_callback) {
        BOOL eaten = FALSE;
        if (test_callback) {
            ASSERT_EQ(fixture.service.OnTestKeyDown(&fixture.host, key, 0, &eaten), S_OK);
            if (eaten) {
                ASSERT_EQ(fixture.service.OnKeyDown(&fixture.host, key, 0, &eaten), S_OK);
            }
        } else {
            ASSERT_EQ(fixture.service.OnKeyDown(&fixture.host, key, 0, &eaten), S_OK);
        }
        return eaten;
    }

    void release_to_host(uint32_t key, bool test_callback) {
        BOOL eaten = TRUE;
        if (test_callback) {
            ASSERT_EQ(fixture.service.OnTestKeyUp(&fixture.host, key, 0, &eaten), S_OK);
            ASSERT_EQ(eaten, FALSE);
        }
        ASSERT_EQ(fixture.service.OnKeyUp(&fixture.host, key, 0, &eaten), S_OK);
        ASSERT_EQ(eaten, FALSE);
    }
};

} // namespace

TEST(TextServiceShortcut, host_commands_preserve_composition_at_every_callback) {
    const cxxime::KeyEvent commands[] = {
        {VK_RETURN, cxxime::kKeyModifierShift}, {VK_TAB, cxxime::kKeyModifierShift},
        {VK_LEFT, cxxime::kKeyModifierShift},   {VK_BACK, cxxime::kKeyModifierControl},
        {'C', cxxime::kKeyModifierControl},     {VK_F4, cxxime::kKeyModifierAlt},
        {'D', cxxime::kKeyModifierWin},
    };
    for (bool composing : {false, true}) {
        for (bool test_callback : {false, true}) {
            ShortcutHost host(composing);
            const auto text = host.fixture.host.range.text;
            const auto generation = TextServiceTestPeer::generation(host.fixture.service);
            const int writes = host.fixture.host.range.writes;
            const int starts = host.fixture.host.starts;
            const int ends = host.fixture.host.composition.ends;
            for (auto command : commands) {
                set_modifiers(command.modifiers);
                for (int repeat = 0; repeat < 2; ++repeat) {
                    ASSERT_EQ(host.down(command.keycode, test_callback), FALSE);
                    ASSERT_EQ(host.received_modifiers, command.modifiers);
                    ASSERT_EQ(host.fixture.host.range.writes, writes);
                    ASSERT_EQ(host.fixture.host.starts, starts);
                    ASSERT_EQ(host.fixture.host.composition.ends, ends);
                    ASSERT_EQ(host.fixture.host.range.text, text);
                    if (composing) {
                        ASSERT_EQ(TextServiceTestPeer::generation(host.fixture.service),
                                  generation);
                    } else {
                        ASSERT_TRUE(TextServiceTestPeer::empty(host.fixture.service));
                    }
                }
                const int calls = host.key_calls;
                // Modifier released before the command key: release follows down ownership.
                set_modifiers(0);
                host.release_to_host(command.keycode, test_callback);
                ASSERT_EQ(host.key_calls, calls);
                ASSERT_EQ(host.fixture.host.range.writes, writes);
                ASSERT_EQ(host.fixture.host.composition.ends, ends);
            }
        }
    }
}

TEST(TextServiceShortcut, authoritative_clear_is_applied_before_forwarding) {
    ShortcutHost host(true);
    host.response.status = cxxime::IPCStatus::OK;
    host.response.composing = false;
    host.response.presentation_changed = 1;
    host.response.preedit[0] = '\0';
    set_modifiers(cxxime::kKeyModifierShift);
    ASSERT_EQ(host.down(VK_RETURN, true), FALSE);
    ASSERT_EQ(host.fixture.host.composition.ends, 1);
    ASSERT_TRUE(host.fixture.host.range.text.empty());
    ASSERT_TRUE(TextServiceTestPeer::empty(host.fixture.service));
    host.release_to_host(VK_RETURN, true);
}

TEST(TextServiceShortcut, explicit_ime_shortcut_and_commit_are_not_overridden) {
    for (bool commit : {false, true}) {
        ShortcutHost host(true);
        host.response.status = cxxime::IPCStatus::OK;
        host.response.composing = false;
        host.response.preedit[0] = '\0';
        host.response.key_disposition = cxxime::KeyDisposition::Handled;
        host.response.presentation_changed = 1;
        if (commit) {
            strcpy_s(host.response.commit_text, "n");
        }
        set_modifiers(cxxime::kKeyModifierControl);
        ASSERT_EQ(host.down(VK_SPACE, true), TRUE);
        ASSERT_EQ(host.key_calls, 1);
        ASSERT_EQ(host.fixture.host.composition.ends, 1);
        ASSERT_EQ(host.fixture.host.range.text, commit ? L"n" : L"");
    }
}

TEST(TextServiceShortcut, modifier_releases_still_reach_engine) {
    const cxxime::KeyEvent modifiers[] = {
        {VK_LSHIFT, cxxime::kKeyModifierShift},
        {VK_CONTROL, cxxime::kKeyModifierControl},
        {VK_MENU, cxxime::kKeyModifierAlt},
        {VK_LWIN, cxxime::kKeyModifierWin},
    };
    for (bool composing : {false, true}) {
        ShortcutHost host(composing);
        for (auto modifier : modifiers) {
            set_modifiers(modifier.modifiers);
            ASSERT_EQ(host.down(modifier.keycode, true), FALSE);
            const int calls = host.key_calls;
            set_modifiers(0);
            BOOL eaten = FALSE;
            ASSERT_EQ(
                host.fixture.service.OnTestKeyUp(&host.fixture.host, modifier.keycode, 0, &eaten),
                S_OK);
            ASSERT_EQ(host.key_calls, calls + 1);
            ASSERT_TRUE(host.received_key_up);
            ASSERT_EQ(eaten, FALSE);
        }
    }
}

TEST(TextServiceShortcut, status_only_handling_does_not_reapply_composition) {
    ShortcutHost host(true);
    host.response.key_disposition = cxxime::KeyDisposition::Handled;
    host.response.ime_status.set_full_shape(true);
    const int writes = host.fixture.host.range.writes;
    const auto generation = TextServiceTestPeer::generation(host.fixture.service);
    set_modifiers(cxxime::kKeyModifierShift);
    ASSERT_EQ(host.down(VK_SPACE, true), TRUE);
    ASSERT_EQ(host.key_calls, 1);
    ASSERT_TRUE(TextServiceTestPeer::status(host.fixture.service).full_shape());
    ASSERT_EQ(host.fixture.host.range.writes, writes);
    ASSERT_EQ(host.fixture.host.composition.ends, 0);
    ASSERT_EQ(TextServiceTestPeer::generation(host.fixture.service), generation);
}

TEST(TextServiceShortcut, failed_or_unspecified_results_never_apply_partial_text) {
    for (bool failed : {false, true}) {
        ShortcutHost host(true);
        host.response.status =
            failed ? cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED : cxxime::IPCStatus::OK;
        host.response.key_disposition =
            failed ? cxxime::KeyDisposition::Handled : cxxime::KeyDisposition::Unspecified;
        host.response.presentation_changed = 1;
        strcpy_s(host.response.commit_text, "partial");
        const auto text = host.fixture.host.range.text;
        const int writes = host.fixture.host.range.writes;
        ASSERT_EQ(host.down(VK_RETURN, true), FALSE);
        ASSERT_EQ(host.fixture.host.range.text, text);
        ASSERT_EQ(host.fixture.host.range.writes, writes);
        ASSERT_EQ(host.fixture.host.composition.ends, 0);
    }
}

TEST(TextServiceShortcut, ownership_is_not_inferred_from_key_or_snapshot) {
    ShortcutHost host(true);
    const int writes = host.fixture.host.range.writes;
    // Even a plain text key can be passed by the server without clearing its snapshot.
    set_modifiers(0);
    ASSERT_EQ(host.down('A', false), FALSE);
    ASSERT_EQ(host.fixture.host.range.writes, writes);
    host.release_to_host('A', false);
    host.response.key_disposition = cxxime::KeyDisposition::Handled;
    // An explicit server binding can own a key that the default host policy would pass.
    set_modifiers(cxxime::kKeyModifierShift);
    ASSERT_EQ(host.down(VK_RETURN, false), TRUE);
    ASSERT_EQ(host.fixture.host.range.writes, writes);
    ASSERT_EQ(host.fixture.host.composition.ends, 0);
}

TEST(TextServiceShortcut, explicit_results_preserve_host_edit_failure_ownership) {
    for (bool test_callback : {false, true}) {
        for (bool partial_write : {false, true}) {
            ShortcutHost host(false);
            host.response.key_disposition = cxxime::KeyDisposition::Handled;
            host.response.presentation_changed = 1;
            host.response.composing = true;
            strcpy_s(host.response.preedit, "n");
            host.response.preedit_cursor = 1;
            host.fixture.host.reject_composition = !partial_write;
            host.fixture.host.fail_selection = partial_write;
            ASSERT_EQ(host.down('N', test_callback), partial_write ? TRUE : FALSE);
            ASSERT_TRUE(TextServiceTestPeer::empty(host.fixture.service));
            ASSERT_TRUE(host.fixture.host.range.text.empty());
            ASSERT_EQ(host.fixture.host.composition.ends, partial_write ? 1 : 0);
            ASSERT_EQ(host.fixture.host.range.writes > 0, partial_write);
        }
        ShortcutHost host(false);
        host.response.key_disposition = cxxime::KeyDisposition::Handled;
        host.response.presentation_changed = 1;
        strcpy_s(host.response.commit_text, "committed");
        host.fixture.host.fail_selection = true;
        ASSERT_EQ(host.down(VK_RETURN, test_callback), TRUE);
        ASSERT_EQ(host.fixture.host.range.text, L"committed");
        ASSERT_EQ(host.fixture.host.range.writes, 1);
    }
}

TEST(TextServiceShortcut, caps_lock_down_and_up_follow_explicit_results) {
    for (auto disposition : {cxxime::KeyDisposition::Unspecified,
                             cxxime::KeyDisposition::PassToHost, cxxime::KeyDisposition::Handled}) {
        for (bool test_callback : {false, true}) {
            ShortcutHost host(false);
            host.response.key_disposition = disposition;
            const BOOL expected = disposition == cxxime::KeyDisposition::Handled ? TRUE : FALSE;
            ASSERT_EQ(host.down(VK_CAPITAL, test_callback), expected);
            BOOL eaten = FALSE;
            if (test_callback) {
                ASSERT_EQ(
                    host.fixture.service.OnTestKeyUp(&host.fixture.host, VK_CAPITAL, 0, &eaten),
                    S_OK);
                ASSERT_EQ(eaten, expected);
            }
            if (!test_callback || eaten) {
                ASSERT_EQ(host.fixture.service.OnKeyUp(&host.fixture.host, VK_CAPITAL, 0, &eaten),
                          S_OK);
                ASSERT_EQ(eaten, expected);
            }
            ASSERT_EQ(host.key_calls, 2);
            ASSERT_EQ(host.fixture.host.range.writes, 0);
        }
    }
}

TEST(TextServiceShortcut, entire_modified_command_reaches_host_without_replaying_preedit) {
    for (bool test_callback : {false, true}) {
        ShortcutHost host(true);
        const int writes = host.fixture.host.range.writes;
        const auto generation = TextServiceTestPeer::generation(host.fixture.service);
        set_modifiers(cxxime::kKeyModifierShift);
        ASSERT_EQ(host.down(VK_SHIFT, test_callback), FALSE);
        ASSERT_EQ(host.fixture.host.range.writes, writes);
        ASSERT_EQ(host.down(VK_RETURN, test_callback), FALSE);
        ASSERT_EQ(host.fixture.host.range.writes, writes);
        host.release_to_host(VK_RETURN, test_callback);
        ASSERT_EQ(host.fixture.host.range.writes, writes);
        set_modifiers(0);
        const int calls = host.key_calls;
        BOOL eaten = TRUE;
        if (test_callback) {
            ASSERT_EQ(host.fixture.service.OnTestKeyUp(&host.fixture.host, VK_SHIFT, 0, &eaten),
                      S_OK);
        } else {
            ASSERT_EQ(host.fixture.service.OnKeyUp(&host.fixture.host, VK_SHIFT, 0, &eaten), S_OK);
        }
        ASSERT_EQ(eaten, FALSE);
        ASSERT_EQ(host.key_calls, calls + 1);
        ASSERT_EQ(host.fixture.host.range.writes, writes);
        ASSERT_EQ(host.fixture.host.composition.ends, 0);
        ASSERT_EQ(TextServiceTestPeer::generation(host.fixture.service), generation);
    }
}
