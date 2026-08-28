// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "system_lexicon_inspector.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>

#include "pinyin_code_paths.h"

namespace cxxime {

namespace {

constexpr char kDictionaryMagic[] = "CXDIC\x02\x00\x00";
constexpr char kReverseIndexMagic[] = "CXRIDX\x00\x00";
constexpr uint32_t kDictionaryVersion = 2;
constexpr uint32_t kReverseIndexVersion = 1;
constexpr std::size_t kMaximumQueryResults = 128;

#pragma pack(push, 1)

struct DictionaryHeader {
    char magic[8];
    uint32_t version;
    uint32_t entry_count;
    uint32_t string_data_size;
    uint32_t entries_offset;
    uint32_t strings_offset;
};

struct DictionaryEntry {
    uint32_t code_offset;
    uint32_t text_offset;
    uint32_t code_length;
    uint32_t text_length;
    int32_t frequency;
};

struct ReverseIndexHeader {
    char magic[8];
    uint32_t version;
    uint32_t file_size;
    uint32_t entry_count;
    uint32_t source_entry_count;
    uint32_t source_string_size;
    uint32_t entry_ids_offset;
    uint32_t reserved;
};

#pragma pack(pop)

static_assert(sizeof(DictionaryHeader) == 28, "DictionaryHeader must be 28 bytes");
static_assert(sizeof(DictionaryEntry) == 20, "DictionaryEntry must be 20 bytes");
static_assert(sizeof(ReverseIndexHeader) == 36, "ReverseIndexHeader must be 36 bytes");

class ReadOnlyMapping {
public:
    ReadOnlyMapping() = default;
    ~ReadOnlyMapping() { close(); }

    ReadOnlyMapping(const ReadOnlyMapping&) = delete;
    ReadOnlyMapping& operator=(const ReadOnlyMapping&) = delete;

    bool open(const std::string& path) {
        close();
        file_ = CreateFileA(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(file_, &size) || size.QuadPart <= 0 ||
            static_cast<uint64_t>(size.QuadPart) >
                static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            close();
            return false;
        }

        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping_ == nullptr) {
            close();
            return false;
        }

        data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (data_ == nullptr) {
            close();
            return false;
        }
        size_ = static_cast<std::size_t>(size.QuadPart);
        return true;
    }

    void close() {
        if (data_ != nullptr) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_ != nullptr) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
        size_ = 0;
    }

    const uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    const uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

bool has_prefix(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string compact_code(std::string_view canonical_code) {
    std::string code;
    code.reserve(canonical_code.size());
    for (const char character : canonical_code) {
        if (character != ':') {
            code.push_back(character);
        }
    }
    return code;
}

bool is_lowercase_code_character(char character) { return character >= 'a' && character <= 'z'; }

} // namespace

class SystemLexiconInspector::Impl {
public:
    bool open(SystemLexiconType type, const std::string& dictionary_path,
              const std::string& reverse_index_path) {
        close();
        type_ = type;
        if (!dictionary_.open(dictionary_path)) {
            last_error_ = "Failed to map the system dictionary";
            return false;
        }
        if (!reverse_index_.open(reverse_index_path)) {
            last_error_ = "Failed to map the system reverse index";
            close_mappings();
            return false;
        }
        if (!validate()) {
            close_mappings();
            return false;
        }
        return true;
    }

    void close() {
        close_mappings();
        last_error_.clear();
    }

    bool is_open() const { return entries_ != nullptr && entry_ids_ != nullptr; }

    std::vector<SystemLexiconEntry> query_text(std::string_view text, SystemLexiconTextMatch match,
                                               std::size_t limit) const {
        last_error_.clear();
        std::vector<SystemLexiconEntry> results;
        limit = bounded_limit(limit);
        if (!is_open() || text.empty() || limit == 0) {
            return results;
        }

        uint32_t lower = 0;
        uint32_t upper = entry_count_;
        while (lower < upper) {
            const uint32_t middle = lower + (upper - lower) / 2;
            std::string_view middle_text;
            if (!try_entry_text(entry_ids_[middle], &middle_text)) {
                return {};
            }
            if (middle_text < text) {
                lower = middle + 1;
            } else {
                upper = middle;
            }
        }

        results.reserve(std::min<std::size_t>(limit, entry_count_ - lower));
        while (lower < entry_count_ && results.size() < limit) {
            const uint32_t entry_id = entry_ids_[lower++];
            std::string_view current_text;
            if (!try_entry_text(entry_id, &current_text)) {
                return {};
            }
            const bool matches = match == SystemLexiconTextMatch::kExact
                                     ? current_text == text
                                     : has_prefix(current_text, text);
            if (!matches) {
                break;
            }
            SystemLexiconEntry result;
            if (!try_materialize(entry_id, &result)) {
                return {};
            }
            results.push_back(std::move(result));
        }
        return results;
    }

    std::vector<SystemLexiconEntry> query_code_prefix(std::string_view code,
                                                      std::size_t limit) const {
        last_error_.clear();
        std::vector<SystemLexiconEntry> results;
        limit = bounded_limit(limit);
        if (!is_open() || code.empty() || limit == 0) {
            return results;
        }

        std::vector<std::string> canonical_codes;
        if (!normalize_codes(code, &canonical_codes)) {
            return results;
        }

        std::unordered_set<uint32_t> seen;
        for (const auto& canonical_code : canonical_codes) {
            uint32_t lower = 0;
            uint32_t upper = entry_count_;
            while (lower < upper) {
                const uint32_t middle = lower + (upper - lower) / 2;
                std::string_view middle_code;
                if (!try_entry_code(middle, &middle_code)) {
                    return {};
                }
                if (middle_code < canonical_code) {
                    lower = middle + 1;
                } else {
                    upper = middle;
                }
            }

            while (lower < entry_count_ && results.size() < limit) {
                std::string_view current_code;
                if (!try_entry_code(lower, &current_code)) {
                    return {};
                }
                if (!has_prefix(current_code, canonical_code)) {
                    break;
                }
                SystemLexiconEntry result;
                if (!try_materialize(lower++, &result)) {
                    return {};
                }
                if (seen.insert(result.entry_id).second) {
                    results.push_back(std::move(result));
                }
            }
            if (results.size() == limit) {
                break;
            }
        }
        return results;
    }

    const std::string& last_error() const { return last_error_; }

private:
    bool validate() {
        if (dictionary_.size() < sizeof(DictionaryHeader) ||
            reverse_index_.size() < sizeof(ReverseIndexHeader)) {
            last_error_ = "System lexicon file is smaller than its header";
            return false;
        }

        const auto* dictionary_header =
            reinterpret_cast<const DictionaryHeader*>(dictionary_.data());
        if (std::memcmp(dictionary_header->magic, kDictionaryMagic, 8) != 0 ||
            dictionary_header->version != kDictionaryVersion) {
            last_error_ = "System dictionary has an unsupported format";
            return false;
        }

        const uint64_t entries_size =
            static_cast<uint64_t>(dictionary_header->entry_count) * sizeof(DictionaryEntry);
        const uint64_t expected_strings_offset = sizeof(DictionaryHeader) + entries_size;
        const uint64_t expected_dictionary_size =
            expected_strings_offset + dictionary_header->string_data_size;
        if (dictionary_header->entries_offset != sizeof(DictionaryHeader) ||
            dictionary_header->strings_offset != expected_strings_offset ||
            expected_dictionary_size != dictionary_.size()) {
            last_error_ = "System dictionary has an invalid section layout";
            return false;
        }

        const auto* reverse_header =
            reinterpret_cast<const ReverseIndexHeader*>(reverse_index_.data());
        const uint64_t expected_reverse_size =
            sizeof(ReverseIndexHeader) +
            static_cast<uint64_t>(reverse_header->entry_count) * sizeof(uint32_t);
        if (std::memcmp(reverse_header->magic, kReverseIndexMagic, 8) != 0 ||
            reverse_header->version != kReverseIndexVersion ||
            reverse_header->file_size != reverse_index_.size() ||
            reverse_header->entry_ids_offset != sizeof(ReverseIndexHeader) ||
            reverse_header->reserved != 0 || expected_reverse_size != reverse_index_.size()) {
            last_error_ = "System reverse index has an invalid format";
            return false;
        }
        if (reverse_header->entry_count != dictionary_header->entry_count ||
            reverse_header->source_entry_count != dictionary_header->entry_count ||
            reverse_header->source_string_size != dictionary_header->string_data_size) {
            last_error_ = "System reverse index does not match its dictionary";
            return false;
        }

        entry_count_ = dictionary_header->entry_count;
        string_data_size_ = dictionary_header->string_data_size;
        entries_ = reinterpret_cast<const DictionaryEntry*>(dictionary_.data() +
                                                            dictionary_header->entries_offset);
        strings_ =
            reinterpret_cast<const char*>(dictionary_.data() + dictionary_header->strings_offset);
        entry_ids_ = reinterpret_cast<const uint32_t*>(reverse_index_.data() +
                                                       reverse_header->entry_ids_offset);

        return true;
    }

    bool normalize_codes(std::string_view code, std::vector<std::string>* normalized) const {
        normalized->clear();
        if (type_ == SystemLexiconType::kWubi) {
            for (const char character : code) {
                if (!is_lowercase_code_character(character)) {
                    return false;
                }
            }
            normalized->emplace_back(code);
            return true;
        }
        *normalized = pinyin_code_paths_.parse(code);
        return !normalized->empty();
    }

    bool try_entry_code(uint32_t entry_id, std::string_view* code) const {
        const DictionaryEntry* entry = try_entry(entry_id);
        if (entry == nullptr) {
            return false;
        }
        if (entry->code_offset > string_data_size_ ||
            entry->code_length > string_data_size_ - entry->code_offset) {
            last_error_ = "System dictionary contains an invalid code range";
            return false;
        }
        *code = std::string_view(strings_ + entry->code_offset, entry->code_length);
        return true;
    }

    bool try_entry_text(uint32_t entry_id, std::string_view* text) const {
        const DictionaryEntry* entry = try_entry(entry_id);
        if (entry == nullptr) {
            return false;
        }
        if (entry->text_offset > string_data_size_ ||
            entry->text_length > string_data_size_ - entry->text_offset) {
            last_error_ = "System dictionary contains an invalid text range";
            return false;
        }
        *text = std::string_view(strings_ + entry->text_offset, entry->text_length);
        return true;
    }

    const DictionaryEntry* try_entry(uint32_t entry_id) const {
        if (entry_id >= entry_count_) {
            last_error_ = "System reverse index contains an out-of-range entry ID";
            return nullptr;
        }
        return &entries_[entry_id];
    }

    bool try_materialize(uint32_t entry_id, SystemLexiconEntry* result) const {
        const DictionaryEntry* source = try_entry(entry_id);
        std::string_view code;
        std::string_view text;
        if (source == nullptr || !try_entry_code(entry_id, &code) ||
            !try_entry_text(entry_id, &text)) {
            return false;
        }
        result->text.assign(text);
        result->code = compact_code(code);
        result->syllables.assign(code);
        result->frequency = source->frequency;
        result->entry_id = entry_id;
        return true;
    }

    static std::size_t bounded_limit(std::size_t limit) {
        return std::min(limit, kMaximumQueryResults);
    }

    void close_mappings() {
        entries_ = nullptr;
        strings_ = nullptr;
        entry_ids_ = nullptr;
        entry_count_ = 0;
        string_data_size_ = 0;
        reverse_index_.close();
        dictionary_.close();
    }

    SystemLexiconType type_ = SystemLexiconType::kPinyin;
    ReadOnlyMapping dictionary_;
    ReadOnlyMapping reverse_index_;
    const DictionaryEntry* entries_ = nullptr;
    const char* strings_ = nullptr;
    const uint32_t* entry_ids_ = nullptr;
    uint32_t entry_count_ = 0;
    uint32_t string_data_size_ = 0;
    mutable settings::PinyinCodePaths pinyin_code_paths_;
    mutable std::string last_error_;
};

SystemLexiconInspector::SystemLexiconInspector()
    : impl_(std::make_unique<Impl>()) {}

SystemLexiconInspector::~SystemLexiconInspector() = default;

bool SystemLexiconInspector::open(SystemLexiconType type, const std::string& dictionary_path,
                                  const std::string& reverse_index_path) {
    return impl_->open(type, dictionary_path, reverse_index_path);
}

void SystemLexiconInspector::close() { impl_->close(); }

bool SystemLexiconInspector::is_open() const { return impl_->is_open(); }

std::vector<SystemLexiconEntry> SystemLexiconInspector::query_text(std::string_view text,
                                                                   SystemLexiconTextMatch match,
                                                                   std::size_t limit) const {
    return impl_->query_text(text, match, limit);
}

std::vector<SystemLexiconEntry> SystemLexiconInspector::query_code_prefix(std::string_view code,
                                                                          std::size_t limit) const {
    return impl_->query_code_prefix(code, limit);
}

const std::string& SystemLexiconInspector::last_error() const { return impl_->last_error(); }

} // namespace cxxime
