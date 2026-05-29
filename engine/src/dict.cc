// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/dict.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>
#include "binary_format.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <unordered_set>
#include <windows.h>
#include <shlobj.h>
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
    load_user_dict(user_dict_path);
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

    // Load entire file into heap memory (no mmap — avoids page-out latency)
    dict_data_ = new (std::nothrow) char[dict_data_size_];
    if (!dict_data_) {
        CloseHandle(hFile);
        CXXIME_LOG(L"Dict::open_dict allocation failed (%zu bytes)", dict_data_size_);
        return false;
    }

    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, dict_data_, (DWORD)dict_data_size_, &bytes_read, nullptr);
    CloseHandle(hFile);
    if (!ok || bytes_read != dict_data_size_) {
        CXXIME_LOG(L"Dict::open_dict ReadFile FAILED");
        unload_dict();
        return false;
    }

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

    // Try to load pre-built ID index (.dict.idx); build from scratch if absent
    if (!load_id_index(bin_path)) {
        build_syllabary();
        build_id_index();
    }
    return true;
}

// ─── User dictionary: in-memory + TSV persistence ───────────────────

static std::string default_user_dict_path() {
    wchar_t profile[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, profile) != S_OK)
        return {};
    std::wstring user_dir = std::wstring(profile) + L"\\cxxime";
    CreateDirectoryW(user_dir.c_str(), nullptr);
    std::wstring path = user_dir + L"\\user.tsv";
    char path_utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, path_utf8, sizeof(path_utf8), nullptr, nullptr);
    return path_utf8;
}

bool Dict::load_user_dict(const std::string& path) {
    std::unique_lock<std::shared_mutex> lock(user_mutex_);
    user_entries_.clear();
    user_text_index_.clear();

    user_dict_path_ = path.empty() ? default_user_dict_path() : path;
    if (user_dict_path_.empty())
        return false;

    FILE* f = fopen(user_dict_path_.c_str(), "r");
    if (!f)
        return true;  // First run, no file yet

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char* text = strtok(line, "\t");
        char* code = strtok(nullptr, "\t");
        char* freq = strtok(nullptr, "\t\n");
        if (!text || !code) continue;

        UserEntry e;
        e.text = text;
        e.code = code;
        e.frequency = freq ? atoi(freq) : 1;
        if (e.frequency < 1) e.frequency = 1;

        size_t idx = user_entries_.size();
        user_entries_.push_back(std::move(e));
        user_text_index_[text] = idx;
    }
    fclose(f);

    CXXIME_LOG(L"Dict::load_user_dict loaded %zu entries", user_entries_.size());
    return true;
}

bool Dict::save_user_dict() {
    std::shared_lock<std::shared_mutex> lock(user_mutex_);
    if (!user_dirty_.load() || user_dict_path_.empty())
        return true;

    lock.unlock();
    std::unique_lock<std::shared_mutex> wlock(user_mutex_);

    FILE* f = fopen(user_dict_path_.c_str(), "w");
    if (!f)
        return false;

    for (auto& e : user_entries_) {
        fprintf(f, "%s\t%s\t%d\n", e.text.c_str(), e.code.c_str(), e.frequency);
    }
    fclose(f);
    user_dirty_ = false;

    CXXIME_LOG(L"Dict::save_user_dict saved %zu entries", user_entries_.size());
    return true;
}

void Dict::unload_dict() {
    unload_id_index();
    delete[] dict_data_;
    dict_data_ = nullptr;
    dict_entries_ = nullptr;
    dict_strings_ = nullptr;
    dict_entry_count_ = 0;
    dict_data_size_ = 0;
}

void Dict::close() {
    save_user_dict();
    unload_dict();
}

std::vector<Candidate> Dict::lookup_by_syllables(
    const std::vector<std::string>& syllables, int limit, QueryTrace* trace) {
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
    if ((int)results.size() < limit) {
        std::string concat_code;
        for (auto& s : syllables) concat_code += s;

        std::shared_lock<std::shared_mutex> lock(user_mutex_);
        for (auto& e : user_entries_) {
            if (trace)
                ++trace->user_scan_count;
            if (e.code == concat_code) {
                Candidate c;
                c.text = e.text;
                c.frequency = e.frequency;
                if (seen.insert(c.text).second)
                    results.push_back(std::move(c));
                if ((int)results.size() >= limit)
                    break;
            }
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

std::vector<Candidate> Dict::lookup(const std::string& code_prefix, int limit, QueryTrace* trace) {
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
    {
        std::shared_lock<std::shared_mutex> lock(user_mutex_);
        for (auto& e : user_entries_) {
            if (trace)
                ++trace->user_scan_count;
            if (e.code.size() >= prefix_len &&
                std::memcmp(e.code.data(), prefix_data, prefix_len) == 0) {
                Candidate c;
                c.text = e.text;
                c.frequency = e.frequency + 50000;  // user entries boost below exact matches
                if (seen.insert(c.text).second)
                    results.push_back(std::move(c));
                if ((int)results.size() >= limit)
                    break;
            }
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

int Dict::count(const std::string& code_prefix, QueryTrace* trace) {
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

    {
        std::shared_lock<std::shared_mutex> lock(user_mutex_);
        for (auto& e : user_entries_) {
            if (trace)
                ++trace->user_scan_count;
            if (e.code.size() >= prefix_len &&
                std::memcmp(e.code.data(), prefix_data, prefix_len) == 0)
                ++result;
        }
    }

    return result;
}

std::string Dict::reverse_lookup(const std::string& text) {
    // Check user dict first (O(1) via text index)
    {
        std::shared_lock<std::shared_mutex> lock(user_mutex_);
        auto it = user_text_index_.find(text);
        if (it != user_text_index_.end() && it->second < user_entries_.size())
            return user_entries_[it->second].code;
    }

    if (!dict_entries_)
        return {};

    // Linear scan in binary dict
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
    std::unique_lock<std::shared_mutex> lock(user_mutex_);

    auto it = user_text_index_.find(text);
    if (it != user_text_index_.end() && it->second < user_entries_.size()) {
        user_entries_[it->second].frequency++;
    } else {
        UserEntry e;
        e.text = text;
        e.code = code;
        e.frequency = 1;
        size_t idx = user_entries_.size();
        user_entries_.push_back(std::move(e));
        user_text_index_[text] = idx;
    }
    user_dirty_ = true;
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

// ─── Syllable ID index (zero-copy mmap, v3 format) ────────────────────

void Dict::unload_id_index() {
    delete[] idx_data_;
    idx_data_ = nullptr;
    idx_data_size_ = 0;
    syllabary_.clear();
    syllable_to_id_.clear();
    id_index_.clear();
}

bool Dict::load_id_index(const std::string& dict_bin_path) {
    std::string idx_path = dict_bin_path;
    auto pos = idx_path.rfind(".dict.bin");
    if (pos == std::string::npos) {
        pos = idx_path.rfind(".dict.db");
        if (pos == std::string::npos)
            return false;
    }
    idx_path.replace(pos, std::string::npos, ".dict.idx");

    HANDLE hFile = CreateFileA(idx_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER li;
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 28) {
        CloseHandle(hFile);
        return false;
    }
    size_t file_size = (size_t)li.QuadPart;

    // Load entire file into heap memory
    idx_data_ = new (std::nothrow) char[file_size];
    if (!idx_data_) {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, idx_data_, (DWORD)file_size, &bytes_read, nullptr);
    CloseHandle(hFile);
    if (!ok || bytes_read != file_size) {
        delete[] idx_data_;
        idx_data_ = nullptr;
        return false;
    }
    idx_data_size_ = file_size;

    const char* base = idx_data_;

    // Header: magic(8) version(4) syl_count(4) syl_str_size(4) idx_count(4) idx_data_size(4) = 28
    if (std::memcmp(base, "CXIDX\0\0\0\0", 8) != 0) {
        unload_id_index();
        return false;
    }
    const uint32_t* h = (const uint32_t*)(base + 8);
    uint32_t ver = h[0], syl_count = h[1], syl_str_size = h[2];
    uint32_t idx_count = h[3], idx_data_size = h[4];
    if (ver < 2 || ver > 3) {
        unload_id_index();
        return false;
    }

    // Syllabary
    const uint32_t* syl_offs = (const uint32_t*)(base + 28);
    const char* syl_strs = (const char*)(syl_offs + syl_count);
    syllabary_.resize(syl_count);
    syllable_to_id_.clear();
    for (uint32_t i = 0; i < syl_count; ++i) {
        const char* s = syl_strs + syl_offs[i];
        syllabary_[i] = s;
        syllable_to_id_[s] = i;
    }

    const uint8_t* after_syl = (const uint8_t*)(syl_strs + syl_str_size);

    if (ver == 3) {
        // v3: zero-copy — offsets table + data section
        const uint32_t* id_offsets = (const uint32_t*)after_syl;
        const uint8_t*  id_data    = after_syl + idx_count * 4;

        id_index_.clear();
        id_index_.reserve(idx_count);
        for (uint32_t i = 0; i < idx_count; ++i) {
            const uint8_t* e = id_data + id_offsets[i];
            uint32_t cnt = *(const uint32_t*)e;
            id_index_.push_back({(const uint32_t*)(e + 4), cnt,
                                 *(const uint32_t*)(e + 4 + cnt * 4)});
        }
    } else {
        // v2: parse variable-length entries (backward compat)
        id_index_.clear();
        id_index_.reserve(idx_count);
        const uint8_t* p = after_syl;
        const uint8_t* end = p + idx_data_size;
        for (uint32_t i = 0; i < idx_count && p < end; ++i) {
            uint32_t cnt = *(const uint32_t*)p; p += 4;
            if (p + cnt * 4 + 4 > end) break;
            const uint32_t* ids = (const uint32_t*)p; p += cnt * 4;
            uint32_t idx = *(const uint32_t*)p; p += 4;
            id_index_.push_back({ids, cnt, idx});
        }
    }

    CXXIME_LOG(L"Dict::load_id_index v%u syllables=%u idx=%zu",
               ver, syl_count, id_index_.size());
    return true;
}

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
    runtime_ids_.clear();
    runtime_ids_.reserve(dict_entry_count_);
    id_index_.reserve(dict_entry_count_);

    for (uint32_t i = 0; i < dict_entry_count_; ++i) {
        const auto& e = dict_entries_[i];
        const char* sid = dict_strings_ + e.syllable_ids_offset;
        uint32_t len = e.syllable_ids_len;

        std::vector<uint32_t> ids;
        uint32_t start = 0;
        for (uint32_t j = 0; j <= len; ++j) {
            if (j == len || sid[j] == ':') {
                if (j > start) {
                    std::string syl(sid + start, j - start);
                    auto it = syllable_to_id_.find(syl);
                    if (it != syllable_to_id_.end())
                        ids.push_back(it->second);
                }
                start = j + 1;
            }
        }
        runtime_ids_.push_back(std::move(ids));
    }

    for (uint32_t i = 0; i < dict_entry_count_; ++i) {
        auto& v = runtime_ids_[i];
        if (!v.empty())
            id_index_.push_back({v.data(), (uint32_t)v.size(), i});
    }

    std::sort(id_index_.begin(), id_index_.end(),
        [](const IdEntry& a, const IdEntry& b) {
            uint32_t n = a.count < b.count ? a.count : b.count;
            for (uint32_t k = 0; k < n; ++k) {
                if (a.ids[k] < b.ids[k]) return true;
                if (a.ids[k] > b.ids[k]) return false;
            }
            return a.count < b.count;
        });

    CXXIME_LOG(L"Dict::build_id_index %zu entries", id_index_.size());
}

uint32_t Dict::syllable_to_id(const std::string& syllable) const {
    auto it = syllable_to_id_.find(syllable);
    return it != syllable_to_id_.end() ? it->second : UINT32_MAX;
}

bool Dict::has_prefix(const std::vector<uint32_t>& query_ids, QueryTrace* trace) const {
    if (query_ids.empty() || id_index_.empty())
        return false;
    auto ids_less = [&](const IdEntry& e, const std::vector<uint32_t>& q) {
        uint32_t n = e.count < q.size() ? e.count : (uint32_t)q.size();
        for (uint32_t k = 0; k < n; ++k) {
            if (e.ids[k] < q[k]) return true;
            if (e.ids[k] > q[k]) return false;
        }
        return e.count < q.size();
    };
    uint32_t lo = 0, hi = (uint32_t)id_index_.size();
    uint32_t steps = 0;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        ++steps;
        if (ids_less(id_index_[mid], query_ids))
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= (uint32_t)id_index_.size())
        return false;
    const auto& e = id_index_[lo];
    if (e.count < query_ids.size())
        return false;
    for (size_t k = 0; k < query_ids.size(); ++k)
        if (e.ids[k] != query_ids[k]) return false;

    if (trace)
        trace->prefix_scan_count += steps;

    return true;
}

std::vector<Candidate> Dict::lookup_by_ids(const std::vector<uint32_t>& query_ids, int limit,
                                            QueryTrace* trace, const QueryBudget* budget) {
    std::vector<Candidate> results;
    if (query_ids.empty() || id_index_.empty())
        return results;

    // Binary search: compare (ids_ptr, count) tuples
    auto ids_less = [&](const IdEntry& e, const std::vector<uint32_t>& q) {
        uint32_t n = e.count < q.size() ? e.count : (uint32_t)q.size();
        for (uint32_t k = 0; k < n; ++k) {
            if (e.ids[k] < q[k]) return true;
            if (e.ids[k] > q[k]) return false;
        }
        return e.count < q.size();
    };

    uint32_t lo = 0, hi = (uint32_t)id_index_.size();
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (ids_less(id_index_[mid], query_ids))
            lo = mid + 1;
        else
            hi = mid;
    }

    std::unordered_set<std::string> seen;
    bool deadline_hit = false;

    // First pass: collect ALL exact matches (no limit — need frequency sort)
    auto ids_eq = [&](const IdEntry& e) {
        if (e.count != query_ids.size()) return false;
        for (size_t k = 0; k < query_ids.size(); ++k)
            if (e.ids[k] != query_ids[k]) return false;
        return true;
    };

    uint32_t pos = lo;
    uint32_t exact_count = 0;
    while (pos < (uint32_t)id_index_.size() && ids_eq(id_index_[pos])) {
        // Check scan budget
        if (budget && exact_count >= budget->max_exact_scan) {
            if (trace) {
                trace->truncated = true;
            }
            break;
        }
        // Check deadline every 64 entries
        if (budget && (exact_count & 63) == 0 && budget->expired()) {
            deadline_hit = true;
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
            break;
        }
        const auto& e = dict_entries_[id_index_[pos].index];
        Candidate c;
        c.text.assign(dict_strings_ + e.text_offset, e.text_len);
        c.frequency = e.frequency + 100000;  // exact match boost
        if (seen.insert(c.text).second)
            results.push_back(std::move(c));
        ++pos;
        ++exact_count;
    }

    // Second pass: prefix matches (skip if deadline already hit)
    uint32_t prefix_count = 0;
    if (!deadline_hit) {
        auto ids_prefix = [&](const IdEntry& e) {
            if (e.count < query_ids.size()) return false;
            for (size_t k = 0; k < query_ids.size(); ++k)
                if (e.ids[k] != query_ids[k]) return false;
            return true;
        };

        while (pos < (uint32_t)id_index_.size() && ids_prefix(id_index_[pos])) {
            // Check scan budget
            if (budget && prefix_count >= budget->max_prefix_scan) {
                if (trace) {
                    trace->truncated = true;
                }
                break;
            }
            // Check deadline every 64 entries
            if (budget && (prefix_count & 63) == 0 && budget->expired()) {
                if (trace) {
                    trace->deadline_exceeded = true;
                    trace->truncated = true;
                }
                break;
            }
            const auto& e = dict_entries_[id_index_[pos].index];
            Candidate c;
            c.text.assign(dict_strings_ + e.text_offset, e.text_len);
            c.frequency = e.frequency;
            if (seen.insert(c.text).second)
                results.push_back(std::move(c));
            ++pos;
            ++prefix_count;
        }
    }

    if (trace) {
        trace->exact_scan_count += exact_count;
        trace->prefix_scan_count += prefix_count;
    }

    std::sort(results.begin(), results.end(),
        [](const Candidate& a, const Candidate& b) { return a.frequency > b.frequency; });

    if ((int)results.size() > limit)
        results.resize(limit);

    return results;
}

} // namespace cxxime
