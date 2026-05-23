// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <string>
#include <vector>
#include "preedit_mode.h"

TEST(PreeditMode, composition) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"\x4f60\x597d", L"\x6ce5\x597d"};

    auto d = cxxime_tsf::decide_preedit(true, "composition", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"nihao");
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, preview) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"\x4f60\x597d", L"\x6ce5\x597d"};

    auto d = cxxime_tsf::decide_preedit(true, "preview", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"\x4f60\x597d");
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, preview_all) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"\x4f60\x597d", L"\x6ce5\x597d"};

    auto d = cxxime_tsf::decide_preedit(true, "preview_all", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"\x4f60\x597d \x6ce5\x597d");
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, no_inline) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates = {L"\x4f60\x597d"};

    auto d = cxxime_tsf::decide_preedit(false, "composition", preedit, candidates);

    ASSERT_TRUE(!d.start_composition);
    ASSERT_TRUE(d.inline_text.empty());
    ASSERT_TRUE(d.show_preedit_in_popup);
}

TEST(PreeditMode, preview_no_candidates) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(true, "preview", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"nihao");
    ASSERT_TRUE(!d.show_preedit_in_popup);
}

TEST(PreeditMode, preview_all_no_candidates) {
    std::wstring preedit = L"nihao";
    std::vector<std::wstring> candidates;

    auto d = cxxime_tsf::decide_preedit(true, "preview_all", preedit, candidates);

    ASSERT_TRUE(d.start_composition);
    ASSERT_EQ(d.inline_text, L"nihao");
    ASSERT_TRUE(!d.show_preedit_in_popup);
}
