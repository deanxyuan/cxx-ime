// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <cxxime/dict.h>

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

TEST(Dict, open_close) {
    std::string path = make_temp_path("test_dict_open.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "\xe7\x9a\x84", 1000},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));
    ASSERT_TRUE(dict.is_open());
    dict.close();
    ASSERT_TRUE(!dict.is_open());

    DeleteFileA(path.c_str());
}

TEST(Dict, lookup) {
    std::string path = make_temp_path("test_dict_lookup.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "\xe7\x9a\x84", 1000},
        {"de:dao", "\xe5\xbe\x97\xe5\x88\xb0", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto results = dict.lookup("de", 10);
    ASSERT_GE(results.size(), 1u);

    bool found_de = false;
    for (const auto& c : results) {
        if (c.text == "\xe7\x9a\x84") found_de = true;
    }
    ASSERT_TRUE(found_de);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_syllables) {
    std::string path = make_temp_path("test_dict_syll.bin");
    cxxime::Dict::create_test_dict(path, {
        {"di:di", "\xe5\xbc\x9f\xe5\xbc\x9f", 500},
        {"da:da", "\xe5\xa4\xa7\xe5\xa4\xa7", 400},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    std::vector<std::string> syllables = {"di", "di"};
    auto results = dict.lookup_by_syllables(syllables, 10);
    ASSERT_GE(results.size(), 1u);

    bool found = false;
    for (const auto& c : results) {
        if (c.text == "\xe5\xbc\x9f\xe5\xbc\x9f") found = true;
    }
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_empty) {
    std::string path = make_temp_path("test_dict_empty.bin");
    cxxime::Dict::create_test_dict(path, {});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto results = dict.lookup("zzz", 10);
    ASSERT_EQ(results.size(), 0u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, reverse_lookup) {
    std::string path = make_temp_path("test_dict_rev.bin");
    cxxime::Dict::create_test_dict(path, {
        {"ni:hao", "\xe4\xbd\xa0\xe5\xa5\xbd", 800},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto code = dict.reverse_lookup("\xe4\xbd\xa0\xe5\xa5\xbd");
    ASSERT_TRUE(code == "ni:hao");

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_frequency) {
    std::string path = make_temp_path("test_dict_freq.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "\xe7\x9a\x84", 1000},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));
    ASSERT_TRUE(dict.open_user_dict(":memory:"));

    dict.update_frequency("\xe7\x9a\x84", "de");
    dict.update_frequency("\xe7\x9a\x84", "de");

    auto results = dict.lookup("de", 10);
    ASSERT_GE(results.size(), 1u);

    dict.close();
    DeleteFileA(path.c_str());
}

// Ensure temp_path is initialized before any Dict tests run
static bool _dict_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();
