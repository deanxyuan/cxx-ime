// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager_integration_test_support.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

char temp_path[MAX_PATH] = {};
std::string test_user_data_dir;

std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

std::string read_text_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static std::string basename_of(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

static std::vector<std::string> split_syllables(const std::string& code) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= code.size()) {
        size_t end = code.find(':', start);
        std::string item = end == std::string::npos
            ? code.substr(start)
            : code.substr(start, end - start);
        if (!item.empty())
            result.push_back(item);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return result;
}

static std::string input_key_for_code(const std::string& code) {
    std::string key;
    key.reserve(code.size());
    for (char c : code) {
        if (c != ':')
            key.push_back(c);
    }
    return key;
}

static void write_u32(std::ofstream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

static void write_test_id_index(const std::string& path,
                                const std::vector<TestDictEntry>& entries) {
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end());

    std::vector<std::string> syllabary;
    std::vector<std::pair<std::vector<uint32_t>, uint32_t>> id_entries;
    for (uint32_t entry_index = 0; entry_index < sorted.size(); ++entry_index) {
        const auto& code = std::get<0>(sorted[entry_index]);
        std::vector<uint32_t> ids;
        for (const auto& syllable : split_syllables(code)) {
            auto it = std::find(syllabary.begin(), syllabary.end(), syllable);
            if (it == syllabary.end()) {
                syllabary.push_back(syllable);
                ids.push_back(static_cast<uint32_t>(syllabary.size() - 1));
            } else {
                ids.push_back(static_cast<uint32_t>(it - syllabary.begin()));
            }
        }
        if (!ids.empty())
            id_entries.push_back({ids, entry_index});
    }
    std::sort(id_entries.begin(), id_entries.end());

    std::string syllable_data;
    std::vector<uint32_t> syllable_offsets;
    for (const auto& syllable : syllabary) {
        syllable_offsets.push_back(static_cast<uint32_t>(syllable_data.size()));
        syllable_data.append(syllable);
        syllable_data.push_back('\0');
    }

    std::string id_data;
    std::vector<uint32_t> id_offsets;
    for (const auto& entry : id_entries) {
        id_offsets.push_back(static_cast<uint32_t>(id_data.size()));
        uint32_t count = static_cast<uint32_t>(entry.first.size());
        id_data.append(reinterpret_cast<const char*>(&count), sizeof(count));
        for (uint32_t id : entry.first)
            id_data.append(reinterpret_cast<const char*>(&id), sizeof(id));
        id_data.append(reinterpret_cast<const char*>(&entry.second), sizeof(entry.second));
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out.write("CXIDX\0\0\0", 8);
    write_u32(out, 3);
    write_u32(out, static_cast<uint32_t>(syllabary.size()));
    write_u32(out, static_cast<uint32_t>(syllable_data.size()));
    write_u32(out, static_cast<uint32_t>(id_entries.size()));
    write_u32(out, static_cast<uint32_t>(id_data.size()));
    for (uint32_t offset : syllable_offsets)
        write_u32(out, offset);
    out.write(syllable_data.data(), static_cast<std::streamsize>(syllable_data.size()));
    for (uint32_t offset : id_offsets)
        write_u32(out, offset);
    out.write(id_data.data(), static_cast<std::streamsize>(id_data.size()));
    out.close();
    ASSERT_TRUE(out.good());
}

void write_manifest_for_files(const std::string& dict_path,
                              const std::vector<std::pair<const char*, std::string>>& files) {
    std::string manifest_path = cxxime::dictionary_manifest_path_for_dict(dict_path);
    std::string tmp_path = manifest_path + ".tmp";
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "{\n"
        << "  \"schema\": 1,\n"
        << "  \"generation\": \"test\",\n"
        << "  \"files\": [\n";
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& item = files[i];
        std::string hash;
        ASSERT_TRUE(cxxime::compute_file_sha256(item.second, hash));

        WIN32_FILE_ATTRIBUTE_DATA data = {};
        ASSERT_TRUE(GetFileAttributesExA(item.second.c_str(), GetFileExInfoStandard, &data));
        uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

        out << "    {\"role\":\"" << item.first << "\",\"path\":\""
            << basename_of(item.second) << "\",\"size\":" << size
            << ",\"sha256\":\"" << hash << "\",\"required\":true}";
        if (i + 1 < files.size())
            out << ",";
        out << "\n";
    }
    out << "  ]\n"
        << "}\n";
    out.close();
    ASSERT_TRUE(out.good());
    ASSERT_TRUE(MoveFileExA(tmp_path.c_str(), manifest_path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}

void create_test_dictionary_bundle(const std::string& dict_path,
                                          const std::vector<TestDictEntry>& entries) {
    std::string idx_path = dict_path + ".idx";
    std::string spellings_path = dict_path + ".spellings.bin";
    std::string topn_path = dict_path + ".topn.bin";
    std::string wubi_path = dict_path + ".wubi.bin";
    std::string wubi_prefix_index_path = wubi_path + ".idx";

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, entries));
    write_test_id_index(idx_path, entries);

    std::vector<std::tuple<std::string, std::string, int, float>> spellings;
    std::vector<std::pair<std::string, std::vector<cxxime::Candidate>>> topn;
    for (const auto& entry : entries) {
        std::string key = input_key_for_code(std::get<0>(entry));
        spellings.push_back({key, std::get<0>(entry), cxxime::kNormalSpelling, 0.0f});

        cxxime::Candidate candidate;
        candidate.text = std::get<1>(entry);
        candidate.frequency = std::get<2>(entry);
candidate.code = std::get<0>(entry);
candidate.syllables = std::get<0>(entry);
        topn.push_back({key, {candidate}});
    }
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, spellings));
    ASSERT_TRUE(cxxime::test::create_test_topn(topn_path, topn));
    const std::vector<TestDictEntry> wubi_entries = {{"a", "wubi-test", 100}};
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_entries));
    ASSERT_TRUE(
        cxxime::test::create_test_wubi_index(wubi_prefix_index_path, wubi_entries));

    write_manifest_for_files(dict_path, {
        {"pinyin_dict", dict_path},
        {"pinyin_idx", idx_path},
        {"pinyin_spellings", spellings_path},
        {"pinyin_topn", topn_path},
        {"wubi_dict", wubi_path},
        {"wubi_prefix_index", wubi_prefix_index_path},
    });
}

void create_test_dictionary_bundle_with_wubi(const std::string& dict_path,
                                             const std::vector<TestDictEntry>& pinyin_entries,
                                             const std::vector<TestDictEntry>& wubi_entries) {
    create_test_dictionary_bundle(dict_path, pinyin_entries);

    const std::string wubi_path = dict_path + ".wubi.bin";
    const std::string wubi_prefix_index_path = wubi_path + ".idx";
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_entries));
    ASSERT_TRUE(
        cxxime::test::create_test_wubi_index(wubi_prefix_index_path, wubi_entries));

    write_manifest_for_files(dict_path, {
        {"pinyin_dict", dict_path},
        {"pinyin_idx", dict_path + ".idx"},
        {"pinyin_spellings", dict_path + ".spellings.bin"},
        {"pinyin_topn", dict_path + ".topn.bin"},
        {"wubi_dict", wubi_path},
        {"wubi_prefix_index", wubi_prefix_index_path},
    });
}

void delete_test_dictionary_bundle(const std::string& dict_path) {
    DeleteFileA(dict_path.c_str());
    DeleteFileA((dict_path + ".idx").c_str());
    DeleteFileA((dict_path + ".spellings.bin").c_str());
    DeleteFileA((dict_path + ".topn.bin").c_str());
    DeleteFileA((dict_path + ".wubi.bin").c_str());
    DeleteFileA((dict_path + ".wubi.bin.idx").c_str());
    DeleteFileA(cxxime::dictionary_manifest_path_for_dict(dict_path).c_str());
}

std::string setup_test_dict() {
    std::string dict_path = make_temp_path("test_integration_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"ni", "你", 1000},
        {"hao", "好", 800},
        {"nihao", "你好", 900},
        {"de", "的", 700},
        {"di", "地", 600},
    });
    return dict_path;
}

cxxime::KeyEvent make_key(uint32_t vk, bool shift, bool caps) {
    cxxime::KeyEvent e;
    e.keycode = vk;
    e.is_key_up = false;
    if (shift)
        e.set_shift();
    if (caps)
        e.set_caps_lock();
    return e;
}

ProcessKeyResult type_kao(SessionManager& mgr, uint32_t id) {
    ProcessKeyResult r;
    r = mgr.process_key(id, make_key('K'));
    r = mgr.process_key(id, make_key('A'));
    r = mgr.process_key(id, make_key('O'));
    return r;
}

bool candidate_contains(const cxxime::CandidatePage& page, const std::string& text) {
    for (const auto& c : page.candidates) {
        if (c.text == text)
            return true;
    }
    return false;
}

bool wait_for_count(std::atomic<int>& value, int expected, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (value.load() < expected) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}
