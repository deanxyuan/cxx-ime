// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include <cstring>
#include <initializer_list>
#include <string>

#include "text_service_test_support.h"

using namespace tsf_test;

TEST(TextServiceComposition, refused_first_composition_passes_key_and_discards_engine_prefix) {
    Fixture fixture;
    fixture.host.reject_composition = true;
    const auto first = fixture.key('N');
    ASSERT_EQ(first.preedit, "n");
    BOOL eaten = TRUE;
    ASSERT_TRUE(!fixture.apply(first, &eaten));
    ASSERT_EQ(eaten, FALSE);
    ASSERT_GE(fixture.host.starts, 1);
    ASSERT_EQ(fixture.host.range.writes, 0);
    ASSERT_EQ(fixture.host.composition.ends, 0);
    fixture.assert_cleared();
    const auto next = fixture.key('H');
    ASSERT_EQ(next.preedit, "h");
}

TEST(TextServiceComposition, failed_text_write_ends_partial_composition_without_replaying_key) {
    Fixture fixture;
    fixture.host.range.fail_write = true;
    fixture.host.range.text = L"original selection";
    BOOL eaten = FALSE;
    ASSERT_TRUE(!fixture.apply(fixture.key('N'), &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_EQ(fixture.host.starts, 1);
    ASSERT_EQ(fixture.host.composition.ends, 1);
    ASSERT_TRUE(fixture.host.range.text == L"original selection");
    fixture.assert_cleared();
}

TEST(TextServiceComposition, failed_selection_removes_written_text_without_replaying_key) {
    Fixture fixture;
    fixture.host.fail_selection = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(!fixture.apply(fixture.key('N'), &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_EQ(fixture.host.selections, 1);
    ASSERT_EQ(fixture.host.composition.ends, 1);
    ASSERT_TRUE(fixture.host.range.text.empty());
    fixture.assert_cleared();
}

TEST(TextServiceComposition, deferred_rejection_clears_engine_and_presentation) {
    Fixture fixture;
    fixture.host.defer_write = true;
    fixture.host.range.fail_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_EQ(fixture.server.clears.load(), 0);
    ASSERT_TRUE(fixture.host.pending != nullptr);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    fixture.assert_cleared();
    ASSERT_EQ(fixture.key('H').preedit, "h");
}

TEST(TextServiceComposition, obsolete_generation_cannot_edit_or_clear_new_presentation) {
    Fixture fixture;
    fixture.host.defer_write = true;
    fixture.host.reject_composition = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    const auto old_generation = TextServiceTestPeer::generation(fixture.service);
    TextServiceTestPeer::replace_presentation(fixture.service);
    ASSERT_NE(TextServiceTestPeer::generation(fixture.service), old_generation);
    TextServiceTestPeer::invalidate_composition_edits(fixture.service);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 0);
    ASSERT_EQ(fixture.server.clears.load(), 0);
    ASSERT_EQ(TextServiceTestPeer::preedit(fixture.service), "new");
}

TEST(TextServiceComposition, obsolete_context_cannot_edit_or_clear_current_context) {
    Fixture fixture;
    HostContext next_host;
    fixture.host.defer_write = true;
    fixture.host.reject_composition = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    TextServiceTestPeer::bind(fixture.service, &next_host);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 0);
    ASSERT_EQ(fixture.server.clears.load(), 0);
    ASSERT_EQ(fixture.server.engine.context().active_input(), "n");
}

TEST(TextServiceComposition, failed_server_clear_discards_the_session) {
    Fixture fixture;
    fixture.server.reject_clear = true;
    fixture.host.reject_composition = true;
    BOOL eaten = TRUE;
    ASSERT_TRUE(!fixture.apply(fixture.key('N'), &eaten));
    ASSERT_EQ(eaten, FALSE);
    ASSERT_EQ(fixture.server.clears.load(), 1);
    ASSERT_TRUE(TextServiceTestPeer::empty(fixture.service));
    ASSERT_TRUE(TextServiceTestPeer::disconnected(fixture.service));
}

TEST(TextServiceComposition, rejected_write_request_passes_unedited_first_key) {
    Fixture fixture;
    fixture.host.reject_write_request = true;
    BOOL eaten = TRUE;
    ASSERT_TRUE(!fixture.apply(fixture.key('N'), &eaten));
    ASSERT_EQ(eaten, FALSE);
    ASSERT_TRUE(fixture.host.pending == nullptr);
    ASSERT_EQ(fixture.host.starts, 0);
    ASSERT_EQ(fixture.host.range.writes, 0);
    fixture.assert_cleared();
}

TEST(TextServiceComposition, partial_commit_failure_does_not_repeat_committed_text) {
    Fixture fixture;
    fixture.key('N');
    fixture.host.fail_selection = true;
    cxxime::IPCResponse response = {};
    response.status = cxxime::IPCStatus::OK;
    strcpy_s(response.commit_text, "committed");
    BOOL eaten = FALSE;
    ASSERT_TRUE(!fixture.apply(response, &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_EQ(fixture.host.range.writes, 1);
    ASSERT_TRUE(fixture.host.range.text == L"committed");
    ASSERT_EQ(fixture.host.selections, 1);
    ASSERT_TRUE(fixture.host.pending == nullptr);
    fixture.assert_cleared();
}

TEST(TextServiceComposition, queued_commit_survives_presentation_generation_change) {
    Fixture fixture;
    fixture.host.defer_write = true;
    cxxime::IPCResponse response = {};
    response.status = cxxime::IPCStatus::OK;
    strcpy_s(response.commit_text, "committed");
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_TRUE(fixture.host.pending != nullptr);
    TextServiceTestPeer::replace_presentation(fixture.service);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.range.writes, 1);
    ASSERT_TRUE(fixture.host.range.text == L"committed");
    ASSERT_EQ(fixture.server.clears.load(), 0);
    ASSERT_EQ(TextServiceTestPeer::preedit(fixture.service), "new");
}

TEST(TextServiceComposition, null_context_discards_engine_prefix_with_or_without_bound_target) {
    for (bool bound_target : {true, false}) {
        Fixture fixture;
        if (!bound_target) {
            TextServiceTestPeer::clear_focus(fixture.service);
        }
        const auto response = fixture.key('N');
        BOOL eaten = TRUE;
        ASSERT_TRUE(!TextServiceTestPeer::apply(fixture.service, nullptr, response, &eaten));
        fixture.assert_cleared();
        ASSERT_EQ(fixture.key('H').preedit, "h");
    }
}

TEST(TextServiceComposition, cancel_invalidates_queued_first_composition) {
    Fixture fixture;
    fixture.host.defer_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    ASSERT_TRUE(fixture.host.pending != nullptr);
    ASSERT_TRUE(fixture.apply(fixture.key(VK_ESCAPE), &eaten));
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 0);
    ASSERT_EQ(fixture.host.range.writes, 0);
    ASSERT_TRUE(TextServiceTestPeer::empty(fixture.service));
    ASSERT_EQ(fixture.key('H').preedit, "h");
}

TEST(TextServiceComposition, focus_loss_invalidates_queued_first_composition) {
    Fixture fixture;
    fixture.host.defer_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    TextServiceTestPeer::clear_focus(fixture.service);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 0);
    ASSERT_TRUE(TextServiceTestPeer::empty(fixture.service));
}

TEST(TextServiceComposition, host_termination_invalidates_queued_update) {
    Fixture fixture;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    fixture.host.defer_write = true;
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    ASSERT_EQ(fixture.service.OnCompositionTerminated(1, &fixture.host.composition), S_OK);
    const int writes = fixture.host.range.writes;
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 1);
    ASSERT_EQ(fixture.host.range.writes, writes);
    fixture.assert_cleared();
}

TEST(TextServiceComposition, missing_insertion_point_never_commits_at_document_start) {
    Fixture fixture;
    fixture.host.fail_get_selection = true;
    fixture.host.insertion_result = TF_E_NOSELECTION;
    fixture.host.range.text = L"original document";
    cxxime::IPCResponse response = {};
    response.status = cxxime::IPCStatus::OK;
    strcpy_s(response.commit_text, "committed");
    BOOL eaten = FALSE;
    ASSERT_TRUE(!fixture.apply(response, &eaten));
    ASSERT_EQ(fixture.host.document_start_queries, 0);
    ASSERT_EQ(fixture.host.range.writes, 0);
    ASSERT_TRUE(fixture.host.range.text == L"original document");
    fixture.assert_cleared();
}

TEST(TextServiceComposition, queried_insertion_point_works_without_readable_selection) {
    Fixture fixture;
    fixture.host.fail_get_selection = true;
    cxxime::IPCResponse response = {};
    response.status = cxxime::IPCStatus::OK;
    strcpy_s(response.commit_text, "committed");
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_EQ(fixture.host.document_start_queries, 0);
    ASSERT_EQ(fixture.host.range.writes, 1);
    ASSERT_TRUE(fixture.host.range.text == L"committed");
}

TEST(TextServiceComposition, deferred_commit_precedes_restart_in_one_write_session) {
    Fixture fixture;
    auto response = fixture.key('N');
    strcpy_s(response.commit_text, "chosen");
    fixture.host.defer_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    ASSERT_TRUE(fixture.host.later_pending.empty());
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 1);
    ASSERT_EQ(fixture.host.range.written_texts.size(), 2u);
    ASSERT_TRUE(fixture.host.range.written_texts[0] == L"chosen");
    ASSERT_TRUE(fixture.host.range.written_texts[1] == L"n");
    fixture.host.defer_write = false;
}

TEST(TextServiceComposition, canceled_restart_still_writes_already_confirmed_commit) {
    Fixture fixture;
    auto response = fixture.key('N');
    strcpy_s(response.commit_text, "chosen");
    fixture.host.defer_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    ASSERT_TRUE(fixture.apply(fixture.key(VK_ESCAPE), &eaten));
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 0);
    ASSERT_EQ(fixture.host.range.writes, 1);
    ASSERT_TRUE(fixture.host.range.text == L"chosen");
    ASSERT_TRUE(TextServiceTestPeer::empty(fixture.service));
}

TEST(TextServiceComposition, later_key_cannot_overtake_queued_commit) {
    Fixture fixture;
    auto response = fixture.key('N');
    strcpy_s(response.commit_text, "chosen");
    fixture.host.defer_write = true;
    fixture.host.honor_async = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    fixture.host.defer_write = false;
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    ASSERT_EQ(fixture.host.range.writes, 0);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.range.written_texts.size(), 3u);
    ASSERT_TRUE(fixture.host.range.written_texts[0] == L"chosen");
    ASSERT_TRUE(fixture.host.range.written_texts[1] == L"n");
    ASSERT_TRUE(fixture.host.range.written_texts[2] == L"ni");
    fixture.host.honor_async = false;
}

TEST(TextServiceComposition, failed_queued_commit_cancels_later_preedit_updates) {
    Fixture fixture;
    auto response = fixture.key('N');
    strcpy_s(response.commit_text, "chosen");
    fixture.host.defer_write = true;
    fixture.host.range.fail_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.starts, 0);
    ASSERT_EQ(fixture.host.range.writes, 1);
    fixture.assert_cleared();
}

TEST(TextServiceComposition, queued_commit_does_not_modify_another_context_composition) {
    HostContext next_host;
    Fixture fixture;
    auto response = fixture.key('N');
    strcpy_s(response.commit_text, "chosen");
    fixture.host.defer_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    TextServiceTestPeer::bind(fixture.service, &next_host);
    auto next_response = fixture.key('H');
    ASSERT_TRUE(TextServiceTestPeer::apply(fixture.service, &next_host, next_response, &eaten));
    const std::wstring next_text = next_host.range.text;
    const int next_writes = next_host.range.writes;
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.range.writes, 1);
    ASSERT_TRUE(fixture.host.range.text == L"chosen");
    ASSERT_EQ(next_host.range.writes, next_writes);
    ASSERT_TRUE(next_host.range.text == next_text);
    ASSERT_EQ(next_host.composition.ends, 0);
}

TEST(TextServiceComposition, popup_placeholder_survives_updates_and_is_replaced_or_canceled) {
    for (bool commit : {true, false}) {
        Fixture fixture;
        TextServiceTestPeer::popup_only(fixture.service);
        // A plausible host rectangle must not suppress the nonempty composition range.
        fixture.host.active_view = &fixture.view;
        fixture.view.text_result = S_OK;
        fixture.view.text_rect = {100, 100, 117, 120};
        BOOL eaten = FALSE;
        ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
        ASSERT_TRUE(fixture.service.empty_composition_placeholder_active());
        ASSERT_TRUE(fixture.host.range.text == L" ");
        ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
        ASSERT_TRUE(fixture.host.range.text == L" ");
        ASSERT_EQ(fixture.host.starts, 1);
        ASSERT_EQ(fixture.host.composition.ends, 0);
        for (const auto& text : fixture.host.range.written_texts) {
            ASSERT_TRUE(text == L" ");
        }

        auto response = fixture.key(VK_ESCAPE);
        if (commit) {
            strcpy_s(response.commit_text, "chosen");
        }
        ASSERT_TRUE(fixture.apply(response, &eaten));
        ASSERT_TRUE(fixture.host.range.text == (commit ? L"chosen" : L""));
        ASSERT_EQ(fixture.host.composition.ends, 1);
        ASSERT_TRUE(!fixture.service.empty_composition_placeholder_active());
        ASSERT_TRUE(TextServiceTestPeer::empty(fixture.service));
    }
}

TEST(TextServiceComposition, popup_first_write_failure_preserves_original_selection) {
    Fixture fixture;
    TextServiceTestPeer::popup_only(fixture.service);
    fixture.host.range.text = L"original selection";
    fixture.host.range.fail_write = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(!fixture.apply(fixture.key('N'), &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_TRUE(fixture.host.range.text == L"original selection");
    ASSERT_EQ(fixture.host.composition.ends, 1);
    ASSERT_TRUE(!fixture.service.empty_composition_placeholder_active());
    fixture.assert_cleared();
}

TEST(TextServiceComposition, popup_selection_failure_removes_placeholder) {
    Fixture fixture;
    TextServiceTestPeer::popup_only(fixture.service);
    fixture.host.fail_selection = true;
    BOOL eaten = FALSE;
    ASSERT_TRUE(!fixture.apply(fixture.key('N'), &eaten));
    ASSERT_EQ(eaten, TRUE);
    ASSERT_TRUE(fixture.host.range.written_texts.front() == L" ");
    ASSERT_TRUE(fixture.host.range.text.empty());
    ASSERT_EQ(fixture.host.composition.ends, 1);
    ASSERT_TRUE(!fixture.service.empty_composition_placeholder_active());
    fixture.assert_cleared();
}

TEST(TextServiceComposition, popup_deferred_update_failure_cleans_existing_placeholder) {
    Fixture fixture;
    TextServiceTestPeer::popup_only(fixture.service);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    ASSERT_TRUE(fixture.host.range.text == L" ");
    fixture.host.defer_write = true;
    fixture.host.range.fail_write = true;
    ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
    ASSERT_TRUE(fixture.apply(fixture.key('H'), &eaten));
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    const int writes = fixture.host.range.writes;
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.range.writes, writes);
    ASSERT_TRUE(fixture.host.range.text.empty());
    ASSERT_EQ(fixture.host.composition.ends, 1);
    ASSERT_TRUE(!fixture.service.empty_composition_placeholder_active());
    fixture.assert_cleared();
}

TEST(TextServiceComposition, popup_host_termination_cleans_only_unchanged_readable_placeholder) {
    for (int scenario = 0; scenario < 4; ++scenario) {
        Fixture fixture;
        TextServiceTestPeer::popup_only(fixture.service);
        BOOL eaten = FALSE;
        ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
        ASSERT_TRUE(fixture.host.range.text == L" ");
        fixture.host.defer_write = true;
        ASSERT_TRUE(fixture.apply(fixture.key('I'), &eaten));
        if (scenario == 1) {
            fixture.host.range.text = L"host replacement";
        } else if (scenario == 2) {
            fixture.host.range.text.clear();
        } else if (scenario == 3) {
            fixture.host.range.fail_read = true;
        }
        const auto expected = scenario == 0 ? L"" : fixture.host.range.text;
        const int writes_before_termination = fixture.host.range.writes;
        ASSERT_EQ(fixture.service.OnCompositionTerminated(1, &fixture.host.composition), S_OK);
        ASSERT_TRUE(fixture.host.range.text == expected);
        ASSERT_EQ(fixture.host.range.writes, writes_before_termination + (scenario == 0 ? 1 : 0));
        const int writes = fixture.host.range.writes;
        ASSERT_EQ(fixture.host.complete_pending(), S_OK);
        ASSERT_EQ(fixture.host.range.writes, writes);
        ASSERT_EQ(fixture.host.starts, 1);
        ASSERT_TRUE(!fixture.service.empty_composition_placeholder_active());
        fixture.assert_cleared();
    }
}

TEST(TextServiceComposition, popup_deferred_commit_replaces_placeholder_before_restart) {
    Fixture fixture;
    TextServiceTestPeer::popup_only(fixture.service);
    BOOL eaten = FALSE;
    ASSERT_TRUE(fixture.apply(fixture.key('N'), &eaten));
    ASSERT_TRUE(fixture.host.range.text == L" ");
    auto response = fixture.key('I');
    strcpy_s(response.commit_text, "chosen");
    fixture.host.defer_write = true;
    ASSERT_TRUE(fixture.apply(response, &eaten));
    ASSERT_EQ(fixture.host.complete_pending(), S_OK);
    ASSERT_EQ(fixture.host.range.written_texts.size(), 3u);
    ASSERT_TRUE(fixture.host.range.written_texts[1] == L"chosen");
    ASSERT_TRUE(fixture.host.range.written_texts[2] == L" ");
    ASSERT_EQ(fixture.host.starts, 2);
    ASSERT_EQ(fixture.host.composition.ends, 1);
    ASSERT_TRUE(fixture.service.empty_composition_placeholder_active());
    fixture.host.defer_write = false;
    ASSERT_TRUE(fixture.apply(fixture.key(VK_ESCAPE), &eaten));
    ASSERT_TRUE(fixture.host.range.text.empty());
}

RUN_ALL_TESTS();
