// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>
#include <utility>

#include "candidate_presentation.h"
#include "support/testutil.h"

namespace {

cxxime::CandidatePage page_with_candidate(const char* text) {
    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text = text;
    page.candidates.push_back(candidate);
    return page;
}

cxxime::CandidatePage page_with_candidates(std::size_t count) {
    cxxime::CandidatePage page;
    for (std::size_t index = 0; index < count; ++index) {
        cxxime::Candidate candidate;
        candidate.text = "candidate";
        page.candidates.push_back(std::move(candidate));
    }
    return page;
}

} // namespace

TEST(CandidatePresentation, external_ready_expects_window) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 2);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_TRUE(presentation.external_window_expected());
    ASSERT_TRUE(presentation.should_show_external_window(true));
}

TEST(CandidatePresentation, content_update_without_focus_keeps_preedit_unfocused) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "ni", 2, 1, 1);

    ASSERT_EQ(presentation.focused_preedit_start(), static_cast<std::size_t>(2));
    ASSERT_EQ(presentation.focused_preedit_end(), static_cast<std::size_t>(2));
}

TEST(CandidatePresentation, host_ownership_never_expects_external_window) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kHost);

    ASSERT_TRUE(!presentation.external_window_expected());
}

TEST(CandidatePresentation, local_visible_count_resets_when_content_or_presenter_changes) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidates(2), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kLocal);
    presentation.set_local_visible_candidate_count(2);

    ASSERT_EQ(presentation.local_visible_candidate_count(), 2u);
    presentation.update_content(page_with_candidates(3), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    ASSERT_EQ(presentation.local_visible_candidate_count(), 0u);

    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kLocal);
    presentation.set_local_visible_candidate_count(2);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kServer);
    ASSERT_EQ(presentation.local_visible_candidate_count(), 0u);
}

TEST(CandidatePresentation, presenter_transition_advances_presentation_generation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidates(3), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kServer);
    const std::uint64_t server_generation = presentation.presentation_generation();

    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kHost);

    ASSERT_TRUE(presentation.presentation_generation() > server_generation);
    ASSERT_EQ(presentation.presenter(), cxxime_tsf::CandidatePresenter::kHost);
}

TEST(CandidatePresentation, host_takeover_suppresses_external_wait_without_losing_guard) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.begin_positioning(false, cxxime_tsf::CandidatePresentation::Clock::now());

    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kHost);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.external_window_expected());
}

TEST(CandidatePresentation, finish_clears_a_pending_composition_restart) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "preedit", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const std::uint64_t generation = presentation.generation();
    presentation.begin_positioning(true, cxxime_tsf::CandidatePresentation::Clock::now());

    ASSERT_TRUE(presentation.composition_restart_pending());
    presentation.finish();
    ASSERT_TRUE(!presentation.generation_matches(generation));
    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_EQ(presentation.ownership(), cxxime_tsf::CandidateOwnership::kNone);
    ASSERT_TRUE(!presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.composition_restart_pending());
}

TEST(CandidatePresentation, caret_resolution_does_not_end_restart_episode) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "preedit", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const std::uint64_t generation = presentation.generation();
    presentation.begin_positioning(true, cxxime_tsf::CandidatePresentation::Clock::now());
    ASSERT_TRUE(presentation.complete_composition_restart(generation));

    ASSERT_TRUE(!presentation.composition_restart_pending());
    ASSERT_TRUE(presentation.composition_restart_active());
    presentation.finish();
    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_TRUE(!presentation.composition_restart_active());
}

TEST(CandidatePresentation, restart_episode_follows_the_latest_content_generation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("old"), "preedit", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const std::uint64_t old_generation = presentation.generation();
    presentation.begin_positioning(true, cxxime_tsf::CandidatePresentation::Clock::now());
    presentation.update_content(page_with_candidate("new"), "new", 3, 1, 1);
    const std::uint64_t current_generation = presentation.generation();

    ASSERT_TRUE(presentation.composition_restart_active());
    ASSERT_TRUE(!presentation.complete_composition_restart(old_generation));
    ASSERT_TRUE(presentation.composition_restart_pending());
    ASSERT_TRUE(presentation.complete_composition_restart(current_generation));
    ASSERT_TRUE(!presentation.composition_restart_pending());
    presentation.finish();
    ASSERT_TRUE(!presentation.composition_restart_active());
}

TEST(CandidatePresentation, host_can_return_waiting_presentation_to_external_ownership) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.begin_positioning(false, cxxime_tsf::CandidatePresentation::Clock::now());
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kHost);

    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.should_show_external_window(true));
}

TEST(CandidatePresentation, commit_guard_precedes_new_content_and_ownership) {
    cxxime_tsf::CandidatePresentation presentation;
    const auto started = cxxime_tsf::CandidatePresentation::Clock::now();
    presentation.begin_positioning(true, started);

    presentation.update_content(page_with_candidate("new"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.caret_resolution_allowed());
    ASSERT_TRUE(!presentation.should_show_external_window(true));
}

TEST(CandidatePresentation, zero_content_invalidates_previous_page) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("stale"), "", 0, 2, 3);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.update_content(cxxime::CandidatePage(), "", 0, 8, 9);

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_TRUE(presentation.page().items.empty());
    ASSERT_EQ(presentation.page_current(), 0);
    ASSERT_EQ(presentation.page_total(), 0);
    ASSERT_EQ(presentation.ownership(), cxxime_tsf::CandidateOwnership::kNone);
    ASSERT_TRUE(!presentation.external_window_expected());
}

TEST(CandidatePresentation, popup_preedit_is_presentable_without_candidates) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(cxxime::CandidatePage(), "preedit", 3, 0, 0);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kPreeditOnly);
    ASSERT_EQ(presentation.popup_preedit(), "preedit");
    ASSERT_EQ(presentation.popup_preedit_cursor(), 3);
    ASSERT_TRUE(presentation.external_window_expected());
}

TEST(CandidatePresentation, preserves_revision_converted_prefix_and_hint) {
    cxxime::CandidatePresentationPage page;
    cxxime::CandidatePresentationItem item;
    item.text = "candidate";
    item.hint = "/rs";
    page.items.push_back(item);

    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page, "prefix-code", 11, 6, 42, 1, 1);

    ASSERT_EQ(presentation.candidate_revision(), static_cast<std::uint64_t>(42));
    ASSERT_EQ(presentation.converted_prefix_bytes(), static_cast<std::size_t>(6));
    ASSERT_EQ(presentation.page().items[0].hint, std::string("/rs"));
    ASSERT_EQ(cxxime::format_candidate_presentation(presentation.page().items[0]),
              std::string("candidate(/rs)"));
}

TEST(CandidatePresentation, candidate_to_popup_preedit_transition_keeps_presentation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("stale"), "old", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    presentation.update_content(cxxime::CandidatePage(), "raw", 3, 0, 0);

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kPreeditOnly);
    ASSERT_TRUE(presentation.page().items.empty());
    ASSERT_EQ(presentation.popup_preedit(), "raw");
    ASSERT_EQ(presentation.popup_preedit_cursor(), static_cast<std::size_t>(3));
    ASSERT_TRUE(presentation.external_window_expected());
}

TEST(CandidatePresentation, finish_clears_all_presentation_state) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "preedit", 2, 2, 4);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.begin_positioning(false, cxxime_tsf::CandidatePresentation::Clock::now());

    presentation.finish();

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_EQ(presentation.ownership(), cxxime_tsf::CandidateOwnership::kNone);
    ASSERT_EQ(presentation.position_state(), cxxime_tsf::CandidatePositionState::kReady);
    ASSERT_TRUE(presentation.page().items.empty());
    ASSERT_EQ(presentation.page_current(), 0);
    ASSERT_EQ(presentation.page_total(), 0);
}

RUN_ALL_TESTS()
