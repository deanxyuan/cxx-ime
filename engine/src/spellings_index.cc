// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/spellings_index.h>
#include <cstring>
#include <algorithm>
#include <windows.h>
#include <cxxime/logging.h>

static const char SPELLINGS_MAGIC_V2[] = "CXSPL\x02\x00\x00";
static const char SPELLINGS_MAGIC_V1[] = "CXSPL\x01\x00\x00";
static const uint32_t NODE_HEADER_SIZE = 12;
static const uint32_t SPELLING_SIZE = 14;  // v2 per-node spelling
static const uint32_t CHILD_SIZE = 8;

namespace cxxime {

SpellingsIndex::~SpellingsIndex() {
    unload();
}

bool SpellingsIndex::load(const std::string& bin_path) {
    unload();
    CXXIME_LOG(L"SpellingsIndex::load path=%S", bin_path.c_str());

    HANDLE hFile = CreateFileA(bin_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        CXXIME_LOG(L"SpellingsIndex::load CreateFileA FAILED");
        return false;
    }

    LARGE_INTEGER li;
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 28) {
        CloseHandle(hFile);
        return false;
    }
    data_size_ = (size_t)li.QuadPart;

    HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) {
        CloseHandle(hFile);
        return false;
    }

    void* base = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    file_handle_ = hFile;
    mapping_handle_ = hMap;
    data_ = (const char*)base;

    // Detect format version
    if (std::memcmp(data_, SPELLINGS_MAGIC_V2, 8) == 0) {
        // v2: Patricia trie
        auto* hdr = (const SpellingsHeader*)data_;
        node_count_ = hdr->node_count;
        nodes_ = data_ + hdr->nodes_offset;
        strings_ = data_ + hdr->strings_offset;
        nodes_size_ = hdr->strings_offset - hdr->nodes_offset;
        is_trie_ = true;

        // Build offset table
        node_offsets_ = std::make_unique<uint32_t[]>(node_count_);
        const char* p = nodes_;
        for (uint32_t i = 0; i < node_count_; ++i) {
            node_offsets_[i] = (uint32_t)(p - nodes_);
            uint8_t ns = *(const uint8_t*)(p + 8);
            uint8_t nc = *(const uint8_t*)(p + 9);
            p += NODE_HEADER_SIZE + ns * SPELLING_SIZE + nc * CHILD_SIZE;
        }

        CXXIME_LOG(L"SpellingsIndex::load v2 trie nodes=%u", node_count_);
    } else if (std::memcmp(data_, SPELLINGS_MAGIC_V1, 8) == 0) {
        // v1: flat sorted array
        struct V1Header { char magic[8]; uint32_t version, entry_count, string_data_size, entries_offset, strings_offset; };
        auto* hdr = (const V1Header*)data_;
        flat_entries_ = (const SpellingEntryV1*)(data_ + hdr->entries_offset);
        flat_entry_count_ = hdr->entry_count;
        strings_ = data_ + hdr->strings_offset;
        node_count_ = flat_entry_count_;  // for has_spellings()
        is_trie_ = false;

        CXXIME_LOG(L"SpellingsIndex::load v1 flat entries=%u", flat_entry_count_);
    } else {
        CXXIME_LOG(L"SpellingsIndex::load bad magic");
        unload();
        return false;
    }


    return node_count_ > 0;
}

void SpellingsIndex::unload() {
    if (data_) {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }
    if (mapping_handle_) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_ && file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
    }
    nodes_ = nullptr;
    strings_ = nullptr;
    node_count_ = 0;
    nodes_size_ = 0;
    data_size_ = 0;
    node_offsets_.reset();
    flat_entries_ = nullptr;
    flat_entry_count_ = 0;
    is_trie_ = false;
}

// v2 trie prefix search: O(k) walk
static std::vector<SpellingMatch> trie_prefix_search(
    const char* nodes, const char* strings, const uint32_t* node_offsets,
    uint32_t node_count, const std::string& prefix) {

    std::vector<SpellingMatch> results;
    const uint32_t prefix_len = (uint32_t)prefix.size();
    const char* prefix_data = prefix.data();
    uint32_t prefix_pos = 0;
    uint32_t current_node = 0;

    while (current_node < node_count) {
        const char* node = nodes + node_offsets[current_node];
        uint32_t key_len = *(const uint32_t*)(node + 4);
        uint8_t ns = *(const uint8_t*)(node + 8);
        uint8_t nc = *(const uint8_t*)(node + 9);

        if (prefix_pos + key_len > prefix_len)
            break;

        const char* key = strings + *(const uint32_t*)(node);
        if (key_len > 0 && std::memcmp(key, prefix_data + prefix_pos, key_len) != 0)
            break;

        prefix_pos += key_len;

        // Collect spellings
        const char* sp = node + NODE_HEADER_SIZE;
        for (uint8_t i = 0; i < ns; ++i) {
            SpellingMatch m;
            m.syllable.assign(strings + *(const uint32_t*)(sp),
                              *(const uint32_t*)(sp + 4));
            m.type = *(const uint8_t*)(sp + 8);
            m.credibility = *(const float*)(sp + 10);
            m.input_key_len = key_len;
            results.push_back(std::move(m));
            sp += SPELLING_SIZE;
        }

        if (prefix_pos >= prefix_len)
            break;

        // Find child
        uint8_t next_char = (uint8_t)prefix_data[prefix_pos];
        const char* ch = sp;
        bool found = false;
        for (uint8_t i = 0; i < nc; ++i) {
            if (*(const uint8_t*)(ch) == next_char) {
                current_node = *(const uint32_t*)(ch + 4);
                found = true;
                break;
            }
            ch += CHILD_SIZE;
        }
        if (!found)
            break;
    }

    return results;
}

// v1 flat array prefix search: binary search + scan
static std::vector<SpellingMatch> flat_prefix_search(
    const SpellingEntryV1* entries, uint32_t entry_count,
    const char* strings, const std::string& prefix) {

    std::vector<SpellingMatch> results;
    const uint32_t prefix_len = (uint32_t)prefix.size();
    const char* prefix_data = prefix.data();

    // Find first entry whose key starts with prefix[0]
    uint32_t lo = 0, hi = entry_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const char* key = strings + entries[mid].key_offset;
        if ((uint8_t)key[0] < (uint8_t)prefix_data[0])
            lo = mid + 1;
        else
            hi = mid;
    }

    for (uint32_t i = lo; i < entry_count; ++i) {
        const auto& e = entries[i];
        const char* key = strings + e.key_offset;
        if ((uint8_t)key[0] != (uint8_t)prefix_data[0])
            break;
        if (e.key_len > prefix_len)
            continue;
        if (std::memcmp(key, prefix_data, e.key_len) != 0)
            continue;

        SpellingMatch m;
        m.syllable.assign(strings + e.syllable_offset, e.syllable_len);
        m.type = e.type;
        m.credibility = e.credibility;
        m.input_key_len = e.key_len;
        results.push_back(std::move(m));
    }

    return results;
}

std::vector<SpellingMatch> SpellingsIndex::prefix_search(const std::string& prefix) const {
    if (!data_ || prefix.empty())
        return {};

    if (is_trie_)
        return trie_prefix_search(nodes_, strings_, node_offsets_.get(), node_count_, prefix);
    else
        return flat_prefix_search(flat_entries_, flat_entry_count_, strings_, prefix);
}

bool SpellingsIndex::create_test_trie(const std::string& path,
    const std::vector<std::tuple<std::string, std::string, int, float>>& entries) {

    // Build a simple trie in memory
    struct Node {
        std::string key;
        std::vector<std::tuple<std::string, int, float>> spellings;
        std::vector<std::pair<uint8_t, size_t>> children;  // (first_char, child_index)
    };
    std::vector<Node> nodes;
    nodes.push_back(Node{});  // root

    for (auto& [input, syll, stype, cred] : entries) {
        size_t cur = 0;
        size_t pos = 0;
        while (pos < input.size()) {
            uint8_t fc = (uint8_t)input[pos];
            bool found = false;
            for (auto& [c, ci] : nodes[cur].children) {
                if (c == fc) {
                    // Check how much of the node's key matches
                    auto& child = nodes[ci];
                    size_t match = 0;
                    while (pos + match < input.size() && match < child.key.size() &&
                           input[pos + match] == child.key[match])
                        ++match;

                    if (match == child.key.size()) {
                        // Fully consumed node key
                        pos += match;
                        cur = ci;
                        found = true;
                        break;
                    } else if (match > 0) {
                        // Partial match: split node
                        size_t split_idx = nodes.size();
                        nodes.push_back(Node{child.key.substr(0, match)});
                        auto& split = nodes[split_idx];

                        // Old child becomes sub-child
                        size_t old_idx = nodes.size();
                        nodes.push_back(Node{child.key.substr(match), child.spellings, child.children});
                        split.children.push_back({(uint8_t)child.key[match], old_idx});

                        // Replace old child with split
                        for (auto& [cc, ci2] : nodes[cur].children) {
                            if (ci2 == ci) { ci2 = split_idx; break; }
                        }

                        pos += match;
                        cur = split_idx;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                // New child
                size_t new_idx = nodes.size();
                nodes.push_back(Node{input.substr(pos)});
                nodes[cur].children.push_back({fc, new_idx});
                cur = new_idx;
                pos = input.size();
            }
        }
        nodes[cur].spellings.push_back({syll, stype, cred});
    }

    // Serialize
    std::string string_data;
    auto intern = [&string_data](const std::string& s) -> std::pair<uint32_t, uint32_t> {
        uint32_t off = (uint32_t)string_data.size();
        string_data += s;
        return {off, (uint32_t)s.size()};
    };

    // Assign indices via BFS
    std::vector<size_t> order;
    std::vector<size_t> q = {0};
    while (!q.empty()) {
        size_t n = q.front(); q.erase(q.begin());
        order.push_back(n);
        for (auto& [fc, ci] : nodes[n].children)
            q.push_back(ci);
    }

    std::vector<uint32_t> node_idx(nodes.size());
    for (uint32_t i = 0; i < order.size(); ++i)
        node_idx[order[i]] = i;

    // Write binary
    std::string node_data;
    for (size_t idx : order) {
        auto& n = nodes[idx];
        auto [ko, kl] = intern(n.key);
        uint8_t ns = (uint8_t)n.spellings.size();
        uint8_t nc = (uint8_t)n.children.size();

        // Header
        node_data.append((const char*)&ko, 4);
        node_data.append((const char*)&kl, 4);
        node_data += (char)ns;
        node_data += (char)nc;
        node_data += '\0';
        node_data += '\0';

        // Spellings
        for (auto& [syll, stype, cred] : n.spellings) {
            auto [so, sl] = intern(syll);
            node_data.append((const char*)&so, 4);
            node_data.append((const char*)&sl, 4);
            node_data += (char)(uint8_t)stype;
            node_data += '\0';
            node_data.append((const char*)&cred, 4);
        }

        // Children
        for (auto& [fc, ci] : n.children) {
            uint32_t ni = node_idx[ci];
            node_data += (char)fc;
            node_data += '\0';
            node_data += '\0';
            node_data += '\0';
            node_data.append((const char*)&ni, 4);
        }
    }

    uint32_t entries_offset = 28;  // header size
    uint32_t strings_offset = entries_offset + (uint32_t)node_data.size();

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD written;
    char hdr[28];
    memcpy(hdr, SPELLINGS_MAGIC_V2, 8);
    uint32_t ver = 2;
    memcpy(hdr + 8, &ver, 4);
    uint32_t nc = (uint32_t)nodes.size();
    uint32_t sdsize = (uint32_t)string_data.size();
    memcpy(hdr + 12, &nc, 4);
    memcpy(hdr + 16, &sdsize, 4);
    memcpy(hdr + 20, &entries_offset, 4);
    memcpy(hdr + 24, &strings_offset, 4);
    WriteFile(hFile, hdr, 28, &written, nullptr);
    WriteFile(hFile, node_data.data(), (DWORD)node_data.size(), &written, nullptr);
    WriteFile(hFile, string_data.data(), (DWORD)string_data.size(), &written, nullptr);
    CloseHandle(hFile);
    return true;
}

} // namespace cxxime
