// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdio>
#include <fstream>
#include <string>

#include <windows.h>

#include <cxxime/symbol_table.h>

#include "util/testutil.h"

namespace {

std::string project_data_path(const char* name) { return std::string(CXXIME_DATA_DIR) + name; }

std::string temp_file_path(const char* name) {
    char temp_path[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp_path);
    return std::string(temp_path) + name;
}

} // namespace

TEST(SymbolTable, loads_categories_and_paginates) {
    cxxime::SymbolTable table;
    ASSERT_TRUE(table.load(project_data_path("symbols.json")));

    cxxime::CandidatePage first = table.translate("bd", 0, 7);
    ASSERT_EQ(first.total_count, 46);
    ASSERT_EQ(first.candidates.size(), 7u);
    ASSERT_EQ(first.candidates[0].text, "。");
    ASSERT_EQ(first.candidates[0].code, "/bd");
    ASSERT_EQ(first.candidates[0].source, cxxime::CandidateSource::kSymbol);

    cxxime::CandidatePage second = table.translate("bd", 1, 7);
    ASSERT_EQ(second.page_offset, 7);
    ASSERT_EQ(second.candidates.size(), 7u);
    ASSERT_NE(second.candidates[0].text, first.candidates[0].text);
    ASSERT_TRUE(table.translate("unknown", 0, 7).candidates.empty());
}

TEST(SymbolTable, lists_category_navigation_in_source_order) {
    cxxime::SymbolTable table;
    ASSERT_TRUE(table.load(project_data_path("symbols.json")));

    cxxime::CandidatePage first = table.translate("", 0, 7);
    ASSERT_EQ(first.total_count, 14);
    ASSERT_EQ(first.candidates.size(), 7u);
    ASSERT_EQ(first.candidates[0].text, "标点");
    ASSERT_EQ(first.candidates[0].comment, "/bd");
    ASSERT_EQ(first.candidates[0].code, "/bd");
    ASSERT_EQ(first.candidates[1].text, "数字序号");
    ASSERT_EQ(first.candidates[1].comment, "/sz");

    cxxime::CandidatePage second = table.translate("", 1, 7);
    ASSERT_EQ(second.page_offset, 7);
    ASSERT_EQ(second.candidates.size(), 7u);
    ASSERT_EQ(second.candidates[0].text, "电脑符号");
    ASSERT_EQ(second.candidates[0].comment, "/dn");

    cxxime::CandidatePage capped = table.translate("", 0, 10);
    ASSERT_EQ(capped.page_size, 9);
    ASSERT_EQ(capped.candidates.size(), 9u);
}

TEST(SymbolTable, every_published_category_has_candidates) {
    cxxime::SymbolTable table;
    ASSERT_TRUE(table.load(project_data_path("symbols.json")));

    const char* codes[] = {
        "bd", "sz", "sx", "jt", "xl", "ew", "rw", "dn", "dw", "hb", "ts", "zy", "py", "pp",
    };
    for (const char* code : codes) {
        ASSERT_TRUE(!table.translate(code, 0, 7).candidates.empty()) << code;
    }
}

TEST(SymbolTable, failed_reload_clears_previous_data) {
    const std::string invalid_path = temp_file_path("cxxime_invalid_symbols.json");
    {
        std::ofstream file(invalid_path, std::ios::binary | std::ios::trunc);
        file << "{\"version\":1,\"categories\":[]}";
    }

    cxxime::SymbolTable table;
    ASSERT_TRUE(table.load(project_data_path("symbols.json")));
    ASSERT_TRUE(!table.load(invalid_path));
    ASSERT_TRUE(table.empty());

    DeleteFileA(invalid_path.c_str());
}

RUN_ALL_TESTS()
