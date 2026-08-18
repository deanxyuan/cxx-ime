// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>
#include <vector>

#include "preedit_mode.h"
#include "util/testutil.h"

// -- inline_preedit=true: TSF composition active, popup complements inline content --

TEST(PreeditMode, inline_composition) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        true, "composition", preedit, preedit.size(), candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"nihao");
    ASSERT_EQ(d.inline_cursor, preedit.size());
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, inline_preview) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        true, "preview", preedit, preedit.size(), candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"你好");
    ASSERT_EQ(d.inline_cursor, d.inline_text.size());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, inline_preview_no_candidates) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(
        true, "preview", preedit, preedit.size(), candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_EQ(d.inline_cursor, static_cast<size_t>(0));
    ASSERT_TRUE(d.show_preedit_in_popup);
}

// -- inline_preedit=false: no TSF composition, candidate window shows raw input --

TEST(PreeditMode, no_inline_composition) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        false, "composition", preedit, preedit.size(), candidates);

    ASSERT_TRUE(!d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, no_inline_preview) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(
        false, "preview", preedit, preedit.size(), candidates);

    ASSERT_TRUE(!d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, composition_maps_cursor_directly) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好"};

    auto d = cxxime_tsf::decide_preedit(true, "composition", preedit, 2, candidates);

    ASSERT_EQ(d.inline_text, preedit);
    ASSERT_EQ(d.inline_cursor, static_cast<size_t>(2));
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, preview_keeps_roles_stable_for_interior_cursor) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好"};

    auto d = cxxime_tsf::decide_preedit(true, "preview", preedit, 2, candidates);

    ASSERT_EQ(d.inline_text, L"你好");
    ASSERT_EQ(d.inline_cursor, d.inline_text.size());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, cursor_is_clamped_to_preedit_length) {
    std::wstring preedit = L"ni";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(true, "composition", preedit, 99, candidates);

    ASSERT_EQ(d.inline_cursor, preedit.size());
}

TEST(PreeditMode, only_nonempty_to_empty_requires_placeholder) {
    ASSERT_TRUE(cxxime_tsf::composition_transition_requires_placeholder(L"你好", L""));
    ASSERT_TRUE(!cxxime_tsf::composition_transition_requires_placeholder(L"", L""));
    ASSERT_TRUE(!cxxime_tsf::composition_transition_requires_placeholder(L"", L"你好"));
    ASSERT_TRUE(!cxxime_tsf::composition_transition_requires_placeholder(L"你好", L"您好"));
}

RUN_ALL_TESTS()
