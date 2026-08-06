// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "wubi_index_test_data.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "wubi_prefix_index_format.h"

namespace cxxime::test {

namespace {

uint32_t pack_code(const std::string& code) {
    uint32_t packed = 0;
    for (char character : code) {
        packed = (packed << 5) | static_cast<uint32_t>(character - 'a' + 1);
    }
    return packed;
}

} // namespace

bool create_test_wubi_index(const std::string& path,
                            const std::vector<std::tuple<std::string, std::string, int>>& entries) {
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end());

    std::map<uint32_t, std::pair<size_t, std::vector<uint32_t>>> prefixes;
    for (uint32_t entry_index = 0; entry_index < sorted.size(); ++entry_index) {
        const auto& code = std::get<0>(sorted[entry_index]);
        if (code.empty() || code.size() > kWubiPrefixIndexMaxCodeLength ||
            std::any_of(code.begin(), code.end(),
                        [](char character) { return character < 'a' || character > 'z'; })) {
            return false;
        }
        for (size_t length = 1; length <= code.size(); ++length) {
            auto& prefix = prefixes[pack_code(code.substr(0, length))];
            prefix.first = length;
            prefix.second.push_back(entry_index);
        }
    }

    std::vector<WubiPrefixIndexKey> keys;
    std::vector<uint32_t> postings;
    for (auto& [packed_code, prefix] : prefixes) {
        const size_t prefix_length = prefix.first;
        auto& indexes = prefix.second;
        std::sort(indexes.begin(), indexes.end(), [&](uint32_t left_index, uint32_t right_index) {
            const auto& left = sorted[left_index];
            const auto& right = sorted[right_index];
            const bool left_exact = std::get<0>(left).size() == prefix_length;
            const bool right_exact = std::get<0>(right).size() == prefix_length;
            if (left_exact != right_exact) {
                return left_exact;
            }
            if (std::get<0>(left).size() != std::get<0>(right).size()) {
                return std::get<0>(left).size() < std::get<0>(right).size();
            }
            if (std::get<2>(left) != std::get<2>(right)) {
                return std::get<2>(left) > std::get<2>(right);
            }
            if (std::get<0>(left) != std::get<0>(right)) {
                return std::get<0>(left) < std::get<0>(right);
            }
            if (std::get<1>(left).size() != std::get<1>(right).size()) {
                return std::get<1>(left).size() < std::get<1>(right).size();
            }
            if (std::get<1>(left) != std::get<1>(right)) {
                return std::get<1>(left) < std::get<1>(right);
            }
            return left_index < right_index;
        });

        const uint32_t posting_offset = static_cast<uint32_t>(postings.size());
        std::unordered_set<std::string> seen_text;
        for (uint32_t entry_index : indexes) {
            if (seen_text.insert(std::get<1>(sorted[entry_index])).second) {
                postings.push_back(entry_index);
            }
        }
        const uint32_t posting_count =
            static_cast<uint32_t>(postings.size() - posting_offset);
        keys.push_back({packed_code, posting_offset, posting_count});
    }

    WubiPrefixIndexHeader header = {};
    std::memcpy(header.magic, kWubiPrefixIndexMagic, sizeof(header.magic));
    header.version = kWubiPrefixIndexVersion;
    header.header_size = sizeof(header);
    header.dict_entry_count = static_cast<uint32_t>(sorted.size());
    header.key_count = static_cast<uint32_t>(keys.size());
    header.posting_count = static_cast<uint32_t>(postings.size());
    header.keys_offset = sizeof(header);
    header.postings_offset =
        header.keys_offset + static_cast<uint32_t>(keys.size() * sizeof(WubiPrefixIndexKey));
    header.file_size =
        header.postings_offset + static_cast<uint32_t>(postings.size() * sizeof(uint32_t));
    header.max_code_length = kWubiPrefixIndexMaxCodeLength;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(keys.data()),
                 static_cast<std::streamsize>(keys.size() * sizeof(WubiPrefixIndexKey)));
    output.write(reinterpret_cast<const char*>(postings.data()),
                 static_cast<std::streamsize>(postings.size() * sizeof(uint32_t)));
    return static_cast<bool>(output);
}

} // namespace cxxime::test
