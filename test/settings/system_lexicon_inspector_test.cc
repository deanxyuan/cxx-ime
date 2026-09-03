// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "system_lexicon_inspector.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "support/testutil.h"

namespace {

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

struct SourceEntry {
    std::string code;
    std::string text;
    int32_t frequency;
};

struct TestFiles {
    TestFiles(std::string dictionary_path, std::string reverse_index_path)
        : dictionary(std::move(dictionary_path))
        , reverse_index(std::move(reverse_index_path)) {}

    TestFiles(TestFiles&& other) noexcept
        : dictionary(std::move(other.dictionary))
        , reverse_index(std::move(other.reverse_index)) {}

    TestFiles(const TestFiles&) = delete;
    TestFiles& operator=(const TestFiles&) = delete;

    std::string dictionary;
    std::string reverse_index;

    ~TestFiles() {
        DeleteFileA(dictionary.c_str());
        DeleteFileA(reverse_index.c_str());
    }
};

std::string make_temp_path(const char* suffix) {
    char directory[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, directory);
    char name[MAX_PATH] = {};
    GetTempFileNameA(directory, "cxi", 0, name);
    DeleteFileA(name);
    return std::string(name) + suffix;
}

void write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input.good());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),
                                std::istreambuf_iterator<char>());
}

TestFiles create_test_files(std::vector<SourceEntry> source_entries) {
    std::stable_sort(source_entries.begin(), source_entries.end(),
                     [](const SourceEntry& left, const SourceEntry& right) {
                         if (left.code != right.code) {
                             return left.code < right.code;
                         }
                         return left.frequency > right.frequency;
                     });

    std::vector<DictionaryEntry> dictionary_entries;
    std::string strings;
    for (const SourceEntry& source : source_entries) {
        DictionaryEntry entry = {};
        entry.code_offset = static_cast<uint32_t>(strings.size());
        entry.code_length = static_cast<uint32_t>(source.code.size());
        strings.append(source.code);
        entry.text_offset = static_cast<uint32_t>(strings.size());
        entry.text_length = static_cast<uint32_t>(source.text.size());
        strings.append(source.text);
        entry.frequency = source.frequency;
        dictionary_entries.push_back(entry);
    }

    DictionaryHeader dictionary_header = {};
    std::memcpy(dictionary_header.magic, "CXDIC\x02\x00\x00", 8);
    dictionary_header.version = 2;
    dictionary_header.entry_count = static_cast<uint32_t>(dictionary_entries.size());
    dictionary_header.string_data_size = static_cast<uint32_t>(strings.size());
    dictionary_header.entries_offset = sizeof(DictionaryHeader);
    dictionary_header.strings_offset =
        dictionary_header.entries_offset +
        static_cast<uint32_t>(dictionary_entries.size() * sizeof(DictionaryEntry));

    std::vector<uint8_t> dictionary_bytes(dictionary_header.strings_offset + strings.size());
    std::memcpy(dictionary_bytes.data(), &dictionary_header, sizeof(dictionary_header));
    if (!dictionary_entries.empty()) {
        std::memcpy(dictionary_bytes.data() + dictionary_header.entries_offset,
                    dictionary_entries.data(), dictionary_entries.size() * sizeof(DictionaryEntry));
    }
    std::memcpy(dictionary_bytes.data() + dictionary_header.strings_offset, strings.data(),
                strings.size());

    std::vector<uint32_t> entry_ids(dictionary_entries.size());
    for (uint32_t entry_id = 0; entry_id < entry_ids.size(); ++entry_id) {
        entry_ids[entry_id] = entry_id;
    }
    std::sort(entry_ids.begin(), entry_ids.end(), [&](uint32_t left_id, uint32_t right_id) {
        const SourceEntry& left = source_entries[left_id];
        const SourceEntry& right = source_entries[right_id];
        if (left.text != right.text) {
            return left.text < right.text;
        }
        if (left.code != right.code) {
            return left.code < right.code;
        }
        if (left.frequency != right.frequency) {
            return left.frequency > right.frequency;
        }
        return left_id < right_id;
    });

    ReverseIndexHeader reverse_header = {};
    std::memcpy(reverse_header.magic, "CXRIDX\x00\x00", 8);
    reverse_header.version = 1;
    reverse_header.file_size =
        static_cast<uint32_t>(sizeof(ReverseIndexHeader) + entry_ids.size() * sizeof(uint32_t));
    reverse_header.entry_count = static_cast<uint32_t>(entry_ids.size());
    reverse_header.source_entry_count = static_cast<uint32_t>(entry_ids.size());
    reverse_header.source_string_size = static_cast<uint32_t>(strings.size());
    reverse_header.entry_ids_offset = sizeof(ReverseIndexHeader);

    std::vector<uint8_t> reverse_bytes(reverse_header.file_size);
    std::memcpy(reverse_bytes.data(), &reverse_header, sizeof(reverse_header));
    if (!entry_ids.empty()) {
        std::memcpy(reverse_bytes.data() + reverse_header.entry_ids_offset, entry_ids.data(),
                    entry_ids.size() * sizeof(uint32_t));
    }

    TestFiles files(make_temp_path(".dict.bin"), make_temp_path(".reverse.idx"));
    write_bytes(files.dictionary, dictionary_bytes);
    write_bytes(files.reverse_index, reverse_bytes);
    return files;
}

TEST(SystemLexiconInspector, queries_pinyin_by_text_and_compact_code) {
    TestFiles files = create_test_files({
        {"che:dan", "撤单", 500},
        {"che:dan", "扯淡", 100},
        {"che:di", "彻底", 400},
        {"ni:hao", "你好", 900},
        {"ni:hao", "你号", 50},
    });

    cxxime::SystemLexiconInspector inspector;
    ASSERT_TRUE(
        inspector.open(cxxime::SystemLexiconType::kPinyin, files.dictionary, files.reverse_index))
        << inspector.last_error();

    const auto exact = inspector.query_text("撤单", cxxime::SystemLexiconTextMatch::kExact, 10);
    ASSERT_EQ(exact.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(exact[0].code, "chedan");
    ASSERT_EQ(exact[0].syllables, "che:dan");
    ASSERT_EQ(exact[0].frequency, 500);

    const auto text_prefix =
        inspector.query_text("你", cxxime::SystemLexiconTextMatch::kPrefix, 10);
    ASSERT_EQ(text_prefix.size(), static_cast<std::size_t>(2));

    const auto exact_code = inspector.query_code_prefix("chedan", 10);
    ASSERT_EQ(exact_code.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(exact_code[0].text, "撤单");
    ASSERT_EQ(exact_code[1].text, "扯淡");

    const auto partial_code = inspector.query_code_prefix("ched", 10);
    ASSERT_EQ(partial_code.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(inspector.query_code_prefix("che'dan", 10).size(), static_cast<std::size_t>(2));
    ASSERT_EQ(inspector.query_code_prefix("che'", 10).size(), static_cast<std::size_t>(3));
    ASSERT_EQ(inspector.query_code_prefix("che", 1).size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(inspector.query_code_prefix("CHE", 10).empty());
}

TEST(SystemLexiconInspector, queries_all_bounded_pinyin_segmentations) {
    TestFiles files = create_test_files({{"xian", "先", 1000}, {"xi:an", "西安", 900}});

    cxxime::SystemLexiconInspector inspector;
    ASSERT_TRUE(
        inspector.open(cxxime::SystemLexiconType::kPinyin, files.dictionary, files.reverse_index));
    const auto ambiguous = inspector.query_code_prefix("xian", 10);
    ASSERT_EQ(ambiguous.size(), static_cast<std::size_t>(2));
    const auto explicit_boundary = inspector.query_code_prefix("xi'an", 10);
    ASSERT_EQ(explicit_boundary.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(explicit_boundary[0].text, "西安");
}

TEST(SystemLexiconInspector, queries_wubi_code_prefix_without_pinyin_segmentation) {
    TestFiles files = create_test_files({
        {"wq", "你", 500},
        {"wqa", "低", 300},
        {"wqb", "您", 200},
        {"xy", "级", 100},
    });

    cxxime::SystemLexiconInspector inspector;
    ASSERT_TRUE(
        inspector.open(cxxime::SystemLexiconType::kWubi, files.dictionary, files.reverse_index))
        << inspector.last_error();
    const auto matches = inspector.query_code_prefix("wq", 2);
    ASSERT_EQ(matches.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(matches[0].text, "你");
    ASSERT_EQ(matches[1].text, "低");
    ASSERT_TRUE(inspector.query_code_prefix("w/", 10).empty());
}

TEST(SystemLexiconInspector, rejects_invalid_header_or_mismatched_reverse_index) {
    TestFiles files = create_test_files({
        {"che:dan", "撤单", 500},
        {"ni:hao", "你好", 900},
    });
    const std::vector<uint8_t> valid_reverse = read_bytes(files.reverse_index);

    auto corrupt = valid_reverse;
    corrupt[0] = 'X';
    write_bytes(files.reverse_index, corrupt);
    cxxime::SystemLexiconInspector inspector;
    ASSERT_TRUE(
        !inspector.open(cxxime::SystemLexiconType::kPinyin, files.dictionary, files.reverse_index));

    corrupt = valid_reverse;
    auto* header = reinterpret_cast<ReverseIndexHeader*>(corrupt.data());
    ++header->source_string_size;
    write_bytes(files.reverse_index, corrupt);
    ASSERT_TRUE(
        !inspector.open(cxxime::SystemLexiconType::kPinyin, files.dictionary, files.reverse_index));
}

TEST(SystemLexiconInspector, rejects_an_out_of_range_id_when_query_accesses_it) {
    TestFiles files = create_test_files({
        {"che:dan", "撤单", 500},
        {"ni:hao", "你好", 900},
    });
    std::vector<uint8_t> reverse = read_bytes(files.reverse_index);
    auto* ids = reinterpret_cast<uint32_t*>(reverse.data() + sizeof(ReverseIndexHeader));
    ids[0] = UINT32_MAX;
    ids[1] = UINT32_MAX;
    write_bytes(files.reverse_index, reverse);

    cxxime::SystemLexiconInspector inspector;
    ASSERT_TRUE(
        inspector.open(cxxime::SystemLexiconType::kPinyin, files.dictionary, files.reverse_index));
    ASSERT_TRUE(inspector.query_text("撤单", cxxime::SystemLexiconTextMatch::kExact, 10).empty());
    ASSERT_TRUE(!inspector.last_error().empty());
}

TEST(SystemLexiconInspector, rejects_an_invalid_string_range_when_query_accesses_it) {
    TestFiles files = create_test_files({{"ni:hao", "你好", 900}});
    std::vector<uint8_t> dictionary = read_bytes(files.dictionary);
    auto* entry = reinterpret_cast<DictionaryEntry*>(dictionary.data() + sizeof(DictionaryHeader));
    entry->code_offset = UINT32_MAX;
    write_bytes(files.dictionary, dictionary);

    cxxime::SystemLexiconInspector inspector;
    ASSERT_TRUE(
        inspector.open(cxxime::SystemLexiconType::kPinyin, files.dictionary, files.reverse_index));
    ASSERT_TRUE(inspector.query_code_prefix("nihao", 10).empty());
    ASSERT_TRUE(!inspector.last_error().empty());
}

TEST(SystemLexiconInspector, close_releases_the_mapped_files) {
    TestFiles files = create_test_files({{"ni:hao", "你好", 900}});
    cxxime::SystemLexiconInspector inspector;
    ASSERT_TRUE(
        inspector.open(cxxime::SystemLexiconType::kPinyin, files.dictionary, files.reverse_index))
        << inspector.last_error();
    inspector.close();
    ASSERT_TRUE(!inspector.is_open());
    ASSERT_TRUE(DeleteFileA(files.dictionary.c_str()) != 0);
}

} // namespace

RUN_ALL_TESTS()
