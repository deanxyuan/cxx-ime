// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>
#include <vector>

#include "composition_termination.h"
#include "preedit_mode.h"
#include "support/testutil.h"

namespace {

cxxime_tsf::PreeditText make_preedit(const std::wstring& text, size_t cursor = std::wstring::npos,
                                     size_t converted_prefix = 0, size_t focused_start = 0,
                                     size_t focused_end = 0) {
    cxxime_tsf::PreeditText preedit;
    preedit.text = text;
    preedit.cursor = cursor == std::wstring::npos ? text.size() : cursor;
    preedit.converted_prefix = converted_prefix;
    preedit.focused_start = focused_start;
    preedit.focused_end = focused_end;
    return preedit;
}

} // namespace

// -- inline_preedit=true: TSF composition active, popup complements inline content --

TEST(PreeditMode, inline_composition_displays_generated_syllable_boundaries) {
    const std::wstring logical = L"nihao";
    const std::wstring display = L"ni'hao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        true, "composition", make_preedit(logical, 2), make_preedit(display, 3),
        candidates, 0, true);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, display);
    ASSERT_EQ(d.inline_cursor, static_cast<std::size_t>(3));
    ASSERT_TRUE(d.host_termination_text.has_value());
    ASSERT_EQ(*d.host_termination_text, logical);
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, inline_preview) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        true, "preview", make_preedit(preedit), make_preedit(preedit), candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"你好");
    ASSERT_EQ(d.inline_cursor, d.inline_text.size());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, inline_preview_no_candidates) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(
        true, "preview", make_preedit(preedit), make_preedit(preedit), candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_EQ(d.inline_cursor, static_cast<size_t>(0));
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, segmented_preview_keeps_converted_prefix_and_previews_highlight) {
    const std::wstring preedit = L"华锐jishu";
    const std::vector<std::wstring> candidates = {L"技术", L"计数"};

    const auto decision = cxxime_tsf::decide_preedit(
        true, "preview", make_preedit(preedit, preedit.size(), 2),
        make_preedit(preedit, preedit.size(), 2), candidates, 1);

    ASSERT_EQ(decision.inline_text, std::wstring(L"华锐计数"));
    ASSERT_EQ(decision.inline_cursor, decision.inline_text.size());
    ASSERT_EQ(decision.inline_converted_prefix, static_cast<size_t>(2));
    ASSERT_TRUE(decision.show_preedit_in_popup);
}

TEST(PreeditMode, segmented_preview_keeps_active_code_without_candidates) {
    const std::wstring preedit = L"华锐jishu";
    const std::vector<std::wstring> candidates;

    const auto decision = cxxime_tsf::decide_preedit(
        true, "preview", make_preedit(preedit, preedit.size(), 2),
        make_preedit(preedit, preedit.size(), 2), candidates);

    ASSERT_EQ(decision.inline_text, preedit);
    ASSERT_EQ(decision.inline_cursor, preedit.size());
    ASSERT_EQ(decision.inline_converted_prefix, static_cast<size_t>(2));
    ASSERT_TRUE(decision.show_preedit_in_popup);
}

TEST(PreeditMode, commit_and_continue_waits_for_the_post_commit_caret) {
    ASSERT_TRUE(cxxime_tsf::should_defer_candidate_show(true, true, true));
    ASSERT_TRUE(cxxime_tsf::should_defer_candidate_show(false, false, false));
    ASSERT_TRUE(!cxxime_tsf::should_defer_candidate_show(false, true, false));
    ASSERT_TRUE(!cxxime_tsf::should_defer_candidate_show(false, false, true));
}

// -- inline_preedit=false: no TSF composition, candidate window shows raw input --

TEST(PreeditMode, no_inline_composition) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        false, "composition", make_preedit(preedit), make_preedit(preedit), candidates);

    ASSERT_TRUE(!d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, no_inline_preview) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        false, "preview", make_preedit(preedit), make_preedit(preedit), candidates);

    ASSERT_TRUE(!d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, composition_preserves_an_unmarked_apostrophe) {
    std::wstring preedit = L"don't";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(
        true, "composition", make_preedit(preedit, 2), make_preedit(preedit, 2), candidates);

    ASSERT_EQ(d.inline_text, preedit);
    ASSERT_EQ(d.inline_cursor, static_cast<size_t>(2));
    ASSERT_TRUE(!d.host_termination_text.has_value());
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, composition_displays_generated_boundaries_but_preserves_logical_text) {
    const std::wstring logical = L"华锐jishu";
    const std::wstring display = L"华锐ji'shu";
    const std::vector<std::wstring> candidates = {L"技术"};

    const auto decision = cxxime_tsf::decide_preedit(
        true, "composition", make_preedit(logical, 4, 2, 2, 4),
        make_preedit(display, 5, 2, 2, 4), candidates, 0, true);

    ASSERT_EQ(decision.inline_text, display);
    ASSERT_EQ(decision.inline_cursor, static_cast<std::size_t>(5));
    ASSERT_EQ(decision.inline_converted_prefix, static_cast<std::size_t>(2));
    ASSERT_EQ(decision.inline_focused_start, static_cast<std::size_t>(2));
    ASSERT_EQ(decision.inline_focused_end, static_cast<std::size_t>(4));
    ASSERT_TRUE(!decision.inline_focus_converted);
    ASSERT_EQ(decision.inline_text[decision.inline_focused_end], L'\'');
    ASSERT_TRUE(decision.host_termination_text.has_value());
    ASSERT_EQ(*decision.host_termination_text, logical);
}

TEST(PreeditMode, host_termination_normalizes_only_unchanged_display_text) {
    using Result = cxxime_tsf::HostTerminationTextResult;
    auto normalize = [](const std::wstring& current, bool read_succeeds, bool write_succeeds,
                        std::wstring* written) {
        return cxxime_tsf::normalize_host_termination_text(
            L"ni'hao", L"nihao",
            [&](std::wstring* text) {
                if (read_succeeds) {
                    *text = current;
                }
                return read_succeeds;
            },
            [&](const std::wstring& text) {
                if (write_succeeds) {
                    *written = text;
                }
                return write_succeeds;
            });
    };

    std::wstring written;
    ASSERT_EQ(normalize(L"ni'hao", true, true, &written), Result::kReplaced);
    ASSERT_EQ(written, std::wstring(L"nihao"));
    written.clear();
    ASSERT_EQ(normalize(L"ni'haox", true, true, &written), Result::kUnchanged);
    ASSERT_TRUE(written.empty());
    ASSERT_EQ(normalize(L"ni'hao", false, true, &written), Result::kReadFailed);
    ASSERT_TRUE(written.empty());
    ASSERT_EQ(normalize(L"ni'hao", true, false, &written), Result::kWriteFailed);
    ASSERT_TRUE(written.empty());
}

TEST(PreeditMode, preview_focuses_the_selected_candidate_text) {
    const std::wstring preedit = L"华锐ji'shu";
    const std::vector<std::wstring> candidates = {L"技术", L"计数"};

    const auto decision = cxxime_tsf::decide_preedit(
        true, "preview", make_preedit(L"华锐jishu", 7, 2, 2, 4),
        make_preedit(preedit, preedit.size(), 2, 2, 4), candidates, 1, true);

    ASSERT_EQ(decision.inline_text, std::wstring(L"华锐计数"));
    ASSERT_EQ(decision.inline_focused_start, static_cast<std::size_t>(2));
    ASSERT_EQ(decision.inline_focused_end, decision.inline_text.size());
    ASSERT_TRUE(decision.inline_focus_converted);
    ASSERT_TRUE(!decision.host_termination_text.has_value());
}

TEST(PreeditMode, preview_keeps_roles_stable_for_interior_cursor) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好"};

    auto d = cxxime_tsf::decide_preedit(
        true, "preview", make_preedit(preedit, 2), make_preedit(L"ni'hao", 3),
        candidates, 0, true);

    ASSERT_EQ(d.inline_text, L"你好");
    ASSERT_EQ(d.inline_cursor, d.inline_text.size());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, cursor_is_clamped_to_preedit_length) {
    std::wstring preedit = L"ni";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(
        true, "composition", make_preedit(preedit, 99), make_preedit(preedit, 99),
        candidates);

    ASSERT_EQ(d.inline_cursor, preedit.size());
}

TEST(PreeditMode, immersive_empty_composition_always_requires_placeholder) {
    ASSERT_TRUE(cxxime_tsf::empty_composition_requires_placeholder(true, false, L"", L""));
    ASSERT_TRUE(cxxime_tsf::empty_composition_requires_placeholder(true, true, L"", L""));
    ASSERT_TRUE(
        !cxxime_tsf::empty_composition_requires_placeholder(true, true, L"", L"composition"));
}

TEST(PreeditMode, desktop_placeholder_is_limited_to_nonempty_to_empty_transition) {
    ASSERT_TRUE(
        cxxime_tsf::empty_composition_requires_placeholder(false, true, L"composition", L""));
    ASSERT_TRUE(!cxxime_tsf::empty_composition_requires_placeholder(false, false, L"", L""));
    ASSERT_TRUE(!cxxime_tsf::empty_composition_requires_placeholder(false, true, L"", L""));
}

TEST(PreeditMode, empty_placeholder_waits_only_when_no_trusted_caret_is_available) {
    ASSERT_TRUE(cxxime_tsf::should_wait_for_composition_layout(true, false, false));
    ASSERT_TRUE(!cxxime_tsf::should_wait_for_composition_layout(false, false, false));
    ASSERT_TRUE(!cxxime_tsf::should_wait_for_composition_layout(true, true, false));
    ASSERT_TRUE(!cxxime_tsf::should_wait_for_composition_layout(true, false, true));
}

RUN_ALL_TESTS()
