// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "candidate_store_file.h"

#include <cstring>

#include <fstream>
#include <limits>
#include <utility>

#include "binary_format.h"

namespace cxxime::topn {

namespace {

constexpr char kDictionaryMagic[] = "CXDIC\x02\x00\x00";

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace

bool CandidateStoreFile::load(const std::string& path, std::string* error) {
    data_.clear();
    view_ = {};

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        set_error(error, "cannot open candidate dictionary");
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end < static_cast<std::streamoff>(sizeof(DictHeader)) ||
        static_cast<uint64_t>(end) > std::numeric_limits<uint32_t>::max()) {
        set_error(error, "invalid candidate dictionary size");
        return false;
    }
    data_.resize(static_cast<size_t>(end));
    input.seekg(0);
    input.read(data_.data(), static_cast<std::streamsize>(data_.size()));
    if (!input) {
        set_error(error, "failed to read candidate dictionary");
        data_.clear();
        return false;
    }

    const auto* header = reinterpret_cast<const DictHeader*>(data_.data());
    const uint64_t entries_size = static_cast<uint64_t>(header->entry_count) * sizeof(DictEntry);
    if (std::memcmp(header->magic, kDictionaryMagic, sizeof(header->magic)) != 0 ||
        header->version != 2 || header->entry_count == 0 ||
        header->entries_offset != sizeof(DictHeader) ||
        entries_size > data_.size() - sizeof(DictHeader) ||
        header->strings_offset != sizeof(DictHeader) + entries_size ||
        header->string_data_size > data_.size() - header->strings_offset ||
        header->strings_offset + header->string_data_size != data_.size()) {
        set_error(error, "invalid candidate dictionary layout");
        data_.clear();
        return false;
    }

    view_.entries =
        reinterpret_cast<const CandidateStoreEntry*>(data_.data() + header->entries_offset);
    view_.strings = data_.data() + header->strings_offset;
    view_.entry_count = header->entry_count;
    view_.string_size = header->string_data_size;
    for (uint32_t i = 0; i < view_.entry_count; ++i) {
        if (!candidate_store_entry_valid(view_, i)) {
            set_error(error, "candidate dictionary contains an invalid entry");
            data_.clear();
            view_ = {};
            return false;
        }
    }
    view_.fingerprint = candidate_store_fingerprint(view_);
    return true;
}

CandidateStoreView CandidateStoreFile::view() const { return view_; }

} // namespace cxxime::topn
