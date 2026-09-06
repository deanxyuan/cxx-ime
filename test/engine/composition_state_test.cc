// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>
#include <utility>

#include <cxxime/composition_presentation.h>
#include <cxxime/composition_state.h>
#include <cxxime/context.h>
#include <cxxime/input_limits.h>
#include <cxxime/translation_result.h>

#include "support/testutil.h"

namespace {

cxxime::TextSelectionAction make_selection(std::string text, std::size_t consumed) {
    cxxime::TextSelectionAction action;
    action.text = std::move(text);
    action.consumed_input_bytes = consumed;
    action.variants.push_back({});
    return action;
}

} // namespace

TEST(CompositionState, confirms_prefix_and_derives_single_presentation) {
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input("huaruijishu", 11));
    ASSERT_TRUE(state.confirm_prefix(make_selection("华锐", 6)));

    ASSERT_EQ(state.converted_segments().size(), 1u);
    ASSERT_EQ(state.converted_segments()[0].raw_input, "huarui");
    ASSERT_EQ(state.active().input, "jishu");
    ASSERT_EQ(state.active().cursor, 5u);

    const cxxime::CompositionPresentation presentation =
        cxxime::derive_composition_presentation(state);
    ASSERT_EQ(presentation.logical_preedit, "华锐jishu");
    ASSERT_EQ(presentation.preview_preedit, presentation.logical_preedit);
    ASSERT_EQ(presentation.converted_prefix_bytes, std::string("华锐").size());
    ASSERT_EQ(presentation.cursor_bytes, presentation.logical_preedit.size());
    ASSERT_TRUE(presentation.fits_transport());
}

TEST(CompositionState, reopens_last_segment_at_active_boundary) {
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input("huaruijishu", 11));
    ASSERT_TRUE(state.confirm_prefix(make_selection("华锐", 6)));
    ASSERT_TRUE(state.move_cursor_home());
    ASSERT_TRUE(state.erase_before_cursor());

    ASSERT_TRUE(state.converted_segments().empty());
    ASSERT_EQ(state.active().input, "huaruijishu");
    ASSERT_EQ(state.active().cursor, 6u);
}

TEST(CompositionState, cursor_movement_does_not_change_translation_revision) {
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input("abc", 3));
    const uint64_t revision = state.revision();

    ASSERT_TRUE(state.move_cursor_left());
    ASSERT_TRUE(state.move_cursor_home());
    ASSERT_TRUE(state.move_cursor_right());
    ASSERT_TRUE(state.move_cursor_end());
    ASSERT_EQ(state.revision(), revision);
}

TEST(CompositionState, preserves_converted_prefix_across_active_scheme_changes) {
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input("huaruijishu", 11));
    ASSERT_TRUE(state.confirm_prefix(make_selection("华锐", 6)));
    ASSERT_TRUE(state.set_scheme(cxxime::CompositionScheme::kInlineAscii));
    ASSERT_TRUE(state.insert('+'));

    ASSERT_EQ(state.converted_segments().size(), 1u);
    ASSERT_EQ(state.active().scheme, cxxime::CompositionScheme::kInlineAscii);
    ASSERT_EQ(state.active().input, "jishu+");
}

TEST(CompositionState, rejects_input_and_presentation_capacity_overflow_atomically) {
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input(std::string(cxxime::kMaxInputCodeLength, 'a'),
                                       cxxime::kMaxInputCodeLength));
    const uint64_t input_revision = state.revision();
    ASSERT_TRUE(!state.insert('b'));
    ASSERT_EQ(state.revision(), input_revision);
    ASSERT_EQ(state.active().input.size(), cxxime::kMaxInputCodeLength);

    state.cancel();
    ASSERT_TRUE(state.set_active_input("abc", 3));
    const uint64_t presentation_revision = state.revision();
    ASSERT_TRUE(!state.confirm_prefix(make_selection(std::string(254, 'x'), 1)));
    ASSERT_EQ(state.revision(), presentation_revision);
    ASSERT_TRUE(state.converted_segments().empty());
    ASSERT_EQ(state.active().input, "abc");

    const uint64_t utf8_revision = state.revision();
    ASSERT_TRUE(!state.confirm_prefix(make_selection(std::string(253, 'x') + "中", 1)));
    ASSERT_EQ(state.revision(), utf8_revision);
    ASSERT_TRUE(state.converted_segments().empty());
    ASSERT_EQ(state.active().input, "abc");
}

TEST(CompositionState, finalize_and_cancel_leave_no_composition) {
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input("huaruijishu", 11));
    ASSERT_TRUE(state.confirm_prefix(make_selection("华锐", 6)));
    std::string output;
    ASSERT_TRUE(state.finalize_candidate(make_selection("技术", 5), output));
    ASSERT_EQ(output, "华锐技术");
    ASSERT_TRUE(!state.is_composing());

    ASSERT_TRUE(state.set_active_input("nihao", 5));
    state.cancel();
    ASSERT_TRUE(!state.is_composing());
}

TEST(CompositionState, context_restart_keeps_revision_monotonic) {
    cxxime::Context context;
    ASSERT_TRUE(context.start_composition(cxxime::CompositionScheme::kPinyin, "n", 1));
    context.reset();
    const uint64_t previous_revision = context.preedit_revision();

    ASSERT_TRUE(context.start_composition(cxxime::CompositionScheme::kSymbol, "\\", 1));
    ASSERT_GT(context.preedit_revision(), previous_revision);
}

TEST(CompositionState, candidate_presentation_keeps_hint_and_annotation_separate) {
    cxxime::TranslationResult result;
    cxxime::Candidate candidate;
    candidate.text = "华锐";
    cxxime::CandidateEntry entry = cxxime::make_text_candidate_entry(std::move(candidate), 6);
    entry.hint = "abcd";
    entry.annotation = "拼音·前段";
    result.entries.push_back(std::move(entry));

    const cxxime::CandidatePresentationPage page = result.presentation_page();
    ASSERT_EQ(page.items.size(), 1u);
    ASSERT_EQ(page.items[0].text, "华锐");
    ASSERT_EQ(page.items[0].hint, "abcd");
    ASSERT_EQ(page.items[0].annotation, "拼音·前段");
    ASSERT_EQ(cxxime::format_candidate_presentation(page.items[0]), "华锐(abcd)");
}
