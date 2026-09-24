// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>

#include <windows.h>

#include <cxxime/pinyin_resource.h>
#include <cxxime/pinyin_user_code.h>
#include <cxxime/spellings_index.h>

#include "support/testutil.h"

namespace {

std::string pinyin_user_code_temp_path() {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, "puc", 0, path) == 0) {
        return {};
    }
    return path;
}

} // namespace

TEST(PinyinUserCode, normalizes_canonical_pinyin_and_complete_shuangpin) {
    const std::string spellings_path = pinyin_user_code_temp_path();
    ASSERT_TRUE(!spellings_path.empty());
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path, {{"ni", "ni", cxxime::kNormalSpelling, 0.0f},
                         {"hk", "hao", cxxime::kNormalSpelling, 0.0f},
                         {"y;", "ying", cxxime::kNormalSpelling, 0.0f}}));
    auto pinyin_resources = cxxime::PinyinResourceSet::create(
        "microsoft_shuangpin", cxxime::PinyinSchemeKind::kShuangpin, spellings_path,
        cxxime::PinyinSpellingRequirement::kRequired);
    ASSERT_TRUE(pinyin_resources != nullptr);

    ASSERT_TRUE(cxxime::is_canonical_pinyin_user_code("nihao", "ni:hao"));
    ASSERT_TRUE(!cxxime::is_canonical_pinyin_user_code("nihk"));

    std::string code;
    std::string syllables;
    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("nihao", cxxime::PinyinSchemeKind::kShuangpin,
                                                      pinyin_resources.get(), &code, &syllables));
    ASSERT_EQ(code, "nihao");
    ASSERT_TRUE(syllables.empty());

    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("nihk", cxxime::PinyinSchemeKind::kShuangpin,
                                                      pinyin_resources.get(), &code, &syllables));
    ASSERT_EQ(code, "nihao");
    ASSERT_EQ(syllables, "ni:hao");

    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("y;", cxxime::PinyinSchemeKind::kShuangpin,
                                                      pinyin_resources.get(), &code, &syllables));
    ASSERT_EQ(code, "ying");
    ASSERT_EQ(syllables, "ying");
    ASSERT_TRUE(!cxxime::canonicalize_pinyin_user_code("nih", cxxime::PinyinSchemeKind::kShuangpin,
                                                       pinyin_resources.get(), &code, &syllables));

    pinyin_resources.reset();
    DeleteFileA(spellings_path.c_str());
}
