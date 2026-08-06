// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WUBI_PREFIX_INDEX_H_
#define CXXIME_WUBI_PREFIX_INDEX_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cxxime {

struct WubiPrefixIndexKey;

struct WubiPrefixMatch {
    const uint32_t* entry_indexes = nullptr;
    uint32_t count = 0;
};

class WubiPrefixIndex {
public:
    WubiPrefixIndex() = default;
    ~WubiPrefixIndex();
    WubiPrefixIndex(const WubiPrefixIndex&) = delete;
    WubiPrefixIndex& operator=(const WubiPrefixIndex&) = delete;

    bool load(const std::string& path, uint32_t expected_dict_entry_count);
    void unload();
    bool is_loaded() const { return data_ != nullptr; }
    bool find(std::string_view code, WubiPrefixMatch* match) const;

private:
    char* data_ = nullptr;
    size_t data_size_ = 0;
    const WubiPrefixIndexKey* keys_ = nullptr;
    const uint32_t* postings_ = nullptr;
    uint32_t key_count_ = 0;
};

} // namespace cxxime

#endif // CXXIME_WUBI_PREFIX_INDEX_H_
