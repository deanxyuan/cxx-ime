// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "lexicon_panel_model.h"

#include <string_view>
#include <unordered_map>
#include <vector>

#include "util/testutil.h"

namespace {

using cxxime::SystemLexiconEntry;

TEST(LexiconPanelModel, ClassifiesSearchWithoutGuessingInvalidInput) {
    ASSERT_EQ(cxxime::settings::LexiconSearchKind::kEmpty,
              cxxime::settings::classify_lexicon_search(""));
    ASSERT_EQ(cxxime::settings::LexiconSearchKind::kText,
              cxxime::settings::classify_lexicon_search(u8"撤单"));
    ASSERT_EQ(cxxime::settings::LexiconSearchKind::kCode,
              cxxime::settings::classify_lexicon_search("che'dan"));
    ASSERT_EQ(cxxime::settings::LexiconSearchKind::kInvalid,
              cxxime::settings::classify_lexicon_search("Che Dan"));
    ASSERT_EQ(cxxime::settings::LexiconSearchKind::kInvalid,
              cxxime::settings::classify_lexicon_search(u8"撤dan"));
}

TEST(LexiconPanelModel, PrefillsOnlyConfirmedMissingTextSearches) {
    using cxxime::settings::LexiconPanelEntry;
    using cxxime::settings::LexiconSearchKind;
    using cxxime::settings::should_prefill_new_lexicon_entry;

    const std::vector<LexiconPanelEntry> prefix_only = {{u8"撤单", "chedan"}};
    const std::vector<LexiconPanelEntry> exact = {{u8"撤", "che"}, {u8"撤单", "chedan"}};
    ASSERT_TRUE(should_prefill_new_lexicon_entry(LexiconSearchKind::kText, u8"撤", false, true,
                                                 prefix_only));
    ASSERT_TRUE(
        !should_prefill_new_lexicon_entry(LexiconSearchKind::kText, u8"撤", false, true, exact));
    ASSERT_TRUE(
        !should_prefill_new_lexicon_entry(LexiconSearchKind::kCode, "che", false, true, {}));
    ASSERT_TRUE(
        !should_prefill_new_lexicon_entry(LexiconSearchKind::kText, u8"撤", true, true, {}));
    ASSERT_TRUE(
        !should_prefill_new_lexicon_entry(LexiconSearchKind::kText, u8"撤", false, false, {}));
}

TEST(LexiconPanelModel, MergesSourcesAndPreservesSystemDisabledState) {
    std::vector<SystemLexiconEntry> system = {{u8"撤单", "chedan", 100, 1},
                                              {u8"撤单", "cedan", 80, 2}};
    std::vector<cxxime::UserDictEntryInfo> user = {{u8"撤单", "chedan", 5, 0},
                                                   {u8"撤单", "ched", 3, 0}};
    std::vector<cxxime::UserDictEntryInfo> disabled = {{u8"撤单", "", 1, 0}};

    const auto rows = cxxime::settings::merge_lexicon_entries(system, user, disabled);
    ASSERT_EQ(3u, rows.size());
    ASSERT_TRUE(rows[0].has_system_source);
    ASSERT_TRUE(rows[0].has_user_source);
    ASSERT_TRUE(rows[0].system_disabled);
    ASSERT_TRUE(!rows[1].has_system_source);
    ASSERT_TRUE(rows[1].has_user_source);
    ASSERT_TRUE(!rows[1].system_disabled);
    ASSERT_TRUE(rows[2].has_system_source);
    ASSERT_TRUE(rows[2].system_disabled);
}

TEST(LexiconPanelModel, UsesAuthoritativeWholePhraseCodesFirst) {
    auto lookup = [](std::string_view text, std::size_t) {
        if (text == u8"撤单") {
            return std::vector<SystemLexiconEntry>{{u8"撤单", "cedan", 10, 1},
                                                   {u8"撤单", "chedan", 100, 2}};
        }
        return std::vector<SystemLexiconEntry>{};
    };

    const auto codes = cxxime::settings::generate_lexicon_code_suggestions(
        cxxime::SystemLexiconType::kPinyin, u8"撤单", lookup, 4);
    ASSERT_EQ(2u, codes.size());
    ASSERT_EQ("chedan", codes[0]);
    ASSERT_EQ("cedan", codes[1]);
}

TEST(LexiconPanelModel, UsesCompleteWubiCodeForSingleCharacter) {
    auto lookup = [](std::string_view text, std::size_t) {
        if (text == u8"在") {
            return std::vector<SystemLexiconEntry>{{u8"在", "d", 1000, 1},
                                                   {u8"在", "dhfd", 100, 2}};
        }
        return std::vector<SystemLexiconEntry>{};
    };

    const auto codes = cxxime::settings::generate_lexicon_code_suggestions(
        cxxime::SystemLexiconType::kWubi, u8"在", lookup, 4);
    ASSERT_EQ(codes.size(), 1u);
    ASSERT_EQ(codes[0], "dhfd");
}

TEST(LexiconPanelModel, GeneratesBoundedPinyinAndWubiPhraseCodes) {
    const std::unordered_map<std::string, std::vector<SystemLexiconEntry>> entries = {
        {u8"撤", {{u8"撤", "che", 100, 1}, {u8"撤", "ce", 10, 2}}},
        {u8"单", {{u8"单", "dan", 100, 3}}},
        {u8"甲", {{u8"甲", "l", 200, 4}, {u8"甲", "lhnh", 100, 5}}},
        {u8"乙", {{u8"乙", "n", 200, 6}, {u8"乙", "nnll", 100, 7}}},
        {u8"丙", {{u8"丙", "g", 200, 8}, {u8"丙", "gmwi", 100, 9}}},
        {u8"丁", {{u8"丁", "s", 200, 10}, {u8"丁", "sgh", 100, 11}}},
    };
    auto lookup = [&](std::string_view text, std::size_t) {
        const auto found = entries.find(std::string(text));
        return found == entries.end() ? std::vector<SystemLexiconEntry>{} : found->second;
    };

    const auto pinyin = cxxime::settings::generate_lexicon_code_suggestions(
        cxxime::SystemLexiconType::kPinyin, u8"撤单", lookup, 2);
    ASSERT_EQ(2u, pinyin.size());
    ASSERT_EQ("chedan", pinyin[0]);
    ASSERT_EQ("cedan", pinyin[1]);

    const auto wubi = cxxime::settings::generate_lexicon_code_suggestions(
        cxxime::SystemLexiconType::kWubi, u8"甲乙丙丁", lookup, 4);
    ASSERT_EQ(1u, wubi.size());
    ASSERT_EQ("lngs", wubi[0]);
}

} // namespace

int main() { return test::RunAllTests(); }
