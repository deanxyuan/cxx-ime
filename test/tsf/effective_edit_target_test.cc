// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "support/testutil.h"

#include "effective_edit_target.h"

namespace {

cxxime_tsf::EffectiveEditTargetSnapshot target(std::uintptr_t document, std::uintptr_t context,
                                               std::uintptr_t view) {
    cxxime_tsf::EffectiveEditTargetSnapshot snapshot;
    snapshot.document_identity = document;
    snapshot.context_identity = context;
    snapshot.view_window = view;
    snapshot.editable = true;
    return snapshot;
}

} // namespace

TEST(EffectiveEditTarget, unchanged_when_target_and_bindings_match) {
    const auto current = target(1, 2, 3);
    const cxxime_tsf::EffectiveEditTargetBindings bindings;

    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(current, current, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kUnchanged);
}

TEST(EffectiveEditTarget, rebinds_when_any_target_identity_changes) {
    const auto current = target(1, 2, 3);
    const cxxime_tsf::EffectiveEditTargetBindings bindings;

    ASSERT_EQ(
        cxxime_tsf::classify_effective_edit_target_change(current, target(5, 2, 3), bindings),
        cxxime_tsf::EffectiveEditTargetAction::kRebind);
    ASSERT_EQ(
        cxxime_tsf::classify_effective_edit_target_change(current, target(1, 5, 3), bindings),
        cxxime_tsf::EffectiveEditTargetAction::kRebind);
    ASSERT_EQ(
        cxxime_tsf::classify_effective_edit_target_change(current, target(1, 2, 5), bindings),
        cxxime_tsf::EffectiveEditTargetAction::kRebind);
}

TEST(EffectiveEditTarget, repairs_matching_target_with_stale_bindings) {
    const auto current = target(1, 2, 3);
    cxxime_tsf::EffectiveEditTargetBindings bindings;
    bindings.candidate_document_matches = false;

    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(current, current, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kRepairUi);

    bindings.candidate_document_matches = true;
    bindings.input_state_matches = false;
    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(current, current, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kRepairUi);

    bindings.input_state_matches = true;
    bindings.target_resources_match = false;
    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(current, current, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kRepairUi);

    bindings.target_resources_match = true;
    bindings.layout_sink_matches = false;
    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(current, current, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kRepairUi);
}

TEST(EffectiveEditTarget, clears_bound_target_when_input_is_unavailable) {
    const auto current = target(1, 2, 3);
    cxxime_tsf::EffectiveEditTargetSnapshot unavailable;
    cxxime_tsf::EffectiveEditTargetBindings bindings;

    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(current, unavailable, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kClear);

    bindings.has_bound_resources = true;
    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(
                  cxxime_tsf::EffectiveEditTargetSnapshot(), unavailable, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kClear);
}

TEST(EffectiveEditTarget, ignores_repeated_clear_without_resources) {
    const cxxime_tsf::EffectiveEditTargetSnapshot unavailable;
    const cxxime_tsf::EffectiveEditTargetBindings bindings;

    ASSERT_EQ(cxxime_tsf::classify_effective_edit_target_change(unavailable, unavailable, bindings),
              cxxime_tsf::EffectiveEditTargetAction::kUnchanged);
}

RUN_ALL_TESTS()
