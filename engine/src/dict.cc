// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/dict.h>
#include "binary_format.h"
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <windows.h>
#include <shlobj.h>
#include <sqlite3.h>
#include <cxxime/logging.h>

static const char DICT_MAGIC_V1[] = "CXDIC\x01\x00\x00";
static const char DICT_MAGIC_V2[] = "CXDIC\x02\x00\x00";

namespace cxxime {

Dict::~Dict() {
    close();
}

bool Dict::open(const std::string& dict_path, const std::string& user_dict_path) {
    if (!open_dict(dict_path))
        return false;
    open_user_dict(user_dict_path);
    return true;
}

bool Dict::is_open() const {
    return dict_data_ != nullptr;
}

bool Dict::open_dict(const std::string& bin_path) {
    unload_dict();
    CXXIME_LOG(L"Dict::open_dict path=%S", bin_path.c_str());

    HANDLE hFile = CreateFileA(bin_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        CXXIME_LOG(L"Dict::open_dict CreateFileA FAILED");
        return false;
    }

    LARGE_INTEGER li;
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < (LONGLONG)sizeof(DictHeader)) {
        CloseHandle(hFile);
        CXXIME_LOG(L"Dict::open_dict file too small");
        return false;
    }
    dict_data_size_ = (size_t)li.QuadPart;

    HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) {
        CloseHandle(hFile);
        CXXIME_LOG(L"Dict::open_dict CreateFileMappingA FAILED");
        return false;
    }

    void* base = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        CXXIME_LOG(L"Dict::open_dict MapViewOfFile FAILED");
        return false;
    }

    dict_file_handle_ = hFile;
    dict_mapping_handle_ = hMap;
    dict_data_ = (const char*)base;

    auto* hdr = (const DictHeader*)dict_data_;
    if (std::memcmp(hdr->magic, DICT_MAGIC_V1, 8) != 0 &&
        std::memcmp(hdr->magic, DICT_MAGIC_V2, 8) != 0) {
        CXXIME_LOG(L"Dict::open_dict bad magic");
        unload_dict();
        return false;
    }

    // Bounds validation: ensure header fields don't point outside the file
    uint32_t version = hdr->version;
    if (version != 1 && version != 2) {
        CXXIME_LOG(L"Dict::open_dict bad version=%u", version);
        unload_dict();
        return false;
    }
    if (hdr->entries_offset > dict_data_size_ ||
        hdr->strings_offset > dict_data_size_ ||
        hdr->entry_count > (dict_data_size_ / sizeof(DictEntry)) ||
        hdr->entries_offset + (uint64_t)hdr->entry_count * sizeof(DictEntry) > dict_data_size_ ||
        hdr->strings_offset + (uint64_t)hdr->string_data_size > dict_data_size_) {
        CXXIME_LOG(L"Dict::open_dict bounds check FAILED");
        unload_dict();
        return false;
    }

    dict_entry_count_ = hdr->entry_count;
    dict_entries_ = (const DictEntry*)(dict_data_ + hdr->entries_offset);
    dict_strings_ = dict_data_ + hdr->strings_offset;

    CXXIME_LOG(L"Dict::open_dict OK entries=%u", dict_entry_count_);

    build_syllabary();
    build_id_index();
    return true;
}

bool Dict::open_user_dict(const std::string& db_path) {
    if (user_db_) {
        sqlite3_close(user_db_);
        user_db_ = nullptr;
    }

    if (db_path == ":memory:") {
        int rc = sqlite3_open(":memory:", &user_db_);
        if (rc != SQLITE_OK) {
            CXXIME_LOG(L"Dict::open_user_dict :memory: FAILED");
            user_db_ = nullptr;
            return false;
        }
    } else {
        wchar_t appdata[MAX_PATH] = {};
        std::wstring path;
        if (db_path.empty()) {
            if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) != S_OK) {
                CXXIME_LOG(L"Dict::open_user_dict SHGetFolderPathW FAILED");
                return false;
            }
            std::wstring user_dir = std::wstring(appdata) + L"\\CxxIME";
            CreateDirectoryW(user_dir.c_str(), nullptr);
            path = user_dir + L"\\user.dict.db";
        } else {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, db_path.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                path.resize(wlen - 1);
                MultiByteToWideChar(CP_UTF8, 0, db_path.c_str(), -1, &path[0], wlen);
            }
        }

        char path_utf8[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, path_utf8, MAX_PATH, nullptr, nullptr);

        int rc = sqlite3_open(path_utf8, &user_db_);
        if (rc != SQLITE_OK) {
            CXXIME_LOG(L"Dict::open_user_dict FAILED rc=%d", rc);
            user_db_ = nullptr;
            return false;
        }
    }

    const char* schema =
        "CREATE TABLE IF NOT EXISTS user_dict ("
        "id INTEGER PRIMARY KEY, "
        "text TEXT NOT NULL, "
        "code TEXT NOT NULL, "
        "frequency INTEGER DEFAULT 1, "
        "last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
        "UNIQUE(text, code));"
        "CREATE INDEX IF NOT EXISTS idx_user_code ON user_dict(code);";

    char* err = nullptr;
    int rc = sqlite3_exec(user_db_, schema, nullptr, nullptr, &err);
    if (err) {
        CXXIME_LOG(L"Dict::open_user_dict schema error: %S", err);
        sqlite3_free(err);
    }
    if (rc != SQLITE_OK) {
        sqlite3_close(user_db_);
        user_db_ = nullptr;
        return false;
    }

    CXXIME_LOG(L"Dict::open_user_dict OK");
    return true;
}

void Dict::unload_dict() {
    if (dict_data_) {
        UnmapViewOfFile(dict_data_);
        dict_data_ = nullptr;
    }
    if (dict_mapping_handle_) {
        CloseHandle(dict_mapping_handle_);
        dict_mapping_handle_ = nullptr;
    }
    if (dict_file_handle_ && dict_file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(dict_file_handle_);
        dict_file_handle_ = nullptr;
    }
    dict_entries_ = nullptr;
    dict_strings_ = nullptr;
    dict_entry_count_ = 0;
    dict_data_size_ = 0;
}

void Dict::close() {
    unload_dict();
    if (user_db_) {
        sqlite3_close(user_db_);
        user_db_ = nullptr;
    }
}

std::vector<Candidate> Dict::lookup_by_syllables(
    const std::vector<std::string>& syllables, int limit) {
    std::vector<Candidate> results;
    if (!dict_entries_ || syllables.empty())
        return results;

    // Build syllable_ids key: ["ni","hao"] → "ni:hao"
    std::string key;
    for (size_t i = 0; i < syllables.size(); ++i) {
        if (i > 0) key += ":";
        key += syllables[i];
    }
    const uint32_t key_len = (uint32_t)key.size();
    const char* key_data = key.data();

    // Binary search for first entry with matching syllable_ids
    uint32_t lo = 0, hi = dict_entry_count_;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const auto& e = dict_entries_[mid];
        const char* sid = dict_strings_ + e.syllable_ids_offset;
        int cmp = std::memcmp(sid, key_data, std::min(e.syllable_ids_len, key_len));
        if (cmp < 0 || (cmp == 0 && e.syllable_ids_len < key_len)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    // Collect all entries with matching syllable_ids (SQL already sorted by freq desc)
    std::unordered_set<std::string> seen;
    while (lo < dict_entry_count_) {
        const auto& e = dict_entries_[lo];
        if (e.syllable_ids_len != key_len)
            break;
        if (std::memcmp(dict_strings_ + e.syllable_ids_offset, key_data, key_len) != 0)
            break;

        Candidate c;
        c.text.assign(dict_strings_ + e.text_offset, e.text_len);
        c.frequency = e.frequency;
        if (seen.insert(c.text).second) {
            results.push_back(std::move(c));
            if ((int)results.size() >= limit)
                break;
        }
        ++lo;
    }

    // Also query user dict by concatenated code
    if (user_db_ && (int)results.size() < limit) {
        std::string concat_code;
        for (auto& s : syllables) concat_code += s;

        const char* sql = "SELECT text, code, frequency FROM user_dict "
                          "WHERE code = ?1 ORDER BY frequency DESC LIMIT ?2";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(user_db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, concat_code.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limit);
            while (sqlite3_step(stmt) == SQLITE_ROW && (int)results.size() < limit) {
                Candidate c;
                const char* text = (const char*)sqlite3_column_text(stmt, 0);
                c.text = text ? text : "";
                c.frequency = sqlite3_column_int(stmt, 2);
                if (seen.insert(c.text).second)
                    results.push_back(std::move(c));
            }
            sqlite3_finalize(stmt);
        }
    }

    // Sort by frequency descending
    std::sort(results.begin(), results.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.frequency > b.frequency;
        });

    if ((int)results.size() > limit)
        results.resize(limit);

    return results;
}

std::vector<Candidate> Dict::lookup(const std::string& code_prefix, int limit) {
    std::vector<Candidate> results;
    if (!dict_entries_)
        return results;

    const uint32_t prefix_len = (uint32_t)code_prefix.size();
    const char* prefix_data = code_prefix.data();

    // Scan all entries for prefix match on code (syllable_ids)
    // Since dict.bin is sorted by syllable_ids, we can binary search for the start
    // and scan forward until the prefix no longer matches.
    uint32_t lo = 0, hi = dict_entry_count_;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const auto& e = dict_entries_[mid];
        const char* sid = dict_strings_ + e.syllable_ids_offset;
        uint32_t cmp_len = std::min(e.syllable_ids_len, prefix_len);
        int cmp = std::memcmp(sid, prefix_data, cmp_len);
        if (cmp < 0 || (cmp == 0 && e.syllable_ids_len < prefix_len)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    // Scan forward collecting prefix matches.
    // Exact matches (same code length) get boosted so they sort before prefix matches.
    std::unordered_set<std::string> seen;
    while (lo < dict_entry_count_ && (int)results.size() < limit) {
        const auto& e = dict_entries_[lo];
        if (e.syllable_ids_len < prefix_len)
            break;
        if (std::memcmp(dict_strings_ + e.syllable_ids_offset, prefix_data, prefix_len) != 0)
            break;

        Candidate c;
        c.text.assign(dict_strings_ + e.text_offset, e.text_len);
        // Exact match first, then shorter codes before longer codes,
        // then by original frequency. Encode as: exact*100000 + (100-len)*100 + freq
        c.frequency = (e.syllable_ids_len == prefix_len ? 100000 : 0)
                    + (100 - (int)e.syllable_ids_len) * 100
                    + e.frequency;
        if (seen.insert(c.text).second)
            results.push_back(std::move(c));
        ++lo;
    }

    // Query user dict
    if (user_db_) {
        std::string pattern = code_prefix + "%";
        const char* sql = "SELECT text, code, frequency FROM user_dict "
                          "WHERE code LIKE ?1 ORDER BY frequency DESC LIMIT ?2";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(user_db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limit);
            while (sqlite3_step(stmt) == SQLITE_ROW && (int)results.size() < limit) {
                Candidate c;
                const char* text = (const char*)sqlite3_column_text(stmt, 0);
                c.text = text ? text : "";
                c.frequency = sqlite3_column_int(stmt, 2);
                if (seen.insert(c.text).second)
                    results.push_back(std::move(c));
            }
            sqlite3_finalize(stmt);
        }
    }

    std::sort(results.begin(), results.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.frequency > b.frequency;
        });

    if ((int)results.size() > limit)
        results.resize(limit);

    return results;
}

int Dict::count(const std::string& code_prefix) {
    if (!dict_entries_)
        return 0;

    const uint32_t prefix_len = (uint32_t)code_prefix.size();
    const char* prefix_data = code_prefix.data();
    int result = 0;

    // Count matching entries in binary dict
    uint32_t lo = 0, hi = dict_entry_count_;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const auto& e = dict_entries_[mid];
        const char* sid = dict_strings_ + e.syllable_ids_offset;
        uint32_t cmp_len = std::min(e.syllable_ids_len, prefix_len);
        int cmp = std::memcmp(sid, prefix_data, cmp_len);
        if (cmp < 0 || (cmp == 0 && e.syllable_ids_len < prefix_len)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    while (lo < dict_entry_count_) {
        const auto& e = dict_entries_[lo];
        if (e.syllable_ids_len < prefix_len)
            break;
        if (std::memcmp(dict_strings_ + e.syllable_ids_offset, prefix_data, prefix_len) != 0)
            break;
        ++result;
        ++lo;
    }

    if (user_db_) {
        std::string pattern = code_prefix + "%";
        const char* sql = "SELECT COUNT(*) FROM user_dict WHERE code LIKE ?1";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(user_db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW)
                result += sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
    }

    return result;
}

std::string Dict::reverse_lookup(const std::string& text) {
    if (!dict_entries_)
        return {};

    // Linear scan for text match (dict.bin is sorted by syllable_ids, not text)
    for (uint32_t i = 0; i < dict_entry_count_; ++i) {
        const auto& e = dict_entries_[i];
        if (e.text_len == text.size() &&
            std::memcmp(dict_strings_ + e.text_offset, text.data(), e.text_len) == 0) {
            return std::string(dict_strings_ + e.syllable_ids_offset, e.syllable_ids_len);
        }
    }
    return {};
}

void Dict::update_frequency(const std::string& text, const std::string& code) {
    if (!user_db_)
        return;

    const char* sql = "INSERT INTO user_dict (text, code, frequency) VALUES (?1, ?2, 1) "
                      "ON CONFLICT(text, code) DO UPDATE SET frequency = frequency + 1, "
                      "last_used = CURRENT_TIMESTAMP";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(user_db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, code.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

bool Dict::create_test_dict(const std::string& path,
                            const std::vector<std::tuple<std::string, std::string, int>>& entries) {
    // Build string data and entry list
    std::string strings;
    std::vector<std::pair<uint32_t, uint32_t>> offsets; // (syllable_ids_off, text_off)
    std::vector<std::pair<uint32_t, uint32_t>> lens;    // (syllable_ids_len, text_len)
    std::vector<int> freqs;

    auto intern = [&strings](const std::string& s) -> std::pair<uint32_t, uint32_t> {
        uint32_t off = (uint32_t)strings.size();
        strings += s;
        return {off, (uint32_t)s.size()};
    };

    // Sort entries by syllable_ids for binary format
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end());

    for (auto& [sid, text, freq] : sorted) {
        auto [sio, sil] = intern(sid);
        auto [to, tl] = intern(text);
        offsets.push_back({sio, to});
        lens.push_back({sil, tl});
        freqs.push_back(freq);
    }

    uint32_t count = (uint32_t)sorted.size();
    uint32_t entries_offset = sizeof(DictHeader);
    uint32_t strings_offset = entries_offset + count * sizeof(DictEntry);

    // Write file
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD written;
    DictHeader hdr = {};
    std::memcpy(hdr.magic, DICT_MAGIC_V2, 8);
    hdr.version = 2;
    hdr.entry_count = count;
    hdr.string_data_size = (uint32_t)strings.size();
    hdr.entries_offset = entries_offset;
    hdr.strings_offset = strings_offset;
    WriteFile(hFile, &hdr, sizeof(hdr), &written, nullptr);

    for (uint32_t i = 0; i < count; ++i) {
        DictEntry de = {};
        de.syllable_ids_offset = offsets[i].first;
        de.text_offset = offsets[i].second;
        de.syllable_ids_len = lens[i].first;
        de.text_len = lens[i].second;
        de.frequency = freqs[i];
        WriteFile(hFile, &de, sizeof(de), &written, nullptr);
    }

    WriteFile(hFile, strings.data(), (DWORD)strings.size(), &written, nullptr);
    CloseHandle(hFile);
    return true;
}

// ─── Syllable ID index (librime-style integer-ID lookup) ─────────────

void Dict::build_syllabary() {
    syllabary_.clear();
    syllable_to_id_.clear();

    for (uint32_t i = 0; i < dict_entry_count_; ++i) {
        const auto& e = dict_entries_[i];
        const char* sid = dict_strings_ + e.syllable_ids_offset;
        uint32_t len = e.syllable_ids_len;

        // Split by ':'
        uint32_t start = 0;
        for (uint32_t j = 0; j <= len; ++j) {
            if (j == len || sid[j] == ':') {
                if (j > start) {
                    std::string syl(sid + start, j - start);
                    if (syllable_to_id_.find(syl) == syllable_to_id_.end()) {
                        syllable_to_id_[syl] = (uint32_t)syllabary_.size();
                        syllabary_.push_back(syl);
                    }
                }
                start = j + 1;
            }
        }
    }

    CXXIME_LOG(L"Dict::build_syllabary %zu syllables", syllabary_.size());
}

void Dict::build_id_index() {
    id_index_.clear();
    id_index_.reserve(dict_entry_count_);

    for (uint32_t i = 0; i < dict_entry_count_; ++i) {
        const auto& e = dict_entries_[i];
        const char* sid = dict_strings_ + e.syllable_ids_offset;
        uint32_t len = e.syllable_ids_len;

        IdEntry entry;
        entry.index = i;

        uint32_t start = 0;
        for (uint32_t j = 0; j <= len; ++j) {
            if (j == len || sid[j] == ':') {
                if (j > start) {
                    std::string syl(sid + start, j - start);
                    auto it = syllable_to_id_.find(syl);
                    if (it != syllable_to_id_.end())
                        entry.ids.push_back(it->second);
                }
                start = j + 1;
            }
        }

        id_index_.push_back(std::move(entry));
    }

    std::sort(id_index_.begin(), id_index_.end(),
        [](const IdEntry& a, const IdEntry& b) { return a.ids < b.ids; });

    CXXIME_LOG(L"Dict::build_id_index %zu entries", id_index_.size());
}

uint32_t Dict::syllable_to_id(const std::string& syllable) const {
    auto it = syllable_to_id_.find(syllable);
    return it != syllable_to_id_.end() ? it->second : UINT32_MAX;
}

bool Dict::has_prefix(const std::vector<uint32_t>& query_ids) const {
    if (query_ids.empty() || id_index_.empty())
        return false;
    uint32_t lo = 0, hi = (uint32_t)id_index_.size();
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (id_index_[mid].ids < query_ids)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= (uint32_t)id_index_.size())
        return false;
    const auto& ids = id_index_[lo].ids;
    if (ids.size() < query_ids.size())
        return false;
    return std::equal(query_ids.begin(), query_ids.end(), ids.begin());
}

std::vector<Candidate> Dict::lookup_by_ids(const std::vector<uint32_t>& query_ids, int limit) {
    std::vector<Candidate> results;
    if (query_ids.empty() || id_index_.empty())
        return results;

    // Binary search for first match (exact or prefix)
    uint32_t lo = 0, hi = (uint32_t)id_index_.size();
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (id_index_[mid].ids < query_ids)
            lo = mid + 1;
        else
            hi = mid;
    }

    std::unordered_set<std::string> seen;

    // First pass: collect ALL exact matches (no limit — need frequency sort)
    uint32_t pos = lo;
    while (pos < (uint32_t)id_index_.size() && id_index_[pos].ids == query_ids) {
        const auto& e = dict_entries_[id_index_[pos].index];
        Candidate c;
        c.text.assign(dict_strings_ + e.text_offset, e.text_len);
        c.frequency = e.frequency + 100000;  // exact match boost
        if (seen.insert(c.text).second)
            results.push_back(std::move(c));
        ++pos;
    }

    // Second pass: prefix matches
    while (pos < (uint32_t)id_index_.size()
           && id_index_[pos].ids.size() >= query_ids.size()
           && std::equal(query_ids.begin(), query_ids.end(), id_index_[pos].ids.begin())) {
        const auto& e = dict_entries_[id_index_[pos].index];
        Candidate c;
        c.text.assign(dict_strings_ + e.text_offset, e.text_len);
        c.frequency = e.frequency;
        if (seen.insert(c.text).second)
            results.push_back(std::move(c));
        ++pos;
    }

    std::sort(results.begin(), results.end(),
        [](const Candidate& a, const Candidate& b) { return a.frequency > b.frequency; });

    if ((int)results.size() > limit)
        results.resize(limit);
    return results;
}

} // namespace cxxime
