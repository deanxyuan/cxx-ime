// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "wubi_prefix_index.h"

#include <cstring>
#include <limits>
#include <new>

#include <windows.h>

#include <cxxime/logging.h>

#include "wubi_prefix_index_format.h"

namespace cxxime {

namespace {

bool pack_code(std::string_view code, uint32_t* packed) {
    if (code.empty() || code.size() > kWubiPrefixIndexMaxCodeLength) {
        return false;
    }

    uint32_t value = 0;
    for (char character : code) {
        if (character < 'a' || character > 'z') {
            return false;
        }
        value = (value << 5) | static_cast<uint32_t>(character - 'a' + 1);
    }
    *packed = value;
    return true;
}

bool valid_packed_code(uint32_t packed) {
    uint32_t length = 0;
    while (packed != 0) {
        const uint32_t character = packed & 0x1f;
        if (character == 0 || character > 26 || ++length > kWubiPrefixIndexMaxCodeLength) {
            return false;
        }
        packed >>= 5;
    }
    return length != 0;
}

bool validate_index(const char* data, size_t size, uint32_t expected_dict_entry_count,
                    const WubiPrefixIndexKey** keys, const uint32_t** postings) {
    if (size < sizeof(WubiPrefixIndexHeader) ||
        size > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }

    const auto* header = reinterpret_cast<const WubiPrefixIndexHeader*>(data);
    if (std::memcmp(header->magic, kWubiPrefixIndexMagic, sizeof(header->magic)) != 0 ||
        header->version != kWubiPrefixIndexVersion ||
        header->header_size != sizeof(WubiPrefixIndexHeader) || header->file_size != size ||
        header->dict_entry_count != expected_dict_entry_count || header->key_count == 0 ||
        header->posting_count == 0 || header->max_code_length != kWubiPrefixIndexMaxCodeLength ||
        header->reserved != 0) {
        return false;
    }

    const uint64_t keys_size =
        static_cast<uint64_t>(header->key_count) * sizeof(WubiPrefixIndexKey);
    const uint64_t postings_size = static_cast<uint64_t>(header->posting_count) * sizeof(uint32_t);
    if (header->keys_offset != sizeof(WubiPrefixIndexHeader) ||
        keys_size > size - header->keys_offset ||
        header->postings_offset != header->keys_offset + keys_size ||
        postings_size != size - header->postings_offset) {
        return false;
    }

    const auto* parsed_keys =
        reinterpret_cast<const WubiPrefixIndexKey*>(data + header->keys_offset);
    const auto* parsed_postings = reinterpret_cast<const uint32_t*>(data + header->postings_offset);
    uint32_t previous_code = 0;
    uint32_t expected_posting_offset = 0;
    for (uint32_t index = 0; index < header->key_count; ++index) {
        const auto& key = parsed_keys[index];
        if (!valid_packed_code(key.packed_code) ||
            (index != 0 && key.packed_code <= previous_code) || key.posting_count == 0 ||
            key.posting_offset != expected_posting_offset ||
            key.posting_count > header->posting_count - key.posting_offset) {
            return false;
        }
        previous_code = key.packed_code;
        expected_posting_offset += key.posting_count;
    }
    if (expected_posting_offset != header->posting_count) {
        return false;
    }
    for (uint32_t index = 0; index < header->posting_count; ++index) {
        if (parsed_postings[index] >= expected_dict_entry_count) {
            return false;
        }
    }

    *keys = parsed_keys;
    *postings = parsed_postings;
    return true;
}

} // namespace

WubiPrefixIndex::~WubiPrefixIndex() { unload(); }

bool WubiPrefixIndex::load(const std::string& path, uint32_t expected_dict_entry_count) {
    unload();

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        CXXIME_LOG(L"WubiPrefixIndex::load CreateFileA FAILED path=%S", path.c_str());
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < sizeof(WubiPrefixIndexHeader) ||
        static_cast<uint64_t>(size.QuadPart) >
            static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
        CloseHandle(file);
        return false;
    }

    data_size_ = static_cast<size_t>(size.QuadPart);
    data_ = new (std::nothrow) char[data_size_];
    if (data_ == nullptr) {
        CloseHandle(file);
        return false;
    }

    DWORD bytes_read = 0;
    const BOOL read = ReadFile(file, data_, static_cast<DWORD>(data_size_), &bytes_read, nullptr);
    CloseHandle(file);
    if (!read || bytes_read != data_size_ ||
        !validate_index(data_, data_size_, expected_dict_entry_count, &keys_, &postings_)) {
        CXXIME_LOG(L"WubiPrefixIndex::load invalid index path=%S", path.c_str());
        unload();
        return false;
    }

    const auto* header = reinterpret_cast<const WubiPrefixIndexHeader*>(data_);
    key_count_ = header->key_count;
    CXXIME_LOG(L"WubiPrefixIndex::load OK keys=%u postings=%u", header->key_count,
               header->posting_count);
    return true;
}

void WubiPrefixIndex::unload() {
    delete[] data_;
    data_ = nullptr;
    data_size_ = 0;
    keys_ = nullptr;
    postings_ = nullptr;
    key_count_ = 0;
}

bool WubiPrefixIndex::find(std::string_view code, WubiPrefixMatch* match) const {
    if (match == nullptr) {
        return false;
    }
    *match = {};
    if (keys_ == nullptr) {
        return false;
    }

    uint32_t packed = 0;
    if (!pack_code(code, &packed)) {
        return false;
    }

    uint32_t lower = 0;
    uint32_t upper = key_count_;
    while (lower < upper) {
        const uint32_t middle = lower + (upper - lower) / 2;
        if (keys_[middle].packed_code < packed) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }
    if (lower == key_count_ || keys_[lower].packed_code != packed) {
        return false;
    }

    match->entry_indexes = postings_ + keys_[lower].posting_offset;
    match->count = keys_[lower].posting_count;
    return true;
}

} // namespace cxxime
