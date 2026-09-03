// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/dictionary_manifest.h>

#include "support/testutil.h"

namespace {

std::string make_temp_path() {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, "dmf", 0, path) == 0) {
        return {};
    }
    return path;
}

TEST(DictionaryManifest, validates_only_requested_runtime_files) {
    const std::string runtime_path = make_temp_path();
    ASSERT_TRUE(!runtime_path.empty());
    {
        std::ofstream output(runtime_path, std::ios::binary | std::ios::trunc);
        output << "runtime";
    }

    std::string runtime_hash;
    ASSERT_TRUE(cxxime::compute_file_sha256(runtime_path, runtime_hash));
    cxxime::DictionaryManifest manifest;
    manifest.files = {
        {"pinyin_dict", "runtime.bin", runtime_path, runtime_hash, 7, true},
        {"pinyin_reverse_index", "pinyin.reverse.idx", runtime_path + ".missing",
         std::string(64, '0'), 1, true},
    };

    std::string error;
    ASSERT_TRUE(cxxime::validate_dictionary_manifest_files(
        manifest, std::vector<std::string>{"pinyin_dict"}, &error));
    ASSERT_TRUE(!cxxime::validate_dictionary_manifest_files(
        manifest, std::vector<std::string>{"pinyin_reverse_index"}, &error));

    DeleteFileA(runtime_path.c_str());
}

} // namespace

RUN_ALL_TESTS()
