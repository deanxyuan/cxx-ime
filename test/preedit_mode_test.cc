// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <string>
#include <vector>
#include "preedit_mode.h"

// -- inline_preedit=true: TSF composition active, candidate window hides preedit --

TEST(PreeditMode, inline_composition) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(true, "composition", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"nihao");
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, inline_preview) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(true, "preview", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"你好");
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, inline_preview_no_candidates) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(true, "preview", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"nihao");
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

// -- inline_preedit=false: no TSF composition, candidate window shows raw pinyin --

TEST(PreeditMode, no_inline_composition) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(false, "composition", preedit, candidates);

    ASSERT_TRUE(!d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, no_inline_preview) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"你好", L"泥好"};

    auto d = cxxime_tsf::decide_preedit(false, "preview", preedit, candidates);

    ASSERT_TRUE(!d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

RUN_ALL_TESTS()
