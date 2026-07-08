// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DICTIONARY_MANIFEST_H_
#define CXXIME_DICTIONARY_MANIFEST_H_

#include <cstdint>
#include <string>
#include <vector>

namespace cxxime {

struct DictionaryManifestFile {
    std::string role;
    std::string path;
    std::string absolute_path;
    std::string sha256;
    uint64_t size = 0;
    bool required = true;
};

struct DictionaryManifest {
    std::string manifest_path;
    std::string directory;
    std::string generation;
    std::vector<DictionaryManifestFile> files;

    const DictionaryManifestFile* find_role(const std::string& role) const;
};

std::string dictionary_manifest_path_for_dict(const std::string& dict_path);
std::string dictionary_manifest_default_path();

bool compute_file_sha256(const std::string& path, std::string& out, std::string* error = nullptr);
bool load_dictionary_manifest(const std::string& manifest_path,
                              DictionaryManifest& out,
                              std::string* error = nullptr);
bool validate_dictionary_manifest(const DictionaryManifest& manifest,
                                  std::string* error = nullptr);

} // namespace cxxime

#endif // CXXIME_DICTIONARY_MANIFEST_H_
