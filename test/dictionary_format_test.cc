// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/dict.h>
#include <cxxime/spellings_index.h>

#include "util/testutil.h"
#include "util/topn_test_data.h"

namespace {

std::string make_temp_path(const char* name) {
    static const std::string directory = []() {
        char path[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, path);
        return std::string(path);
    }();
    return directory + name;
}

void write_u32(std::ofstream& output, uint32_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool write_empty_id_index(const std::string& path, uint32_t version) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write("CXIDX\0\0\0", 8);
    write_u32(output, version);
    for (int i = 0; i < 4; ++i) {
        write_u32(output, 0);
    }
    return output.good();
}

bool replace_magic_and_version(const std::string& path,
                               const char (&magic)[8],
                               uint32_t version) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        return false;
    }
    file.write(magic, sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    return file.good();
}

bool create_test_topn(const std::string& path) {
    const std::vector<cxxime::Candidate> candidates = {{"a", "", 100}};
    return cxxime::test::create_test_topn(path, {{"a", candidates}});
}

} // namespace

TEST(DictionaryFormat, accepts_current_versions) {
    const std::string dict_path = make_temp_path("format_current.dict.bin");
    const std::string index_path = make_temp_path("format_current.dict.idx");
    const std::string topn_path = make_temp_path("format_current.topn.bin");
    const std::string spellings_path = make_temp_path("format_current.spellings.bin");

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {{"a", "a", 100}}));
    ASSERT_TRUE(write_empty_id_index(index_path, 3));
    ASSERT_TRUE(create_test_topn(topn_path));
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path, {{"a", "a", cxxime::kNormalSpelling, 0.0f}}));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open_bundle(dict_path, "", index_path, topn_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));

    dictionary.close();
    spellings.unload();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(index_path.c_str());
    DeleteFileA(topn_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(DictionaryFormat, rejects_dict_v1) {
    const std::string path = make_temp_path("format_v1.dict.bin");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(path, {{"a", "a", 100}}));
    const char magic[8] = {'C', 'X', 'D', 'I', 'C', '\x01', '\0', '\0'};
    ASSERT_TRUE(replace_magic_and_version(path, magic, 1));

    cxxime::Dict dictionary;
    ASSERT_TRUE(!dictionary.open_dict(path));
    DeleteFileA(path.c_str());
}

TEST(DictionaryFormat, rejects_spellings_v1) {
    const std::string path = make_temp_path("format_v1.spellings.bin");
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        path, {{"a", "a", cxxime::kNormalSpelling, 0.0f}}));
    const char magic[8] = {'C', 'X', 'S', 'P', 'L', '\x01', '\0', '\0'};
    ASSERT_TRUE(replace_magic_and_version(path, magic, 1));

    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(!spellings.load(path));
    DeleteFileA(path.c_str());
}

TEST(DictionaryFormat, rejects_id_index_v2) {
    const std::string dict_path = make_temp_path("format_idx_v2.dict.bin");
    const std::string index_path = make_temp_path("format_idx_v2.dict.idx");
    const std::string topn_path = make_temp_path("format_idx_v2.topn.bin");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {{"a", "a", 100}}));
    ASSERT_TRUE(write_empty_id_index(index_path, 2));
    ASSERT_TRUE(create_test_topn(topn_path));

    cxxime::Dict dictionary;
    ASSERT_TRUE(!dictionary.open_bundle(dict_path, "", index_path, topn_path));
    DeleteFileA(dict_path.c_str());
    DeleteFileA(index_path.c_str());
    DeleteFileA(topn_path.c_str());
}

RUN_ALL_TESTS()
