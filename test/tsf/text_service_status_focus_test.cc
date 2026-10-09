// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "status_test_support.h"

using namespace status_test;

TEST(TextServiceStatusFocus, losing_the_input_target_cancels_confirmation_and_hides_status) {
    enum class Loss { Thread, KeySink, Document, PopLast, Uninitialize };
    for (auto loss :
         {Loss::Thread, Loss::KeySink, Loss::Document, Loss::PopLast, Loss::Uninitialize}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        keyboard.caps(true);
        fixture.focus();
        probe.expect_visible_caps(false);
        const auto begin = probe.mark();
        const auto old_timer = TextServiceTestPeer::status_timer(fixture.service);
        switch (loss) {
        case Loss::Thread:
            ASSERT_EQ(fixture.service.OnKillThreadFocus(), S_OK);
            break;
        case Loss::KeySink:
            ASSERT_EQ(fixture.service.OnSetFocus(FALSE), S_OK);
            break;
        case Loss::Document:
            fixture.manager.document.top = nullptr;
            ASSERT_EQ(fixture.service.OnSetFocus(nullptr, &fixture.manager.document), S_OK);
            break;
        case Loss::PopLast:
            fixture.manager.document.top = nullptr;
            ASSERT_EQ(fixture.service.OnPopContext(&fixture.host), S_OK);
            break;
        case Loss::Uninitialize:
            ASSERT_EQ(fixture.service.OnUninitDocumentMgr(&fixture.manager.document), S_OK);
            break;
        }
        TextServiceTestPeer::dispatch_status_timer(fixture.service, old_timer);
        ASSERT_EQ(fixture.samples.load(), 0);
        ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), static_cast<UINT_PTR>(0));
        probe.expect_since(begin, false, false, false);
        fixture.manager.document.top = &fixture.host;
        fixture.focus();
        const auto new_timer = TextServiceTestPeer::status_timer(fixture.service);
        ASSERT_NE(new_timer, old_timer);
        TextServiceTestPeer::dispatch_status_timer(fixture.service, old_timer);
        ASSERT_EQ(fixture.samples.load(), 0);
        ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), new_timer);
        fixture.confirm();
        ASSERT_TRUE(fixture.caps());
        probe.expect_current(true, false);
    }
}

TEST(TextServiceStatusFocus, every_focus_entry_refreshes_before_keyboard_confirmation) {
    enum class Entry { Document, KeySink, Push, Pop };
    for (auto entry : {Entry::Document, Entry::KeySink, Entry::Push, Entry::Pop}) {
        for (bool caps : {false, true}) {
            KeyboardState keyboard;
            StatusFixture fixture;
            StatusProbe probe(fixture.service);
            keyboard.caps(!caps);
            fixture.server_caps = caps;
            switch (entry) {
            case Entry::Document:
                ASSERT_EQ(fixture.service.OnSetFocus(&fixture.manager.document, nullptr), S_OK);
                break;
            case Entry::KeySink:
                ASSERT_EQ(fixture.service.OnSetFocus(TRUE), S_OK);
                break;
            case Entry::Push:
                ASSERT_EQ(fixture.service.OnPushContext(&fixture.host), S_OK);
                break;
            case Entry::Pop:
                ASSERT_EQ(fixture.service.OnPopContext(&fixture.host), S_OK);
                break;
            }
            ASSERT_EQ(fixture.focus_requests.load(), 1);
            ASSERT_EQ(fixture.samples.load(), 0);
            ASSERT_EQ(fixture.caps(), caps);
            probe.expect_visible_caps(caps);
            keyboard.caps(caps);
            fixture.confirm();
            ASSERT_EQ(fixture.samples.load(), 1);
        }
    }
}

TEST(TextServiceStatusFocus, repeated_focus_coalesces_confirmation_and_heartbeat_only_reads) {
    KeyboardState keyboard;
    StatusFixture fixture;
    StatusProbe probe(fixture.service);
    keyboard.caps(true);
    fixture.focus();
    const auto timer = TextServiceTestPeer::status_timer(fixture.service);
    for (int i = 0; i < 4; ++i) {
        fixture.focus();
        ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), timer);
        TextServiceTestPeer::poll_status(fixture.service, true);
        ASSERT_EQ(fixture.samples.load(), 0);
        ASSERT_EQ(fixture.status_requests.load(), i + 1);
    }
    fixture.confirm();
    ASSERT_EQ(fixture.samples.load(), 1);
    ASSERT_TRUE(fixture.caps());
}

TEST(TextServiceStatusFocus, unavailable_thread_focus_or_foreground_mismatch_rejects_samples) {
    enum class Reason { BackgroundThread, FailedQuery, OtherWindow };
    for (auto reason : {Reason::BackgroundThread, Reason::FailedQuery, Reason::OtherWindow}) {
        KeyboardState keyboard;
        StatusFixture fixture;
        StatusProbe probe(fixture.service);
        keyboard.caps(true);
        fixture.focus();
        HWND background = nullptr;
        if (reason == Reason::FailedQuery) {
            fixture.manager.focus_result = E_FAIL;
        } else if (reason == Reason::BackgroundThread) {
            fixture.manager.focused = FALSE;
        } else {
            background = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                         GetModuleHandleW(nullptr), nullptr);
            ASSERT_TRUE(background != nullptr);
            ASSERT_NE(background, GetForegroundWindow());
            fixture.view.foreground_window = false;
            fixture.view.explicit_window = background;
        }
        fixture.confirm();
        TextServiceTestPeer::poll_status(fixture.service, true);
        ASSERT_EQ(fixture.samples.load(), 0);
        ASSERT_EQ(fixture.status_requests.load(), 1);
        ASSERT_TRUE(!fixture.server_caps.load());
        if (background) {
            DestroyWindow(background);
        }
    }
}

TEST(TextServiceStatusFocus, context_push_and_pop_invalidate_pending_confirmation) {
    KeyboardState keyboard;
    tsf_test::HostContext replacement;
    StatusFixture fixture;
    StatusProbe probe(fixture.service);
    keyboard.caps(true);
    replacement.document = &fixture.manager.document;
    replacement.active_view = &fixture.view;
    fixture.focus();
    const auto original_timer = TextServiceTestPeer::status_timer(fixture.service);
    const auto original_generation = TextServiceTestPeer::target_generation(fixture.service);
    fixture.manager.document.top = &replacement;
    const auto foreground_before = GetForegroundWindow();
    ASSERT_EQ(fixture.service.OnPushContext(&replacement), S_OK);
    ASSERT_TRUE(TextServiceTestPeer::target_matches(fixture.service, &replacement))
        << "foreground_before=" << foreground_before << " view=" << fixture.view.last_window
        << " foreground_after=" << GetForegroundWindow()
        << " unavailable=" << TextServiceTestPeer::target_unavailable(fixture.service)
        << " generation=" << TextServiceTestPeer::target_generation(fixture.service)
        << " reads=" << replacement.view_requests << " status=" << replacement.status_result
        << " flags=" << replacement.dynamic_flags;
    ASSERT_TRUE(TextServiceTestPeer::target_generation(fixture.service) > original_generation);
    const auto pushed_timer = TextServiceTestPeer::status_timer(fixture.service);
    ASSERT_NE(original_timer, pushed_timer);
    TextServiceTestPeer::dispatch_status_timer(fixture.service, original_timer);
    ASSERT_EQ(fixture.samples.load(), 0);
    ASSERT_EQ(TextServiceTestPeer::status_timer(fixture.service), pushed_timer);
    fixture.manager.document.top = &fixture.host;
    ASSERT_EQ(fixture.service.OnPopContext(&replacement), S_OK);
    ASSERT_TRUE(TextServiceTestPeer::target_matches(fixture.service, &fixture.host));
    TextServiceTestPeer::dispatch_status_timer(fixture.service, pushed_timer);
    ASSERT_EQ(fixture.samples.load(), 0);
    fixture.confirm();
    ASSERT_EQ(fixture.samples.load(), 1);
    ASSERT_TRUE(fixture.caps());
}

TEST(TextServiceStatusFocus, current_timer_cannot_sample_an_obsolete_target_generation) {
    KeyboardState keyboard;
    StatusFixture fixture;
    keyboard.caps(true);
    fixture.focus();
    // Exercise the generation guard independently of cancellation or timer ID replacement.
    TextServiceTestPeer::change_target_generation(fixture.service);
    fixture.confirm();
    ASSERT_EQ(fixture.samples.load(), 0);
    ASSERT_TRUE(!fixture.server_caps.load());
}
