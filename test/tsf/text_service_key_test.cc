// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "text_service_test_support.h"

using namespace tsf_test;

TEST(TextServiceKey, key_admission_does_not_request_selection_or_layout_preflight) {
    Fixture fixture;
    fixture.view.foreground_window = true;
    fixture.host.active_view = &fixture.view;
    fixture.host.fail_get_selection = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_EQ(fixture.host.starts, 1);
    ASSERT_EQ(fixture.host.read_requests, 0);
    ASSERT_TRUE(fixture.host.range.text == L"n");
    ASSERT_TRUE(TextServiceTestPeer::target_matches(fixture.service, &fixture.host));
}

TEST(TextServiceKey, composition_key_rebinds_changed_context) {
    HostContext next_host;
    Fixture fixture;
    fixture.view.foreground_window = true;
    fixture.host.active_view = &fixture.view;
    next_host.active_view = &fixture.view;
    next_host.document = &fixture.manager.document;
    BOOL eaten = FALSE;
    ASSERT_TRUE(TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    fixture.manager.document.top = &next_host;
    ASSERT_TRUE(TextServiceTestPeer::process_key(fixture.service, &next_host, 'I', &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_TRUE(TextServiceTestPeer::target_matches(fixture.service, &next_host));
    ASSERT_EQ(fixture.host.composition.ends, 1);
    ASSERT_EQ(next_host.starts, 1);
}

TEST(TextServiceKey, explicit_readonly_or_disconnected_context_rejects_text_keys) {
    Fixture fixture;
    fixture.view.foreground_window = true;
    fixture.host.active_view = &fixture.view;
    fixture.host.dynamic_flags = TF_SD_READONLY;
    BOOL eaten = TRUE;
    ASSERT_TRUE(!TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    ASSERT_EQ(eaten, FALSE);
    ASSERT_TRUE(fixture.server.engine.context().active_input().empty());
    fixture.host.dynamic_flags = 0;
    fixture.host.status_result = TF_E_DISCONNECTED;
    ASSERT_TRUE(!TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    ASSERT_EQ(eaten, FALSE);
    ASSERT_EQ(fixture.host.write_requests, 0);
    fixture.host.status_result = S_OK;
    ASSERT_TRUE(TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_TRUE(fixture.host.range.text == L"n");
}

TEST(TextServiceKey, explicit_disabled_and_empty_compartments_reject_text_keys) {
    Fixture fixture;
    fixture.view.foreground_window = true;
    fixture.host.active_view = &fixture.view;
    BOOL eaten = TRUE;
    fixture.host.disabled.value = 1;
    ASSERT_TRUE(!TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    ASSERT_EQ(eaten, FALSE);
    fixture.host.disabled.value = 0;
    fixture.host.empty.value = 1;
    ASSERT_TRUE(!TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    ASSERT_EQ(eaten, FALSE);
    ASSERT_TRUE(fixture.server.engine.context().active_input().empty());
    ASSERT_EQ(fixture.host.write_requests, 0);
    fixture.host.empty.value = 0;
    ASSERT_TRUE(TextServiceTestPeer::process_key(fixture.service, &fixture.host, 'N', &eaten));
    ASSERT_EQ(eaten, TRUE);
}
