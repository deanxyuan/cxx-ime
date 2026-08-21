// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"

#include "edit_target.h"

namespace {

cxxime_tsf::EditTargetEvidence captured_selection() {
    cxxime_tsf::EditTargetEvidence evidence;
    evidence.request_hr = S_OK;
    evidence.session_hr = S_OK;
    evidence.selection_hr = S_OK;
    evidence.selection_count = 1;
    evidence.selection_available = true;
    return evidence;
}

} // namespace

TEST(EditTarget, unknown_when_inspection_failed) {
    cxxime_tsf::EditTargetEvidence evidence;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Unknown);

    evidence.request_hr = S_OK;
    evidence.session_hr = S_OK;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Unknown);
}

TEST(EditTarget, no_target_when_all_editing_evidence_is_absent) {
    const auto evidence = captured_selection();
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence),
        cxxime_tsf::EditTargetState::NoEditTarget);
}

TEST(EditTarget, unknown_when_focused_child_requires_provisional_composition) {
    auto evidence = captured_selection();
    evidence.context_is_focused_child = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Unknown);
}

TEST(EditTarget, no_target_on_shell_surface_without_editing_evidence) {
    auto evidence = captured_selection();
    evidence.context_is_focused_child = true;
    evidence.foreground_is_shell_window = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence),
              cxxime_tsf::EditTargetState::NoEditTarget);
}

TEST(EditTarget, editable_when_any_supported_evidence_is_present) {
    auto evidence = captured_selection();
    evidence.has_active_selection = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.has_input_scope = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.has_native_caret = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.has_meaningful_text_rect = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.foreground_is_shell_window = true;
    evidence.has_native_caret = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);
}

RUN_ALL_TESTS()
