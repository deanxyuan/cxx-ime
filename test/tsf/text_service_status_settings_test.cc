// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "status_test_support.h"

void TextServiceTestPeer::set_status_window_enabled(TextService& service, bool enabled) {
    // Arrange a received preference without touching the user's configuration service.
    service._config.status_window.enable = enabled;
    service._publish_ui_presentation();
    service._update_state_poll_timer();
}

TEST(TextServiceStatusSettings, showing_status_after_hidden_focus_changes_uses_current_state) {
    status_test::KeyboardState keyboard;
    keyboard.caps(false);
    status_test::StatusFixture fixture;
    status_test::StatusProbe probe(fixture.service);
    fixture.server_chinese = true;
    fixture.focus();
    fixture.confirm();
    probe.expect_current(false, true);
    auto before = probe.mark();
    TextServiceTestPeer::set_status_window_enabled(fixture.service, false);
    probe.expect_since(before, false, false, false);

    ASSERT_EQ(fixture.service.OnKillThreadFocus(), S_OK);
    fixture.server_chinese = false;
    fixture.server_caps = true;
    keyboard.caps(true);
    fixture.focus();
    fixture.confirm();
    before = probe.mark();
    TextServiceTestPeer::set_status_window_enabled(fixture.service, true);
    probe.expect_since(before, true, true, false);
    keyboard.caps(false);
    TextServiceTestPeer::poll_status(fixture.service);
    probe.expect_current(false, false);
}
