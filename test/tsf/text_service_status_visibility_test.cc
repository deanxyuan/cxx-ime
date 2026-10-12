// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "status_test_support.h"

using namespace status_test;

TEST(TextServiceStatusVisibility, unrelated_document_stack_events_do_not_acquire_or_replace_focus) {
    for (bool has_target : {false, true}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        tsf_test::HostDocument other_document;
        tsf_test::HostContext other;
        other.document = &other_document;
        other.active_view = &fixture.view; // The same foreground HWND is not input focus.
        other_document.top = &other;
        if (has_target) {
            fixture.focus();
            probe.expect_current(false, true);
        }
        const auto generation = TextServiceTestPeer::target_generation(fixture.service);
        const int requests = fixture.focus_requests.load();
        const auto before = probe.mark();
        ASSERT_EQ(fixture.service.OnPushContext(&other), S_OK);
        ASSERT_EQ(fixture.service.OnPopContext(&other), S_OK);
        ASSERT_EQ(fixture.focus_requests.load(), requests);
        ASSERT_EQ(TextServiceTestPeer::target_generation(fixture.service), generation);
        ASSERT_EQ(TextServiceTestPeer::target_matches(fixture.service, &fixture.host), has_target);
        TextServiceTestPeer::set_status_window_enabled(fixture.service, true);
        probe.expect_visibility_since(before, has_target);
    }
}

TEST(TextServiceStatusVisibility, stack_events_resolve_current_top_instead_of_notified_context) {
    KeyboardState keyboard;
    StatusFixture fixture;
    StatusProbe probe(fixture.service);
    tsf_test::HostContext obsolete;
    obsolete.document = &fixture.manager.document;
    obsolete.active_view = &fixture.view;
    // Both notifications can arrive after the host has changed the stack again.
    ASSERT_EQ(fixture.service.OnPushContext(&obsolete), S_OK);
    ASSERT_TRUE(TextServiceTestPeer::target_matches(fixture.service, &fixture.host));
    probe.expect_current(false, true);
    ASSERT_EQ(fixture.service.OnPopContext(&obsolete), S_OK);
    ASSERT_TRUE(TextServiceTestPeer::target_matches(fixture.service, &fixture.host));
    ASSERT_TRUE(TextServiceTestPeer::empty(fixture.service));
}

TEST(TextServiceStatusVisibility, explicit_document_loss_does_not_requery_the_previous_document) {
    KeyboardState keyboard;
    StatusFixture fixture;
    StatusProbe probe(fixture.service);
    fixture.focus();
    probe.expect_current(false, true);
    const int requests = fixture.focus_requests.load();
    const auto before = probe.mark();
    // Keep the previous GetFocus result to exercise the callback's explicit NULL meaning.
    ASSERT_EQ(fixture.service.OnSetFocus(nullptr, &fixture.manager.document), S_OK);
    ASSERT_EQ(fixture.focus_requests.load(), requests);
    ASSERT_TRUE(!TextServiceTestPeer::target_matches(fixture.service, &fixture.host));
    probe.expect_visibility_since(before, false);
    fixture.manager.focus_document = nullptr;
    for (bool push : {true, false}) {
        const auto missing_document = probe.mark();
        if (push) {
            ASSERT_EQ(fixture.service.OnPushContext(&fixture.host), S_OK);
        } else {
            ASSERT_EQ(fixture.service.OnPopContext(&fixture.host), S_OK);
        }
        probe.expect_visibility_since(missing_document, false);
        ASSERT_EQ(fixture.focus_requests.load(), requests);
    }
    fixture.manager.focus_document = &fixture.manager.document;
    fixture.focus();
    probe.expect_current(false, true);
}

TEST(TextServiceStatusVisibility, background_thread_events_cannot_reacquire_status) {
    for (bool failed_query : {false, true}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        fixture.focus();
        probe.expect_current(false, true);
        fixture.manager.focused = FALSE;
        fixture.manager.focus_result = failed_query ? E_FAIL : S_OK;
        auto before = probe.mark();
        ASSERT_EQ(fixture.service.OnKillThreadFocus(), S_OK);
        probe.expect_visibility_since(before, false);
        const int requests = fixture.focus_requests.load();
        for (int step = 0; step < 4; ++step) {
            before = probe.mark();
            switch (step) {
            case 0:
                ASSERT_EQ(fixture.service.OnPushContext(&fixture.host), S_OK);
                break;
            case 1:
                ASSERT_EQ(fixture.service.OnPopContext(&fixture.host), S_OK);
                break;
            case 2:
                ASSERT_EQ(fixture.service.OnSetFocus(&fixture.manager.document, nullptr), S_OK);
                break;
            default:
                ASSERT_EQ(fixture.service.OnSetFocus(TRUE), S_OK);
                break;
            }
            probe.expect_visibility_since(before, false);
            ASSERT_EQ(fixture.focus_requests.load(), requests);
            ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), static_cast<UINT_PTR>(0));
        }
        fixture.manager.focused = TRUE;
        fixture.manager.focus_result = S_OK;
        fixture.focus();
        probe.expect_current(false, true);
    }
}

TEST(TextServiceStatusVisibility, windowless_focus_and_host_ui_only_survive_noncomposing_switches) {
    for (bool host_ui_only : {false, true}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        fixture.view.foreground_window = false;
        fixture.view.explicit_window = nullptr;
        if (host_ui_only) {
            TextServiceTestPeer::host_ui_only(fixture.service);
        }
        auto before = probe.mark();
        fixture.focus();
        probe.expect_visibility_since(before, !host_ui_only);
        ASSERT_TRUE(TextServiceTestPeer::target_matches(fixture.service, &fixture.host));
        tsf_test::HostContext replacement;
        replacement.document = &fixture.manager.document;
        replacement.active_view = &fixture.view;
        for (int step = 0; step < 6; ++step) {
            before = probe.mark();
            fixture.manager.document.top = step % 2 == 0 ? &replacement : &fixture.host;
            if (step % 2 == 0) {
                ASSERT_EQ(fixture.service.OnPushContext(&replacement), S_OK);
            } else {
                ASSERT_EQ(fixture.service.OnPopContext(&replacement), S_OK);
            }
            ASSERT_TRUE(TextServiceTestPeer::empty(fixture.service));
            probe.expect_visibility_since(before, !host_ui_only);
        }
    }
}
