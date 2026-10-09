// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "status_test_support.h"

using namespace status_test;

TEST(TextServiceStatusRecovery, reconnect_does_not_restore_caps_overlay_as_base_english_mode) {
    for (bool focus_failed : {false, true}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        fixture.server_chinese = true;
        fixture.server_caps = true;
        fixture.focus();
        ASSERT_TRUE(fixture.caps());
        ASSERT_TRUE(!TextServiceTestPeer::status(fixture.service).chinese_mode());

        // A failed focus refresh invalidates display state, not the cached Caps overlay.
        if (focus_failed) {
            fixture.reject_focus = true;
            fixture.focus();
        }
        // A restarted server has default Chinese state and has not observed CapsLock yet.
        fixture.server_caps = false;
        keyboard.caps(true);
        ASSERT_TRUE(TextServiceTestPeer::recreate_session(fixture.service));
        ASSERT_TRUE(fixture.server_chinese.load());
        fixture.confirm();
        ASSERT_TRUE(fixture.caps());
        keyboard.caps(false);
        TextServiceTestPeer::poll_status(fixture.service);
        ASSERT_TRUE(!fixture.caps());
        ASSERT_TRUE(TextServiceTestPeer::status(fixture.service).chinese_mode());
    }
}

TEST(TextServiceStatusRecovery, heartbeat_recreates_invalid_sessions_without_a_key) {
    for (bool chinese : {false, true}) {
        for (bool caps : {false, true}) {
            KeyboardState keyboard;
            StatusFixture fixture;
            StatusProbe probe(fixture.service);
            fixture.server_chinese = chinese;
            fixture.server_caps = caps;
            keyboard.caps(caps);
            fixture.focus();
            fixture.confirm();
            const auto old_id = TextServiceTestPeer::session_id(fixture.service);
            const auto old_generation = TextServiceTestPeer::session_generation(fixture.service);
            const auto publications = probe.mark();
            fixture.active_session = 0;
            TextServiceTestPeer::poll_status(fixture.service, true);
            ASSERT_NE(TextServiceTestPeer::session_id(fixture.service), old_id);
            ASSERT_TRUE(TextServiceTestPeer::session_generation(fixture.service) > old_generation);
            fixture.confirm();
            ASSERT_EQ(fixture.caps(), caps);
            probe.expect_recovered_since(publications, caps, !caps && chinese);
            // An overlay hides the old base mode; a new session keeps its own default.
            keyboard.caps(false);
            TextServiceTestPeer::poll_status(fixture.service);
            ASSERT_EQ(TextServiceTestPeer::status(fixture.service).chinese_mode(), caps || chinese);
        }
    }
}

TEST(TextServiceStatusRecovery, failed_restart_retries_on_heartbeat_without_focus_or_keys) {
    enum class Failure { Start, Status, Disconnect, HeartbeatStatus, RestoreMode };
    for (auto failure : {Failure::Start, Failure::Status, Failure::Disconnect,
                         Failure::HeartbeatStatus, Failure::RestoreMode}) {
        for (bool chinese : {false, true}) {
            for (bool caps : {false, true}) {
                // Only an uncovered English base requires a mode restoration command.
                if (failure == Failure::RestoreMode && (chinese || caps)) {
                    continue;
                }
                KeyboardState keyboard;
                StatusFixture fixture;
                StatusProbe probe(fixture.service);
                fixture.server_chinese = chinese;
                fixture.server_caps = caps;
                keyboard.caps(caps);
                fixture.focus();
                if (failure != Failure::HeartbeatStatus) {
                    fixture.confirm();
                }
                probe.expect_current(caps, !caps && chinese);
                const auto old_id = TextServiceTestPeer::session_id(fixture.service);
                const auto old_generation =
                    TextServiceTestPeer::session_generation(fixture.service);
                const auto publications = probe.mark();
                if (failure != Failure::HeartbeatStatus) {
                    fixture.active_session = 0;
                }
                fixture.reject_start = failure == Failure::Start;
                fixture.reject_status =
                    failure == Failure::Status || failure == Failure::HeartbeatStatus;
                fixture.reject_mode = failure == Failure::RestoreMode;
                if (failure == Failure::Disconnect) {
                    fixture.server.stop();
                }
                TextServiceTestPeer::poll_status(fixture.service, true);
                ASSERT_EQ(TextServiceTestPeer::session_id(fixture.service), 0u);
                const int attempts = fixture.start_requests.load();
                TextServiceTestPeer::poll_status(fixture.service);
                ASSERT_EQ(fixture.start_requests.load(), attempts);
                fixture.reject_start = false;
                fixture.reject_status = false;
                fixture.reject_mode = false;
                if (failure == Failure::HeartbeatStatus) {
                    // A pending focus timer must not bypass heartbeat restoration or throttling.
                    fixture.confirm();
                    ASSERT_EQ(TextServiceTestPeer::session_id(fixture.service), 0u);
                    ASSERT_EQ(fixture.start_requests.load(), attempts);
                }
                if (failure == Failure::Disconnect) {
                    fixture.restart_server();
                }
                TextServiceTestPeer::poll_status(fixture.service, true);
                ASSERT_NE(TextServiceTestPeer::session_id(fixture.service), 0u);
                ASSERT_NE(TextServiceTestPeer::session_id(fixture.service), old_id);
                ASSERT_TRUE(TextServiceTestPeer::session_generation(fixture.service) >
                            old_generation);
                fixture.confirm();
                probe.expect_recovered_since(publications, caps, !caps && chinese);
                keyboard.caps(false);
                TextServiceTestPeer::poll_status(fixture.service);
                ASSERT_EQ(TextServiceTestPeer::status(fixture.service).chinese_mode(),
                          caps || chinese);
                probe.expect_current(false, caps || chinese);
                ASSERT_EQ(fixture.focus_requests.load(), 1);
            }
        }
    }
}

TEST(TextServiceStatusRecovery, failed_focus_hides_cached_status_until_a_valid_response) {
    for (bool caps : {false, true}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        fixture.server_caps = !caps;
        fixture.server_chinese = true;
        keyboard.caps(!caps);
        fixture.focus();
        fixture.confirm();
        probe.expect_visible_caps(!caps);
        const size_t hidden_begin = probe.mark();
        fixture.reject_focus = true;
        fixture.reject_sample = true;
        fixture.server_caps = caps;
        fixture.focus();
        fixture.confirm();
        probe.expect_since(hidden_begin, false, false, false);
        const size_t restored_begin = probe.mark();
        fixture.reject_sample = false;
        keyboard.caps(caps);
        TextServiceTestPeer::poll_status(fixture.service, true);
        probe.expect_since(restored_begin, true, caps, !caps);
    }
}

TEST(TextServiceStatusRecovery, disconnected_sample_waits_for_heartbeat_to_restore_base_language) {
    KeyboardState keyboard;
    StatusFixture fixture;
    StatusProbe probe(fixture.service);
    fixture.server_chinese = false;
    keyboard.caps(false);
    fixture.focus();
    fixture.confirm();
    const auto old_id = TextServiceTestPeer::session_id(fixture.service);
    const auto attempts = fixture.start_requests.load();
    fixture.server.stop();
    fixture.active_session = 0;
    keyboard.caps(true);
    TextServiceTestPeer::poll_status(fixture.service);
    ASSERT_EQ(TextServiceTestPeer::session_id(fixture.service), old_id);
    fixture.restart_server();
    TextServiceTestPeer::poll_status(fixture.service);
    ASSERT_EQ(fixture.start_requests.load(), attempts);
    ASSERT_EQ(TextServiceTestPeer::session_id(fixture.service), old_id);
    TextServiceTestPeer::poll_status(fixture.service, true);
    ASSERT_NE(TextServiceTestPeer::session_id(fixture.service), old_id);
    fixture.confirm();
    probe.expect_current(true, false);
    keyboard.caps(false);
    TextServiceTestPeer::poll_status(fixture.service);
    probe.expect_current(false, false);
}

TEST(TextServiceStatusRecovery, interrupted_restart_restores_all_user_modes_before_showing_status) {
    for (auto failed : {cxxime::IPCCommand::SWITCH_INPUT_MODE, cxxime::IPCCommand::TOGGLE_SHAPE,
                        cxxime::IPCCommand::TOGGLE_PUNCT}) {
        KeyboardState keyboard;
        keyboard.caps(false);
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        fixture.server_chinese = false;
        fixture.server_shape = true;
        fixture.server_punct = false;
        fixture.server_mode = cxxime::InputMode::WUBI;
        fixture.focus();
        fixture.confirm();
        const auto expected = TextServiceTestPeer::status(fixture.service);
        probe.expect_current(false, false);
        const auto before = probe.mark();

        fixture.active_session = 0;
        fixture.server_mode = cxxime::InputMode::PINYIN;
        fixture.reject_restore = failed;
        TextServiceTestPeer::poll_status(fixture.service, true);
        ASSERT_EQ(TextServiceTestPeer::session_id(fixture.service), 0u);
        ASSERT_EQ(TextServiceTestPeer::status(fixture.service).flags, expected.flags);
        ASSERT_EQ(TextServiceTestPeer::status(fixture.service).input_mode, expected.input_mode);
        fixture.reject_restore = cxxime::IPCCommand::PING;
        TextServiceTestPeer::poll_status(fixture.service, true);
        fixture.confirm();
        probe.expect_restored_status(before, expected);
        ASSERT_TRUE(fixture.server_shape.load());
        ASSERT_TRUE(!fixture.server_punct.load());
        ASSERT_EQ(fixture.server_mode.load(), cxxime::InputMode::WUBI);
    }
}

TEST(TextServiceStatusRecovery, refocus_during_an_outage_preserves_all_previous_user_modes) {
    for (bool focus_while_unavailable : {false, true}) {
        KeyboardState keyboard;
        keyboard.caps(false);
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        fixture.server_chinese = false;
        fixture.server_mode = cxxime::InputMode::WUBI;
        fixture.server_shape = true;
        fixture.server_punct = false;
        fixture.focus();
        fixture.confirm();
        probe.expect_current(false, false);
        const auto expected = TextServiceTestPeer::status(fixture.service);
        const auto before = probe.mark();
        ASSERT_EQ(fixture.service.OnKillThreadFocus(), S_OK);
        fixture.server.stop();
        TextServiceTestPeer::poll_status(fixture.service, true);
        if (focus_while_unavailable) {
            fixture.focus();
            fixture.confirm();
        }
        fixture.active_session = 0;
        fixture.server_mode = cxxime::InputMode::PINYIN;
        fixture.restart_server();
        if (focus_while_unavailable) {
            TextServiceTestPeer::poll_status(fixture.service, true);
        } else {
            fixture.focus();
        }
        fixture.confirm();
        probe.expect_restored_status(before, expected);
    }
}

TEST(TextServiceStatusRecovery, menu_during_server_outage_does_not_replace_recovery_preferences) {
    KeyboardState keyboard;
    keyboard.caps(false);
    StatusFixture fixture;
    StatusProbe probe(fixture.service);
    fixture.server_chinese = false;
    fixture.server_mode = cxxime::InputMode::WUBI;
    fixture.focus();
    fixture.confirm();
    probe.expect_current(false, false);
    const auto expected = TextServiceTestPeer::status(fixture.service);
    fixture.server.stop();
    // A failed menu request must not publish the response structure's default Chinese state.
    TextServiceTestPeer::select_input_mode(fixture.service, cxxime::ImeMenuCommand::kPinyin);
    ASSERT_EQ(TextServiceTestPeer::status(fixture.service).flags, expected.flags);
    ASSERT_EQ(TextServiceTestPeer::status(fixture.service).input_mode, expected.input_mode);
    fixture.active_session = 0;
    fixture.server_mode = cxxime::InputMode::PINYIN;
    fixture.restart_server();
    const auto before = probe.mark();
    TextServiceTestPeer::poll_status(fixture.service, true);
    fixture.confirm();
    probe.expect_restored_status(before, expected);
}
