// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "status_test_support.h"

using namespace status_test;

TEST(TextServiceStatus, focus_publishes_server_state_before_deferred_keyboard_confirmation) {
    for (bool caps : {false, true}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        cxxime::ImeStatus old;
        old.set_chinese_mode(false);
        old.set_caps_lock(!caps);
        TextServiceTestPeer::seed_status(fixture.service, old);
        StatusProbe probe(fixture.service);
        fixture.server_caps = caps;
        keyboard.caps(!caps);
        fixture.focus();
        ASSERT_EQ(fixture.focus_requests.load(), 1);
        ASSERT_EQ(fixture.caps(), caps);
        ASSERT_EQ(fixture.samples.load(), 0);
        probe.expect_visible_caps(caps);
        keyboard.caps(caps);
        fixture.confirm();
        ASSERT_EQ(fixture.caps(), caps);
        ASSERT_EQ(fixture.samples.load(), 1);
    }
}

TEST(TextServiceStatus, deferred_confirmation_repairs_server_even_when_local_sample_matches) {
    KeyboardState keyboard;
    StatusFixture fixture;
    keyboard.caps(false);
    fixture.focus();
    ASSERT_TRUE(!fixture.caps());
    fixture.server_caps = true;
    fixture.confirm();
    ASSERT_EQ(fixture.samples.load(), 1);
    ASSERT_TRUE(!fixture.server_caps.load());
    ASSERT_TRUE(!fixture.caps());
}

TEST(TextServiceStatus, runtime_check_recovers_failed_confirmation_without_a_key) {
    KeyboardState keyboard;
    StatusFixture fixture;
    fixture.focus();
    keyboard.caps(true);
    fixture.reject_sample = true;
    fixture.confirm();
    ASSERT_TRUE(!fixture.caps());
    fixture.reject_sample = false;
    TextServiceTestPeer::poll_status(fixture.service);
    ASSERT_TRUE(fixture.caps());
    ASSERT_TRUE(fixture.server_caps.load());

    // A later stale server value must also be repaired when local state did not change.
    fixture.server_caps = false;
    TextServiceTestPeer::poll_status(fixture.service, true);
    ASSERT_TRUE(fixture.caps());
    ASSERT_TRUE(fixture.server_caps.load());
}

TEST(TextServiceStatus, real_message_loop_confirms_focus_without_keyboard_input) {
    StatusFixture fixture;
    fixture.reject_focus = true;
    fixture.focus();
    ASSERT_NE(TextServiceTestPeer::status_timer(fixture.service), static_cast<UINT_PTR>(0));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (TextServiceTestPeer::status_timer(fixture.service) != 0 &&
           std::chrono::steady_clock::now() < deadline) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), static_cast<UINT_PTR>(0));
    ASSERT_GE(fixture.samples.load(), 1);
    ASSERT_EQ(fixture.caps(), fixture.server_caps.load());
}
