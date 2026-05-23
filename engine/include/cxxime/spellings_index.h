// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// SpellingsIndex — Prism layer: maps input strings to syllable interpretations.
// Loads from binary spellings.bin via memory-mapped file (Patricia trie format v2).

#ifndef CXXIME_SPELLINGS_INDEX_H_
#define CXXIME_SPELLINGS_INDEX_H_

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace cxxime {

enum SpellingType {
    kNormalSpelling = 0,
    kFuzzySpelling = 1,
    kAbbreviation = 2,
};

struct SpellingMatch {
    std::string syllable;
    int type = kNormalSpelling;
    float credibility = 0.0f;
};

#pragma pack(push, 1)

struct SpellingsHeader {
    char magic[8];       // "CXSPL\x02\0\0"
    uint32_t version;    // 2
    uint32_t node_count;
    uint32_t string_data_size;
    uint32_t nodes_offset;
    uint32_t strings_offset;
};

// Each trie node (variable size in file):
//   uint32_t key_offset, key_len
//   uint8_t  num_spellings, num_children
//   uint16_t padding
//   SpellingEntry[num_spellings]
//   ChildEntry[num_children]

// v2 trie per-node spelling
struct SpellingEntry {
    uint32_t syllable_offset;
    uint32_t syllable_len;
    uint8_t type;
    uint8_t padding;
    float credibility;
};

// v1 flat array entry (for backward compatibility)
struct SpellingEntryV1 {
    uint32_t key_offset;
    uint32_t syllable_offset;
    uint32_t key_len;
    uint32_t syllable_len;
    uint8_t type;
    uint8_t padding;
    float credibility;
};

struct ChildEntry {
    uint8_t first_char;
    uint8_t padding[3];
    uint32_t node_index;
};

#pragma pack(pop)

class SpellingsIndex {
public:
    SpellingsIndex() = default;
    ~SpellingsIndex();
    SpellingsIndex(const SpellingsIndex&) = delete;
    SpellingsIndex& operator=(const SpellingsIndex&) = delete;

    bool load(const std::string& bin_path);
    void unload();
    bool has_spellings() const { return node_count_ > 0; }

    // O(k) trie walk: returns all spellings where the stored key is a prefix of `prefix`.
    std::vector<SpellingMatch> prefix_search(const std::string& prefix) const;

    // For tests: create a v2 trie binary file from entries
    static bool create_test_trie(const std::string& path,
                                 const std::vector<std::tuple<std::string, std::string, int, float>>& entries);

private:
    const char* data_ = nullptr;
    size_t data_size_ = 0;
    const char* nodes_ = nullptr;      // raw node data
    const char* strings_ = nullptr;
    uint32_t node_count_ = 0;
    uint32_t nodes_size_ = 0;          // total bytes of node data

    void* file_handle_ = nullptr;
    void* mapping_handle_ = nullptr;

    // Pre-built offset table for O(1) node access (v2 trie)
    std::unique_ptr<uint32_t[]> node_offsets_;

    // v1 flat array fallback
    const SpellingEntryV1* flat_entries_ = nullptr;
    uint32_t flat_entry_count_ = 0;
    bool is_trie_ = false;
};

} // namespace cxxime

#endif // CXXIME_SPELLINGS_INDEX_H_
