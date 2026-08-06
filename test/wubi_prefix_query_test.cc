// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/dict.h>

#include "util/testutil.h"
#include "util/wubi_index_test_data.h"

namespace {

std::string temp_path(const char* filename) {
    char directory[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, directory);
    return std::string(directory) + filename;
}

} // namespace

TEST(WubiPrefixQuery, ranking_is_independent_of_query_limit) {
    const std::string dict_path = temp_path("cxxime_wubi_indexed_ranking.bin");
    const std::string index_path = dict_path + ".idx";
    const std::string user_path = dict_path + ".user.tsv";
    const std::vector<std::tuple<std::string, std::string, int>> entries = {
        {"d", "exact-primary", 20},
        {"d", "exact-secondary", 10},
        {"da", "left", 10},
        {"daaa", "long-high-frequency", 10000},
        {"db", "care", 10},
        {"dc", "friend", 10},
        {"dd", "large", 10},
        {"de", "beard", 10},
    };
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, entries));
    ASSERT_TRUE(cxxime::test::create_test_wubi_index(index_path, entries));
    DeleteFileA(user_path.c_str());

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_wubi_bundle(dict_path, user_path, index_path));
    ASSERT_TRUE(dict.has_wubi_prefix_index());

    const auto narrow = dict.lookup("d", 5);
    const auto wide = dict.lookup("d", 20);
    ASSERT_EQ(narrow.size(), 5u);
    ASSERT_GE(wide.size(), narrow.size());
    for (size_t index = 0; index < narrow.size(); ++index) {
        ASSERT_EQ(narrow[index].text, wide[index].text);
    }
    ASSERT_EQ(narrow[0].text, "exact-primary");
    ASSERT_EQ(narrow[1].text, "exact-secondary");
    ASSERT_EQ(narrow[2].text, "left");
    ASSERT_EQ(narrow[3].text, "care");
    ASSERT_EQ(narrow[4].text, "friend");

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(index_path.c_str());
    DeleteFileA(user_path.c_str());
}

RUN_ALL_TESTS()
