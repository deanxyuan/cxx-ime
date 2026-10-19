// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "activation_test_support.h"
#include "status_test_support.h"

using tsf_test::ActivationManager;

TEST(TextServiceStatusLifecycle, deactivate_and_reactivate_cancel_pending_samples_and_old_state) {
    status_test::KeyboardState keyboard;
    ActivationManager manager;
    status_test::StatusFixture fixture(false);
    manager.document.top = &fixture.host;
    fixture.host.document = &manager.document;
    ASSERT_TRUE(TextServiceTestPeer::connect(fixture.service, fixture.pipe, false));
    UINT_PTR old_timer = 0;
    uint32_t old_session = 0;
    for (bool caps : {true, false, true}) {
        TextServiceTestPeer::prepare_status_dispatch(fixture.service);
        status_test::StatusProbe probe(fixture.service);
        fixture.server_caps = caps;
        keyboard.caps(!caps);
        const HWND foreground_before = GetForegroundWindow();
        ASSERT_EQ(fixture.service.ActivateEx(&manager, 1, 0), S_OK);
        ASSERT_EQ(manager.sinks.size(), size_t{2});
        ASSERT_TRUE(manager.key_sink != nullptr);
        ASSERT_EQ(fixture.caps(), caps);
        const auto session = TextServiceTestPeer::session_id(fixture.service);
        ASSERT_NE(session, old_session);
        const auto timer = TextServiceTestPeer::status_timer(fixture.service);
        ASSERT_NE(timer, old_timer);
        ASSERT_NE(timer, static_cast<UINT_PTR>(0))
            << "caps=" << caps << " foreground_before=" << foreground_before
            << " foreground_after=" << GetForegroundWindow()
            << " focus_requests=" << fixture.focus_requests.load()
            << " samples=" << fixture.samples.load();
        const auto samples = fixture.samples.load();
        if (old_timer) {
            TextServiceTestPeer::dispatch_status_timer(fixture.service, old_timer);
            ASSERT_EQ(fixture.samples.load(), samples);
            ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), timer);
        }
        probe.expect_visible_caps(caps);
        if (!caps) {
            keyboard.caps(caps);
            fixture.confirm();
            ASSERT_EQ(fixture.samples.load(), samples + 1);
        }
        ASSERT_EQ(fixture.service.Deactivate(), S_OK);
        ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), static_cast<UINT_PTR>(0));
        ASSERT_EQ(TextServiceTestPeer::session_id(fixture.service), 0u);
        ASSERT_TRUE(manager.sinks.empty());
        ASSERT_TRUE(manager.key_sink == nullptr);
        ASSERT_EQ(manager.references, 1u);
        TextServiceTestPeer::poll_status(fixture.service, true);
        ASSERT_EQ(fixture.samples.load(), samples + (caps ? 0 : 1));
        old_timer = timer;
        old_session = session;
    }
    ASSERT_EQ(manager.unadvised, 9);
}

TEST(TextServiceStatusLifecycle, service_unavailable_at_activation_recovers_without_user_input) {
    status_test::KeyboardState keyboard;
    keyboard.caps(false);
    ActivationManager manager;
    status_test::StatusFixture fixture(false);
    manager.document.top = &fixture.host;
    fixture.host.document = &manager.document;
    ASSERT_TRUE(TextServiceTestPeer::connect(fixture.service, fixture.pipe, false));
    TextServiceTestPeer::prepare_status_dispatch(fixture.service);
    status_test::StatusProbe probe(fixture.service);
    fixture.reject_start = true;
    ASSERT_EQ(fixture.service.ActivateEx(&manager, 1, 0), S_OK);
    ASSERT_EQ(TextServiceTestPeer::session_id(fixture.service), 0u);
    fixture.reject_start = false;
    // Neither a new focus notification nor a key is needed when the server becomes ready.
    TextServiceTestPeer::poll_status(fixture.service, true);
    ASSERT_NE(TextServiceTestPeer::session_id(fixture.service), 0u);
    fixture.confirm();
    probe.expect_current(false, true);
    ASSERT_EQ(fixture.service.Deactivate(), S_OK);
    ASSERT_TRUE(manager.sinks.empty());
    ASSERT_TRUE(manager.key_sink == nullptr);
}
