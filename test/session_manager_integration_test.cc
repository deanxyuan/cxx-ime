// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

// SessionManager integration tests: verify ProcessKeyResult, select_candidate,
// commit_composition, clear_composition, focus_out, and GET_STATUS with
// the OutputComposer pipeline.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/data_path.h>
#include <cxxime/dictionary_manifest.h>
#include <cxxime/dictionary_monitor.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/key_event.h>
#include <cxxime/lexicon_control.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/spellings_index.h>
#include <cxxime/user_dict_validation.h>

#include "../server/src/session_manager.h"
#include "../server/src/lexicon_control_handler.h"
#include "util/testutil.h"
#include "util/topn_test_data.h"
#include "util/wubi_index_test_data.h"

static char temp_path[MAX_PATH] = {};
static std::string test_user_data_dir;

using TestDictEntry = std::tuple<std::string, std::string, int>;

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

static std::string read_text_file(const std::string& path) {
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

static void write_manifest_for_files(
        const std::string& dict_path,
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

static void create_test_dictionary_bundle(const std::string& dict_path,
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

static void
create_test_dictionary_bundle_with_wubi(const std::string& dict_path,
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

static void delete_test_dictionary_bundle(const std::string& dict_path) {
    DeleteFileA(dict_path.c_str());
    DeleteFileA((dict_path + ".idx").c_str());
    DeleteFileA((dict_path + ".spellings.bin").c_str());
    DeleteFileA((dict_path + ".topn.bin").c_str());
    DeleteFileA((dict_path + ".wubi.bin").c_str());
    DeleteFileA((dict_path + ".wubi.bin.idx").c_str());
    DeleteFileA(cxxime::dictionary_manifest_path_for_dict(dict_path).c_str());
}

static std::string setup_test_dict() {
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

static cxxime::KeyEvent make_key(uint32_t vk, bool shift = false, bool caps = false) {
    cxxime::KeyEvent e;
    e.keycode = vk;
    e.is_key_up = false;
    if (shift) e.set_shift();
    if (caps) e.set_caps_lock();
    return e;
}

static ProcessKeyResult type_kao(SessionManager& mgr, uint32_t id) {
    ProcessKeyResult r;
    r = mgr.process_key(id, make_key('K'));
    r = mgr.process_key(id, make_key('A'));
    r = mgr.process_key(id, make_key('O'));
    return r;
}

static bool candidate_contains(const cxxime::CandidatePage& page, const std::string& text) {
    for (const auto& c : page.candidates) {
        if (c.text == text)
            return true;
    }
    return false;
}

static bool wait_for_count(std::atomic<int>& value, int expected, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (value.load() < expected) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

TEST(SessionIntegration, initialize_rejects_legacy_wubi_index_role) {
    const std::string dict_path = make_temp_path("test_legacy_wubi_role_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni", "你", 100}});
    write_manifest_for_files(dict_path, {
        {"pinyin_dict", dict_path},
        {"pinyin_idx", dict_path + ".idx"},
        {"pinyin_spellings", dict_path + ".spellings.bin"},
        {"pinyin_topn", dict_path + ".topn.bin"},
        {"wubi_dict", dict_path + ".wubi.bin"},
        {"wubi_idx", dict_path + ".wubi.bin.idx"},
    });

    SessionManager manager;
    ASSERT_TRUE(!manager.initialize(dict_path));
    delete_test_dictionary_bundle(dict_path);
}

// ============================================================
// process_key tests
// ============================================================

TEST(SessionIntegration, process_key_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    // Type a letter in Chinese mode
    auto r = mgr.process_key(id, make_key('N'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(r.composing);
    ASSERT_EQ(r.preedit, "n");
    ASSERT_EQ(r.preedit_cursor, static_cast<size_t>(1));

    r = mgr.process_key(id, make_key(VK_LEFT));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(r.composing);
    ASSERT_EQ(r.preedit, "n");
    ASSERT_EQ(r.preedit_cursor, static_cast<size_t>(0));
}

TEST(SessionIntegration, wubi_fifth_key_returns_commit_and_next_composition) {
    const std::string dict_path = make_temp_path("test_wubi_fifth_key_session.bin");
    create_test_dictionary_bundle_with_wubi(dict_path, {{"a", "拼", 100}},
                                                {
                                                    {"abcd", "首选", 300},
                                                    {"abcd", "次选", 200},
                                                    {"e", "下一项", 100},
                                                });

    auto config = std::make_shared<cxxime::Config>();
    config->input_mode = static_cast<int>(cxxime::InputMode::WUBI);
    config->wubi_commit_first_on_fifth_key = true;
    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path, config));
    const uint32_t id = mgr.create_session();

    ASSERT_EQ(mgr.process_key(id, make_key('A')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(mgr.process_key(id, make_key('B')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(mgr.process_key(id, make_key('C')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(mgr.process_key(id, make_key('D')).result, cxxime::ProcessResult::ACCEPTED);

    const auto result = mgr.process_key(id, make_key('E'));
    ASSERT_EQ(result.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(result.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(result.commit_text, "首选");
    ASSERT_TRUE(result.composing);
    ASSERT_EQ(result.preedit, "e");
    ASSERT_EQ(result.preedit_cursor, static_cast<size_t>(1));
    ASSERT_TRUE(candidate_contains(result.candidates, "下一项"));

    mgr.destroy_session(id);
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, process_key_invalid_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto r = mgr.process_key(999, make_key('N'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionIntegration, process_key_committed_has_ime_status) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Toggle to English mode
    mgr.toggle_chinese(id);

    // Type a letter in English mode → should be committed
    auto r = mgr.process_key(id, make_key('A'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    // ime_status should be filled
    ASSERT_EQ(r.ime_status.chinese_mode(), false);
}

TEST(SessionIntegration, english_capslock_letter_preserves_engine_case) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.toggle_chinese(id);

    auto upper = mgr.process_key(id, make_key('N', false, true));
    ASSERT_EQ(upper.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(upper.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(upper.commit_text, "N");

    auto lower = mgr.process_key(id, make_key('N', true, true));
    ASSERT_EQ(lower.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(lower.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(lower.commit_text, "n");
}

TEST(SessionIntegration, english_capslock_keeps_english_and_outputs_uppercase) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.toggle_chinese(id);

    std::string text;
    for (char ch : std::string("NIHAO")) {
        auto r = mgr.process_key(id, make_key(ch, false, true));
        ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
        ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
        ASSERT_TRUE(!r.composing);
        text += r.commit_text;
    }

    ASSERT_EQ(text, "NIHAO");
}

TEST(SessionIntegration, english_enter_passes_to_application) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.toggle_chinese(id);

    auto r = mgr.process_key(id, make_key(VK_RETURN));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(r.commit_text.empty());
    ASSERT_TRUE(!r.composing);
    ASSERT_EQ(r.ime_status.chinese_mode(), false);
}

TEST(SessionIntegration, append_enter_preserves_case_through_output_composer) {
    std::string cfg_path = make_temp_path("test_append_config.json");
    {
        std::ofstream f(cfg_path);
        f << R"({"ascii_composer":{"switch_key":{"Caps_Lock":"append"}}})";
    }

    auto config = std::make_shared<cxxime::Config>();
    ASSERT_TRUE(config->load(cfg_path));
    SessionManager mgr;
    mgr.initialize(setup_test_dict(), config);
    uint32_t id = mgr.create_session();

    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    cxxime::KeyEvent caps;
    caps.keycode = 0x14;
    caps.is_key_up = false;
    caps.set_caps_lock();
    mgr.process_key(id, caps);

    mgr.process_key(id, make_key('D', false, true));
    mgr.process_key(id, make_key('D', false, true));

    cxxime::KeyEvent enter;
    enter.keycode = 0x0D;
    enter.is_key_up = false;
    enter.set_caps_lock();
    auto r = mgr.process_key(id, enter);

    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "niDD");

    DeleteFileA(cfg_path.c_str());
}

// ============================================================
// select_candidate tests
// ============================================================

TEST(SessionIntegration, select_candidate_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    // Type "ni" to get candidates
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Select first candidate
    auto r = mgr.select_candidate(id, 0);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_TRUE(!r.commit_text.empty());
}

TEST(SessionIntegration, select_candidate_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto r = mgr.select_candidate(999, 0);
    ASSERT_EQ(r.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionIntegration, select_candidate_candidate_no_conversion) {
    // kCandidate source should not apply CapsLock/full-width conversion
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Type "ni" to get candidates
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Enable full_shape and caps_lock before selection.
    mgr.toggle_shape(id);
    mgr.sync_caps_lock(id, true);

    // Select first candidate — kCandidate source, no conversion
    auto r = mgr.select_candidate(id, 0);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    // Candidate text should be preserved as-is (no full-width, no case inversion)
    ASSERT_TRUE(!r.commit_text.empty());
    // With kCandidate, transform returns text unchanged — verify it's a valid Chinese string
    // (not full-width ASCII, which would start with 0xEF byte)
    ASSERT_TRUE(r.commit_text[0] != '\xEF');
}

// ============================================================
// commit_composition tests
// ============================================================

TEST(SessionIntegration, commit_composition_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Type "ni" to start composing
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Commit composition
    auto r = mgr.commit_composition(id);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.composing, false);
}

TEST(SessionIntegration, commit_composition_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto r = mgr.commit_composition(999);
    ASSERT_EQ(r.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionIntegration, set_chinese_mode_commits_raw_composition_once) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    auto unchanged = mgr.set_chinese_mode(id, true);
    ASSERT_EQ(unchanged.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(unchanged.commit_text.empty());
    ASSERT_EQ(unchanged.composing, true);
    ASSERT_EQ(unchanged.ime_status.chinese_mode(), true);

    auto first = mgr.set_chinese_mode(id, false);
    ASSERT_EQ(first.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(first.commit_text, "ni");
    ASSERT_EQ(first.composing, false);
    ASSERT_EQ(first.ime_status.chinese_mode(), false);

    auto second = mgr.set_chinese_mode(id, false);
    ASSERT_EQ(second.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(second.commit_text.empty());
    ASSERT_EQ(second.composing, false);
    ASSERT_EQ(second.ime_status.revision, first.ime_status.revision);
}

TEST(SessionIntegration, set_chinese_mode_invalid_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto result = mgr.set_chinese_mode(999, false);
    ASSERT_EQ(result.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// clear_composition tests
// ============================================================

TEST(SessionIntegration, clear_composition_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Type something
    mgr.process_key(id, make_key('N'));

    // Clear
    auto st = mgr.clear_composition(id);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);
}

TEST(SessionIntegration, clear_composition_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto st = mgr.clear_composition(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// focus_out tests
// ============================================================

TEST(SessionIntegration, focus_out_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    auto [st0, s0] = mgr.get_ime_status(id);
    uint64_t rev_before = s0.revision;

    auto st = mgr.focus_out(id);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);

    // focus_out only clears composition; it does not change this session's visible state.
    auto [st1, s1] = mgr.get_ime_status(id);
    ASSERT_EQ(s1.revision, rev_before);
}

TEST(SessionIntegration, focus_out_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto st = mgr.focus_out(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// GET_STATUS tests
// ============================================================

TEST(SessionIntegration, get_status_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    auto [st, s] = mgr.get_ime_status(id);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);
    ASSERT_EQ(s.chinese_mode(), true);
    ASSERT_EQ(s.full_shape(), false);
    ASSERT_EQ(s.chinese_punct(), true);
}

TEST(SessionIntegration, get_status_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto [st, s] = mgr.get_ime_status(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// OutputComposer integration (full_shape intercept_key path)
// ============================================================

TEST(SessionIntegration, english_fullwidth_digit_intercepted) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Switch to English mode
    mgr.toggle_chinese(id);
    // Enable full_shape
    mgr.toggle_shape(id);

    // Press digit key '1' → should be intercepted by OutputComposer
    auto r = mgr.process_key(id, make_key('1'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "１");
    ASSERT_EQ(r.composing, false);
}

TEST(SessionIntegration, chinese_mode_digit_not_intercepted) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Chinese mode + full_shape → digit should NOT be intercepted
    mgr.toggle_shape(id);

    // Type "ni" first to get candidates
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Press digit '1' → selects candidate, not intercepted
    auto r = mgr.process_key(id, make_key('1'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    // Should be a candidate, not "１"
    ASSERT_TRUE(!r.commit_text.empty());
}

// ============================================================
// Multiple sessions visible state
// ============================================================

TEST(SessionIntegration, applied_initial_state_applies_only_to_new_sessions) {
    std::string config_path = make_temp_path("test_initial_state_config.json");
    {
        std::ofstream config(config_path);
        config << R"({"initial_state":{"full_shape":false,"chinese_punct":true}})";
    }

    auto initial_config = std::make_shared<cxxime::Config>();
    ASSERT_TRUE(initial_config->load(config_path));
    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(setup_test_dict(), initial_config));
    uint32_t existing = mgr.create_session();
    ASSERT_GT(existing, (uint32_t)0);

    {
        std::ofstream config(config_path);
        config << R"({"initial_state":{"full_shape":true,"chinese_punct":false}})";
    }
    auto updated_config = std::make_shared<cxxime::Config>();
    ASSERT_TRUE(updated_config->load(config_path));
    mgr.apply_config(updated_config);

    auto [existing_status_result, existing_status] = mgr.get_ime_status(existing);
    ASSERT_EQ(existing_status_result, cxxime::IPCStatus::OK);
    ASSERT_EQ(existing_status.full_shape(), false);
    ASSERT_EQ(existing_status.chinese_punct(), true);

    uint32_t created_after_reload = mgr.create_session();
    ASSERT_GT(created_after_reload, (uint32_t)0);
    auto [new_status_result, new_status] = mgr.get_ime_status(created_after_reload);
    ASSERT_EQ(new_status_result, cxxime::IPCStatus::OK);
    ASSERT_EQ(new_status.full_shape(), true);
    ASSERT_EQ(new_status.chinese_punct(), false);

    DeleteFileA(config_path.c_str());
}

TEST(SessionIntegration, session_shape_change_does_not_affect_other_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();

    // Session 1 changes its own language and full-shape modes.
    mgr.toggle_chinese(id1);
    mgr.toggle_shape(id1);

    auto [st, status] = mgr.get_ime_status(id2);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);
    ASSERT_EQ(status.chinese_mode(), true);
    ASSERT_EQ(status.full_shape(), false);

    auto full_width = mgr.process_key(id1, make_key('5'));
    ASSERT_EQ(full_width.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(full_width.commit_text, cxxime::OutputComposer::to_full_width('5'));

    auto half_width = mgr.process_key(id2, make_key('5'));
    ASSERT_EQ(half_width.result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(half_width.commit_text.empty());
    ASSERT_EQ(half_width.ime_status.chinese_mode(), true);
    ASSERT_EQ(half_width.ime_status.full_shape(), false);
}

TEST(SessionIntegration, session_punctuation_change_does_not_affect_other_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();

    mgr.toggle_punct(id1);

    auto english_punct = mgr.process_key(id1, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(english_punct.result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(english_punct.commit_text.empty());

    auto chinese_punct = mgr.process_key(id2, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(chinese_punct.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(chinese_punct.commit_text, "。");
}

TEST(SessionIntegration, lexicon_control_mutations_and_pagination) {
    const std::string preference_path = test_user_data_dir + "\\learning_pinyin.tsv";
    {
        std::ofstream output(preference_path, std::ios::binary | std::ios::trunc);
        output << "偏好一\tpreferenceone\tpreferenceone\t2\t1\t\n"
            << "偏好二\tpreferencetwo\tpreferencetwo\t3\t2\t\n";
    }
    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(setup_test_dict()));

    auto execute = [&](const cxxime::LexiconControlRequest& request,
                       cxxime::LexiconControlResult* result) {
        std::string request_payload;
        std::string response_payload;
        return cxxime::encode_lexicon_request(request, &request_payload) &&
               handle_lexicon_control_request(mgr, request_payload, &response_payload) &&
               cxxime::decode_lexicon_result(response_payload, result);
    };

    cxxime::LexiconControlRequest request;
    request.operation = cxxime::LexiconOperation::kAdd;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.text = "control-entry";
    request.code = "controlcode";
    cxxime::LexiconControlResult result;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request.text.assign(cxxime::kCandidateTextCapacity, 'x');
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(result.error_code, static_cast<uint32_t>(ERROR_BUFFER_OVERFLOW));

    request.text = "invalid-code";
    request.code = "ABC";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(result.error_code, static_cast<uint32_t>(ERROR_INVALID_DATA));

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "control";
    request.offset = 0;
    request.limit = 1;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<size_t>(1));
    ASSERT_EQ(result.query.entries.size(), static_cast<size_t>(1));
    ASSERT_TRUE(result.query.entries[0].text == "control-entry");

    request = {};
    request.operation = cxxime::LexiconOperation::kReplace;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.old_text = "control-entry";
    request.old_code = "controlcode";
    request.text = "control-replaced";
    request.code = "controlcode";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kAdd;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.text = "control-second";
    request.code = "controlsecond";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {
        {"control-replaced", "controlcode"}, {"control-second", "INVALID"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(mgr.query_user_entries("control", cxxime::UserDictKind::PINYIN, 0, 10).match_total,
              static_cast<std::size_t>(2));

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {
        {"control-replaced", "controlcode"}, {"control-second", "controlsecond"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(mgr.query_user_entries("control", cxxime::UserDictKind::PINYIN, 0, 10).match_total,
              static_cast<std::size_t>(0));

    request = {};
    request.operation = cxxime::LexiconOperation::kSave;
    request.kind = cxxime::UserDictKind::PINYIN;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request.resource = cxxime::LexiconResource::kCandidatePreference;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.resource = cxxime::LexiconResource::kCandidatePreference;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {{"偏好一", "preferenceone"}, {"偏好二", "preferencetwo"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.resource = cxxime::LexiconResource::kCandidatePreference;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "";
    request.limit = 10;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<std::size_t>(0));

    const std::string import_path = make_temp_path("control_import.tsv");
    {
        std::ofstream output(import_path, std::ios::binary | std::ios::trunc);
        output << "control-imported\tcontrolimported\t9\n";
    }
    request.operation = cxxime::LexiconOperation::kImport;
    request.resource = cxxime::LexiconResource::kUserLexicon;
    request.source_path = import_path;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(mgr.query_user_entries("control-imported", cxxime::UserDictKind::PINYIN, 0, 10)
                  .match_total,
              static_cast<std::size_t>(1));
    DeleteFileA(import_path.c_str());

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {{"control-imported", "controlimported"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kAdd;
    request.resource = cxxime::LexiconResource::kCandidatePreference;
    request.text = "unsupported";
    request.code = "unsupported";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(result.error_code, static_cast<uint32_t>(ERROR_NOT_SUPPORTED));

    request = {};
    request.operation = cxxime::LexiconOperation::kDisableSystemEntry;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.text = "你好";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    const std::string disabled_path = test_user_data_dir + "\\disabled_pinyin.tsv";
    ASSERT_TRUE(SetFileAttributesA(disabled_path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
    request.text = "失败词";
    ASSERT_TRUE(execute(request, &result));
    SetFileAttributesA(disabled_path.c_str(), FILE_ATTRIBUTE_NORMAL);
    ASSERT_TRUE(!result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "失败词";
    request.limit = 10;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<std::size_t>(0));

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "你好";
    request.limit = 10;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(result.query.entries[0].text, "你好");

    request = {};
    request.operation = cxxime::LexiconOperation::kQuerySystemEntryStatus;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.texts = {"世界", "你好"};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(result.query.entries[0].text, "你好");

    request.operation = cxxime::LexiconOperation::kRestoreSystemEntry;
    request.text = "你好";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    DeleteFileA(preference_path.c_str());
}

TEST(SessionIntegration, user_lexicon_save_failure_keeps_memory_unchanged) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));

    const auto query = [&](const std::string& text) {
        return manager.query_user_entries(text, cxxime::UserDictKind::PINYIN, 0, 10);
    };
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    ASSERT_EQ(
        manager.add_user_entry(cxxime::UserDictKind::PINYIN, "rollback-baseline", "rollbackcode"),
        cxxime::IPCStatus::OK);
    ASSERT_EQ(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "rollback-second",
                                     "rollbacksecond"),
              cxxime::IPCStatus::OK);
    ASSERT_TRUE(DeleteFileA(user_path.c_str()) != FALSE);
    ASSERT_TRUE(CreateDirectoryA(user_path.c_str(), nullptr) != FALSE);

    ASSERT_TRUE(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "rollback-added",
                                       "rollbackadded") != cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-added").match_total, static_cast<std::size_t>(0));

    ASSERT_TRUE(manager.replace_user_entry(cxxime::UserDictKind::PINYIN, "rollback-baseline",
                                           "rollbackcode", "rollback-replaced",
                                           "rollbackreplaced") != cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-baseline").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(query("rollback-replaced").match_total, static_cast<std::size_t>(0));

    const std::vector<cxxime::LexiconEntryKey> entries = {
        {"rollback-baseline", "rollbackcode"}, {"rollback-second", "rollbacksecond"}};
    ASSERT_TRUE(manager.delete_user_entries(cxxime::UserDictKind::PINYIN, entries) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-baseline").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(query("rollback-second").match_total, static_cast<std::size_t>(1));

    ASSERT_TRUE(RemoveDirectoryA(user_path.c_str()) != FALSE);
    ASSERT_EQ(manager.delete_user_entries(cxxime::UserDictKind::PINYIN, entries),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-baseline").match_total, static_cast<std::size_t>(0));
    ASSERT_EQ(query("rollback-second").match_total, static_cast<std::size_t>(0));
}

TEST(SessionIntegration, user_lexicon_import_is_atomic_and_server_owned) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));

    const auto query = [&](const std::string& text) {
        return manager.query_user_entries(text, cxxime::UserDictKind::PINYIN, 0, 10);
    };
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    const std::string valid_source = make_temp_path("valid_user_import.tsv");
    const std::string invalid_source = make_temp_path("invalid_user_import.tsv");
    const std::string missing_source = make_temp_path("missing_user_import.tsv");
    const std::string oversized_source = make_temp_path("oversized_user_import.tsv");
    const std::string replacement_source = make_temp_path("replacement_user_import.tsv");
    const std::string empty_source = make_temp_path("empty_user_import.tsv");
    DeleteFileA(missing_source.c_str());
    {
        HANDLE file = CreateFileA(oversized_source.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
        LARGE_INTEGER size = {};
        size.QuadPart = static_cast<LONGLONG>(cxxime::kMaxUserDictImportBytes + 1);
        ASSERT_TRUE(SetFilePointerEx(file, size, nullptr, FILE_BEGIN) != FALSE);
        ASSERT_TRUE(SetEndOfFile(file) != FALSE);
        CloseHandle(file);
    }

    ASSERT_EQ(
        manager.add_user_entry(cxxime::UserDictKind::PINYIN, "import-baseline", "importbaseline"),
        cxxime::IPCStatus::OK);
    const std::string baseline_contents = read_text_file(user_path);
    {
        std::ofstream output(invalid_source, std::ios::binary | std::ios::trunc);
        output.write("\xc3\x28", 2);
        output << "\tvalid\t1\n";
    }
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, invalid_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(query("import-baseline").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(read_text_file(user_path), baseline_contents);
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, missing_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(read_text_file(user_path), baseline_contents);
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, oversized_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(read_text_file(user_path), baseline_contents);

    {
        std::ofstream output(valid_source, std::ios::binary | std::ios::trunc);
        output << "imported-entry\timportedcode\t7\n";
    }
    ASSERT_EQ(manager.import_user_dict(cxxime::UserDictKind::PINYIN, valid_source),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(query("import-baseline").match_total, static_cast<std::size_t>(0));
    ASSERT_EQ(query("imported-entry").match_total, static_cast<std::size_t>(1));
    const std::string imported_contents = read_text_file(user_path);

    {
        std::ofstream output(replacement_source, std::ios::binary | std::ios::trunc);
        output << "replacement-entry\treplacementcode\t8\n";
    }
    HANDLE user_lock = CreateFileA(user_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(user_lock != INVALID_HANDLE_VALUE);
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, replacement_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(query("imported-entry").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(query("replacement-entry").match_total, static_cast<std::size_t>(0));
    ASSERT_EQ(read_text_file(user_path), imported_contents);
    CloseHandle(user_lock);

    {
        std::ofstream output(empty_source, std::ios::binary | std::ios::trunc);
    }
    ASSERT_EQ(manager.import_user_dict(cxxime::UserDictKind::PINYIN, empty_source),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(query("").match_total, static_cast<std::size_t>(0));
    DeleteFileA(valid_source.c_str());
    DeleteFileA(invalid_source.c_str());
    DeleteFileA(oversized_source.c_str());
    DeleteFileA(replacement_source.c_str());
    DeleteFileA(empty_source.c_str());
}

// ============================================================
// Dictionary reload integration
// ============================================================

TEST(SessionIntegration, reload_dictionaries_updates_active_session) {
    std::string dict_path = make_temp_path("test_hot_reload_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"kao", "reload-old", 100},
    });

    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path));
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto old_result = type_kao(mgr, id);
    ASSERT_EQ(old_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(old_result.composing);
    ASSERT_TRUE(candidate_contains(old_result.candidates, "reload-old"));

    ASSERT_EQ(mgr.clear_composition(id), cxxime::IPCStatus::OK);
    create_test_dictionary_bundle(dict_path, {
        {"kao", "reload-new", 100},
    });
    ASSERT_EQ(mgr.reload_dictionaries(), cxxime::IPCStatus::OK);

    auto new_result = type_kao(mgr, id);
    ASSERT_EQ(new_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(new_result.composing);
    ASSERT_TRUE(candidate_contains(new_result.candidates, "reload-new"));
    ASSERT_TRUE(!candidate_contains(new_result.candidates, "reload-old"));

    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, dictionary_monitor_reload_updates_active_session) {
    std::string dict_path = make_temp_path("test_auto_hot_reload_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"kao", "auto-hot-reload-old", 100},
    });

    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path));
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto old_result = type_kao(mgr, id);
    ASSERT_EQ(old_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(old_result.composing);
    ASSERT_TRUE(candidate_contains(old_result.candidates, "auto-hot-reload-old"));
    ASSERT_EQ(mgr.clear_composition(id), cxxime::IPCStatus::OK);

    std::atomic<int> reload_count{0};
    cxxime::DictionaryMonitorOptions options;
    options.debounce_ms = 20;
    options.poll_ms = 100;
    options.retry_ms = 50;
    options.max_retries = 5;

    cxxime::DictionaryMonitor monitor;
    std::string manifest_path = cxxime::dictionary_manifest_path_for_dict(dict_path);
    ASSERT_TRUE(monitor.start({manifest_path}, [&] {
        auto status = mgr.reload_dictionaries();
        if (status == cxxime::IPCStatus::OK)
            reload_count.fetch_add(1);
        return status == cxxime::IPCStatus::OK;
    }, options));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    create_test_dictionary_bundle(dict_path, {
        {"kao", "auto-hot-reload-new-value", 100},
    });

    ASSERT_TRUE(wait_for_count(reload_count, 1, 3000));
    monitor.stop();

    auto new_result = type_kao(mgr, id);
    ASSERT_EQ(new_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(new_result.composing);
    ASSERT_TRUE(candidate_contains(new_result.candidates, "auto-hot-reload-new-value"));
    ASSERT_TRUE(!candidate_contains(new_result.candidates, "auto-hot-reload-old"));

    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, reload_dictionaries_failure_keeps_active_session_resources) {
    std::string dict_path = make_temp_path("test_hot_reload_failure_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"kao", "reload-still-live", 100},
    });

    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path));
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    ASSERT_EQ(mgr.clear_composition(id), cxxime::IPCStatus::OK);
    ASSERT_TRUE(DeleteFileA(dict_path.c_str()));
    ASSERT_EQ(mgr.reload_dictionaries(), cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED);

    auto result = type_kao(mgr, id);
    ASSERT_EQ(result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(result.composing);
    ASSERT_TRUE(candidate_contains(result.candidates, "reload-still-live"));
}

static std::string write_temp_punct_json(const char* name, const char* content) {
    std::string path = make_temp_path(name);
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

// Test 1: Hot-reload punctuation mapping
TEST(SessionIntegration, punctuation_hot_reload) {
    // Write initial punctuation.json: "." → "。"
    std::string punct_path = write_temp_punct_json("punct_hot.json",
        R"({"half_shape": {".": {"commit": "。"}}})");

    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    // Load custom punctuation file
    ASSERT_TRUE(mgr.reload_punctuation(punct_path));

    uint32_t id = mgr.create_session();
    // Chinese mode + chinese_punct=true (default)
    // Press '.' → should map to "。"
    auto r = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "。");

    // Write modified punctuation.json: "." → "！"
    write_temp_punct_json("punct_hot.json",
        R"({"half_shape": {".": {"commit": "！"}}})");

    // Reload — new mapping should take effect
    ASSERT_TRUE(mgr.reload_punctuation(punct_path));

    auto r2 = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r2.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r2.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r2.commit_text, "！");
}

// Test 2: Multi-session quote pair_open state isolation
TEST(SessionIntegration, punctuation_pair_state_isolation) {
    std::string punct_path = write_temp_punct_json("punct_pair.json",
        R"({"half_shape": {"'": {"pair": ["'", "'"]}}})");

    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    ASSERT_TRUE(mgr.reload_punctuation(punct_path));

    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();

    // Session 1: first ' → left quote "'"
    auto r1a = mgr.process_key(id1, make_key(VK_OEM_7));
    ASSERT_EQ(r1a.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r1a.commit_text, "'");

    // Session 1: second ' → right quote "'"
    auto r1b = mgr.process_key(id1, make_key(VK_OEM_7));
    ASSERT_EQ(r1b.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r1b.commit_text, "'");

    // Session 2: first ' → left quote "'" (independent state, not affected by session 1)
    auto r2a = mgr.process_key(id2, make_key(VK_OEM_7));
    ASSERT_EQ(r2a.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r2a.commit_text, "'");

    // Session 1: third ' → left quote "'" again (alternation continues)
    auto r1c = mgr.process_key(id1, make_key(VK_OEM_7));
    ASSERT_EQ(r1c.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r1c.commit_text, "'");
}

// Test 3: Punctuation committed via IPC process_key
TEST(SessionIntegration, punctuation_commit_via_ipc) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Chinese mode (default): chinese_punct=true
    // Press '.' → should commit "。"
    auto r = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "。");
    ASSERT_EQ(r.composing, false);

    // Press ',' → should commit "，"
    auto r2 = mgr.process_key(id, make_key(VK_OEM_COMMA));
    ASSERT_EQ(r2.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r2.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r2.commit_text, "，");

    // Toggle chinese_punct off → punctuation should be rejected (pass-through)
    mgr.toggle_punct(id);
    auto r3 = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r3.status, cxxime::IPCStatus::OK);
    // With chinese_punct=false, punctuation is not mapped → REJECTED
    ASSERT_EQ(r3.result, cxxime::ProcessResult::REJECTED);
}

int main() {
    GetTempPathA(MAX_PATH, temp_path);
    const std::string directory_name =
        "cxxime-session-integration-" + std::to_string(GetCurrentProcessId());
    test_user_data_dir = make_temp_path(directory_name.c_str());
    CreateDirectoryA(test_user_data_dir.c_str(), nullptr);
    DeleteFileA((test_user_data_dir + "\\default.json").c_str());
    DeleteFileA((test_user_data_dir + "\\user_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\user_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_wubi.tsv").c_str());

    cxxime::set_data_dir(CXXIME_DATA_DIR);
    cxxime::set_user_data_dir(test_user_data_dir);
    const int result = test::RunAllTests();

    DeleteFileA((test_user_data_dir + "\\default.json").c_str());
    DeleteFileA((test_user_data_dir + "\\user_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\user_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_wubi.tsv").c_str());
    RemoveDirectoryA(test_user_data_dir.c_str());
    return result;
}
