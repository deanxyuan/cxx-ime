// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/dict.h>

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <set>

#include <windows.h>
#include <shlobj.h>

#include <cxxime/logging.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/topk_collector.h>

#include "binary_format.h"

static const char DICT_MAGIC_V1[] = "CXDIC\x01\x00\x00";
static const char DICT_MAGIC_V2[] = "CXDIC\x02\x00\x00";

namespace cxxime {

// Linear dedup — cheaper than hash set for bounded result vectors (≤128)
static bool contains_text(const std::vector<Candidate>& items, const std::string& text) {
    for (auto& c : items)
        if (c.text == text) return true;
    return false;
}

static void merge_candidate_by_score(std::vector<Candidate>& items, Candidate candidate) {
    for (auto& item : items) {
        if (item.text == candidate.text) {
            if (candidate.frequency > item.frequency)
                item = std::move(candidate);
            return;
        }
    }
    items.push_back(std::move(candidate));
}

static void sort_candidates_by_score(std::vector<Candidate>& items) {
    std::sort(items.begin(), items.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.frequency != b.frequency) return a.frequency > b.frequency;
            if (a.text.size() != b.text.size()) return a.text.size() < b.text.size();
            return a.text < b.text;
        });
}

static std::string compact_syllable_code(const char* syllable_ids, uint32_t len) {
    std::string code;
    code.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        if (syllable_ids[i] != ':')
            code.push_back(syllable_ids[i]);
    }
    return code;
}

static void set_candidate_code(Candidate& candidate, const char* syllable_ids, uint32_t len) {
    candidate.syllables.assign(syllable_ids, len);
    candidate.code = compact_syllable_code(syllable_ids, len);
}

enum class UserMatchKind {
    kExact,
    kPrefix,
    kAbbreviation,
    kMixed,
};

static constexpr size_t kMaxMaterializedUserPrefixLength = 6;

static int bounded_user_frequency(int frequency) {
    if (frequency < 1)
        return 1;
    return std::min(frequency, 50000);
}

static int user_recent_bonus(uint64_t current_sequence, uint64_t entry_sequence) {
    uint64_t delta = current_sequence >= entry_sequence
        ? current_sequence - entry_sequence
        : 0;
    return (delta <= 1000) ? (int)(1000 - delta) : 0;
}

static int score_user_match(UserScoringProfile profile, UserMatchKind kind,
                            size_t key_len, size_t code_len, int frequency,
                            uint64_t current_sequence,
                            uint64_t entry_sequence) {
    static constexpr int kExactBase = 200000000;
    static constexpr int kPatternBase = 120000000;
    static constexpr int kNearPrefixBase = 800000;
    static constexpr int kMidPrefixBase = 160000;
    static constexpr int kWeakPrefixBase = 4000;

    int base = kWeakPrefixBase;
    if (kind == UserMatchKind::kExact || key_len == code_len) {
        base = kExactBase;
    } else if (kind == UserMatchKind::kAbbreviation ||
               kind == UserMatchKind::kMixed) {
        base = kPatternBase;
    } else if (profile == UserScoringProfile::kWubi) {
        base = kWeakPrefixBase;
    } else if (kind == UserMatchKind::kPrefix) {
        if (key_len <= 2) {
            base = kWeakPrefixBase;
        } else if (key_len + 1 >= code_len) {
            base = kNearPrefixBase;
        } else if (key_len * 2 >= code_len) {
            base = kMidPrefixBase;
        } else {
            base = kWeakPrefixBase;
        }
    }

    return base + bounded_user_frequency(frequency) +
           user_recent_bonus(current_sequence, entry_sequence);
}

Dict::~Dict() {
    close();
}

bool Dict::open(const std::string& dict_path, const std::string& user_dict_path) {
    if (!open_dict(dict_path))
        return false;
    load_user_dict(user_dict_path);
    return true;
}

bool Dict::open_bundle(const std::string& dict_path,
                       const std::string& user_dict_path,
                       const std::string& idx_path,
                       const std::string& topn_path) {
    if (!open_dict_with_aux(dict_path, idx_path, topn_path, false))
        return false;
    load_user_dict(user_dict_path);
    return true;
}

bool Dict::is_open() const {
    return dict_data_ != nullptr;
}

bool Dict::open_dict(const std::string& bin_path) {
    return open_dict_with_aux(bin_path, {}, {}, true);
}

bool Dict::open_dict_with_aux(const std::string& bin_path,
                              const std::string& idx_path,
                              const std::string& topn_path,
                              bool derive_aux_paths) {
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
    bool index_loaded = false;
    if (derive_aux_paths) {
        index_loaded = load_id_index(bin_path);
    } else if (!idx_path.empty()) {
        index_loaded = load_id_index_file(idx_path);
        if (!index_loaded) {
            CXXIME_LOG(L"Dict::open_dict manifest idx not loaded");
            unload_dict();
            return false;
        }
    }
    if (!index_loaded) {
        build_syllabary();
        build_id_index();
    }

    // Try to load the pre-built Top-N index (pinyin.topn.bin).
    if (derive_aux_paths) {
        // Derive path from dict_bin_path: pinyin.dict.bin -> pinyin.topn.bin
        std::string topn_path = bin_path;
        auto pos = topn_path.rfind(".dict.bin");
        if (pos != std::string::npos)
            topn_path.replace(pos, std::string::npos, ".topn.bin");
        else
            topn_path += ".topn.bin";
        if (!short_cache_.load(topn_path)) {
            CXXIME_LOG(L"Dict::open_dict Top-N index not loaded (standalone mode)");
            // Not fatal for standalone tools/tests. Server runtime uses open_bundle()
            // with manifest-declared topn_path and treats load failure as fatal.
        }
    } else if (!topn_path.empty()) {
        if (!short_cache_.load(topn_path)) {
            CXXIME_LOG(L"Dict::open_dict manifest topn not loaded");
            unload_dict();
            return false;
        }
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
    std::wstring path = user_dir + L"\\user_pinyin.tsv";
    char path_utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, path_utf8, sizeof(path_utf8), nullptr, nullptr);
    return path_utf8;
}

bool Dict::load_user_dict(const std::string& path) {
    std::unique_lock<std::shared_mutex> lock(user_mutex_);
    user_entries_.clear();
    user_text_index_.clear();
    user_exact_index_.clear();
    user_prefix_index_.clear();
    user_abbr_index_.clear();
    user_mixed_index_.clear();
    user_code_sorted_.clear();

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
        char* syl = strtok(nullptr, "\n");
        if (!text || !code) continue;

        UserEntry e;
        e.text = text;
        e.code = code;
        e.frequency = freq ? atoi(freq) : 1;
        if (e.frequency < 1) e.frequency = 1;
        if (syl) {
            // Trim trailing whitespace/newline
            std::string s(syl);
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
                s.pop_back();
            e.syllables = s;
        }

        user_entries_.push_back(std::move(e));
    }
    fclose(f);

    // Rebuild all indexes
    rebuild_user_indexes_locked();
    user_dict_version_++;

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
        if (e.deleted) continue;
        if (e.syllables.empty())
            fprintf(f, "%s\t%s\t%d\n", e.text.c_str(), e.code.c_str(), e.frequency);
        else
            fprintf(f, "%s\t%s\t%d\t%s\n", e.text.c_str(), e.code.c_str(), e.frequency, e.syllables.c_str());
    }
    fclose(f);
    user_dirty_ = false;

    CXXIME_LOG(L"Dict::save_user_dict saved %zu entries", user_entries_.size());
    return true;
}

// ─── Phase 5: User dictionary index helpers ───────────────────────

// Generate abbreviation from colon-separated syllables: "shu:ru:fa" → "srf"
static std::string make_abbr(const std::string& syllables) {
    std::string abbr;
    for (size_t i = 0; i < syllables.size(); ++i) {
        if (i == 0 || syllables[i - 1] == ':')
            abbr += syllables[i];
    }
    return abbr;
}

// Rewrite by removing adjacent duplicate characters (sddf -> sdf)
static std::string dedup_adjacent(const std::string& s) {
    if (s.size() <= 1) return s;
    std::string result;
    result.reserve(s.size());
    result += s[0];
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] != s[i - 1])
            result += s[i];
    }
    return result;
}

// Generate mixed-code keys targeting common abbreviation patterns.
// Patterns:
//   1. Enhanced initial: zh/ch/sh -> two-letter, others -> first letter  (shu:ru:fa -> shrf)
//   2. Long phrase head (5+ syls): first letter of each syllable         (zhong:hua:ren:min:gong:he:guo -> zhrmghg)
//   B: first syllable full + rest first letter    (shu:ru:fa -> shurf)
//   C: first 2 full + rest first letter            (bei:jing:da:xue -> beijidx)
//   D: first letter + second full + rest first     (bei:jing:da:xue -> bjingdx)
//   E: first 2 chars + rest first letter           (shu:ru:fa -> shrf)
static std::vector<std::string> generate_mixed_keys(const std::string& syllables, size_t max_keys = 8) {
    // Split syllables by ':'
    std::vector<std::string> syls;
    std::string cur;
    for (char c : syllables) {
        if (c == ':') {
            if (!cur.empty()) syls.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) syls.push_back(cur);
    if (syls.empty()) return {};
    if (syls.size() == 1) return {};

    size_t n = syls.size();
    std::set<std::string> results;

    // Pattern 1: Enhanced initial (声母增强简拼)
    // For zh/ch/sh syllables, use two-letter initial; otherwise first letter only.
    {
        std::string enhanced;
        for (auto& s : syls) {
            if (s.size() >= 2 && ((s[0] == 'z' && s[1] == 'h') ||
                                  (s[0] == 'c' && s[1] == 'h') ||
                                  (s[0] == 's' && s[1] == 'h'))) {
                enhanced += s.substr(0, 2);
            } else {
                enhanced += s[0];
            }
        }
        results.insert(enhanced);
    }

    // Pattern 2: Long phrase head (长词首字母码, 5+ syllables)
    if (n >= 5) {
        std::string head;
        for (auto& s : syls) head += s[0];
        results.insert(head);
    }

    // Rest first letters
    std::string rest_first;
    for (size_t i = 1; i < n; ++i) rest_first += syls[i][0];

    // Mode B: first syllable full + rest first letter
    results.insert(syls[0] + rest_first);

    // Mode C: first 2 syllables full + rest first letter (3+ syllables)
    if (n >= 3) {
        std::string rest_first_from_3;
        for (size_t i = 2; i < n; ++i) rest_first_from_3 += syls[i][0];
        results.insert(syls[0] + syls[1] + rest_first_from_3);
    }

    // Mode D: first letter + second syllable full + rest first letter (3+ syllables)
    if (n >= 3) {
        std::string rest_first_from_3;
        for (size_t i = 2; i < n; ++i) rest_first_from_3 += syls[i][0];
        results.insert(std::string(1, syls[0][0]) + syls[1] + rest_first_from_3);
    }

    // Mode E: first 2 chars of first syllable + rest first letter
    if (syls[0].size() >= 2) {
        results.insert(syls[0].substr(0, 2) + rest_first);
    }

    // Remove exact (handled by exact index)
    // Keep abbr in mixed — design doc requires srf/shrf/shurf all in mixed generator
    std::string exact;
    for (char c : syllables) if (c != ':') exact += c;
    results.erase(exact);
    results.erase("");

    std::vector<std::string> filtered(results.begin(), results.end());
    if (filtered.size() > max_keys) filtered.resize(max_keys);
    return filtered;
}

// Bucket sorting: entries ordered by (frequency desc, sequence desc, id asc)
static constexpr size_t kMaxUserBucketSize = 64;

void Dict::bucket_insert_sorted_(UserBucket& bucket, UserEntryId id) {
    auto cmp = [this](UserEntryId a, UserEntryId b) {
        auto& ea = user_entries_[a];
        auto& eb = user_entries_[b];
        if (ea.frequency != eb.frequency) return ea.frequency > eb.frequency;
        if (ea.sequence != eb.sequence) return ea.sequence > eb.sequence;
        return a < b;
    };
    auto it = std::lower_bound(bucket.ids.begin(), bucket.ids.end(), id, cmp);
    bucket.ids.insert(it, id);
}

void Dict::sort_bucket_(UserBucket& bucket) {
    std::sort(bucket.ids.begin(), bucket.ids.end(),
        [this](UserEntryId a, UserEntryId b) {
            auto& ea = user_entries_[a];
            auto& eb = user_entries_[b];
            if (ea.frequency != eb.frequency) return ea.frequency > eb.frequency;
            if (ea.sequence != eb.sequence) return ea.sequence > eb.sequence;
            return a < b;
        });
}

void Dict::re_sort_user_buckets_(UserEntryId id) {
    auto& e = user_entries_[id];

    // Re-sort exact bucket
    {
        auto it = user_exact_index_.find(e.code);
        if (it != user_exact_index_.end()) sort_bucket_(it->second);
    }

    // Re-sort prefix buckets
    size_t max_prefix = std::min(e.code.size(), kMaxMaterializedUserPrefixLength);
    for (size_t len = 1; len <= max_prefix; ++len) {
        auto it = user_prefix_index_.find(e.code.substr(0, len));
        if (it != user_prefix_index_.end()) sort_bucket_(it->second);
    }

    // Re-sort abbr bucket
    if (!e.abbr_code.empty()) {
        auto it = user_abbr_index_.find(e.abbr_code);
        if (it != user_abbr_index_.end()) sort_bucket_(it->second);
    }

    // Re-sort mixed buckets
    for (auto& mixed : e.mixed_keys) {
        auto it = user_mixed_index_.find(mixed);
        if (it != user_mixed_index_.end()) sort_bucket_(it->second);
    }
}

void Dict::rebuild_user_indexes_locked() {
    user_exact_index_.clear();
    user_prefix_index_.clear();
    user_abbr_index_.clear();
    user_mixed_index_.clear();
    user_code_sorted_.clear();
    user_text_index_.clear();

    for (size_t i = 0; i < user_entries_.size(); ++i) {
        auto& e = user_entries_[i];
        if (e.deleted) continue;
        user_text_index_[e.text] = i;
        insert_user_into_indexes((UserEntryId)i);
    }

    // Sort all buckets by (frequency desc, sequence desc) after bulk insert
    for (auto& [k, bucket] : user_exact_index_) sort_bucket_(bucket);
    for (auto& [k, bucket] : user_prefix_index_) sort_bucket_(bucket);
    for (auto& [k, bucket] : user_abbr_index_) sort_bucket_(bucket);
    for (auto& [k, bucket] : user_mixed_index_) sort_bucket_(bucket);

    std::sort(user_code_sorted_.begin(), user_code_sorted_.end(),
        [this](UserEntryId a, UserEntryId b) {
            return user_entries_[a].code < user_entries_[b].code;
        });
}

void Dict::insert_user_into_indexes(UserEntryId id) {
    auto& e = user_entries_[id];
    if (e.deleted) return;

    // exact index (sorted insert by frequency/sequence)
    bucket_insert_sorted_(user_exact_index_[e.code], id);

    // Materialize only the hot prefix range; longer prefixes use code_sorted_.
    size_t max_prefix = std::min(e.code.size(), kMaxMaterializedUserPrefixLength);
    for (size_t len = 1; len <= max_prefix; ++len) {
        bucket_insert_sorted_(user_prefix_index_[e.code.substr(0, len)], id);
    }

    // abbr and mixed indexes (require syllables)
    if (!e.syllables.empty()) {
        std::string abbr = make_abbr(e.syllables);
        if (!abbr.empty()) {
            e.abbr_code = abbr;
            bucket_insert_sorted_(user_abbr_index_[abbr], id);
        }
        auto mixed_keys = generate_mixed_keys(e.syllables);
        e.mixed_keys = mixed_keys;  // cache for bucket re-sort
        for (auto& mixed : mixed_keys) {
            if (!mixed.empty()) {
                auto& bucket = user_mixed_index_[mixed];
                bucket_insert_sorted_(bucket, id);
                // Trim mixed bucket to max size
                if (bucket.ids.size() > kMaxUserBucketSize)
                    bucket.ids.resize(kMaxUserBucketSize);
            }
        }
    }

    // code_sorted_ will be sorted by caller after bulk insert
    user_code_sorted_.push_back(id);
}

void Dict::remove_user_from_indexes(UserEntryId id) {
    auto& e = user_entries_[id];

    // Remove from exact index
    auto it = user_exact_index_.find(e.code);
    if (it != user_exact_index_.end()) {
        auto& ids = it->second.ids;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        if (ids.empty()) user_exact_index_.erase(it);
    }

    // Remove from prefix index
    size_t max_prefix = std::min(e.code.size(), kMaxMaterializedUserPrefixLength);
    for (size_t len = 1; len <= max_prefix; ++len) {
        std::string key = e.code.substr(0, len);
        auto pit = user_prefix_index_.find(key);
        if (pit != user_prefix_index_.end()) {
            auto& ids = pit->second.ids;
            ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
            if (ids.empty()) user_prefix_index_.erase(pit);
        }
    }

    // Remove from abbr index
    if (!e.abbr_code.empty()) {
        auto ait = user_abbr_index_.find(e.abbr_code);
        if (ait != user_abbr_index_.end()) {
            auto& ids = ait->second.ids;
            ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
            if (ids.empty()) user_abbr_index_.erase(ait);
        }
    }

    // Remove from mixed index (all generated mixed keys)
    if (!e.syllables.empty()) {
        auto mixed_keys = generate_mixed_keys(e.syllables);
        for (auto& mixed : mixed_keys) {
            auto mit = user_mixed_index_.find(mixed);
            if (mit != user_mixed_index_.end()) {
                auto& ids = mit->second.ids;
                ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
                if (ids.empty()) user_mixed_index_.erase(mit);
            }
        }
    }

    // Remove from code_sorted_
    auto sit = std::find(user_code_sorted_.begin(), user_code_sorted_.end(), id);
    if (sit != user_code_sorted_.end())
        user_code_sorted_.erase(sit);

    e.deleted = true;
}

void Dict::unload_dict() {
    unload_id_index();
    short_cache_.unload();
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
    std::string concat_code;
    for (const auto& s : syllables)
        concat_code += s;
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
    while (lo < dict_entry_count_) {
        const auto& e = dict_entries_[lo];
        if (e.syllable_ids_len != key_len)
            break;
        if (std::memcmp(dict_strings_ + e.syllable_ids_offset, key_data, key_len) != 0)
            break;

        Candidate c;
        c.text.assign(dict_strings_ + e.text_offset, e.text_len);
        c.code = concat_code;
        c.syllables = key;
        c.frequency = e.frequency;
        if (!contains_text(results, c.text)) {
            merge_candidate_by_score(results, std::move(c));
            if ((int)results.size() >= limit)
                break;
        }
        ++lo;
    }

    // Phase 5: query user dict via exact index
    QueryBudget ub;
    UserLookupStats ustats;
    auto user_results = lookup_user_exact(concat_code, limit, ub, trace, &ustats);
    for (auto& c : user_results) {
        merge_candidate_by_score(results, std::move(c));
    }

    // Sort by frequency descending
    sort_candidates_by_score(results);

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
    while (lo < dict_entry_count_ && (int)results.size() < limit) {
        const auto& e = dict_entries_[lo];
        if (e.syllable_ids_len < prefix_len)
            break;
        if (std::memcmp(dict_strings_ + e.syllable_ids_offset, prefix_data, prefix_len) != 0)
            break;

        Candidate c;
        c.text.assign(dict_strings_ + e.text_offset, e.text_len);
        set_candidate_code(c, dict_strings_ + e.syllable_ids_offset,
                           e.syllable_ids_len);
        // Exact match first, then shorter codes before longer codes,
        // then by original frequency. Encode as: exact*100000 + (100-len)*100 + freq
        c.frequency = (e.syllable_ids_len == prefix_len ? 100000 : 0)
                    + (100 - (int)e.syllable_ids_len) * 100
                    + e.frequency;
        merge_candidate_by_score(results, std::move(c));
        ++lo;
    }

    // Phase 5: query user dict via prefix index
    {
        QueryBudget ub;
        UserLookupStats ustats;
        auto user_results = lookup_user_prefix(code_prefix, limit, ub, trace, &ustats);
        for (auto& c : user_results) {
            merge_candidate_by_score(results, std::move(c));
        }
    }

    sort_candidates_by_score(results);

    if ((int)results.size() > limit)
        results.resize(limit);

    return results;
}

// Budget-aware overload: delegates to non-budget version, adds deadline check
std::vector<Candidate> Dict::lookup(const std::string& code_prefix, int limit,
                                     const QueryBudget& budget, QueryTrace* trace) {
    if (budget.deadline.enabled && budget.deadline.expired()) {
        if (trace) {
            trace->deadline_exceeded = true;
            trace->truncated = true;
        }
        return {};
    }
    return lookup(code_prefix, limit, trace);
}

// Budget-aware overload: delegates to non-budget version, adds deadline check
std::vector<Candidate> Dict::lookup_by_syllables(const std::vector<std::string>& syllables,
                                                   int limit, const QueryBudget& budget,
                                                   QueryTrace* trace) {
    if (budget.deadline.enabled && budget.deadline.expired()) {
        if (trace) {
            trace->deadline_exceeded = true;
            trace->truncated = true;
        }
        return {};
    }
    return lookup_by_syllables(syllables, limit, trace);
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

    // Phase 5: count user dict via index
    {
        std::shared_lock<std::shared_mutex> lock(user_mutex_);
        if (code_prefix.size() <= kMaxMaterializedUserPrefixLength) {
            auto it = user_prefix_index_.find(code_prefix);
            if (it != user_prefix_index_.end()) {
                for (auto id : it->second.ids) {
                    if (!user_entries_[id].deleted)
                        ++result;
                }
                if (trace)
                    trace->user_scan_count += (uint32_t)it->second.ids.size();
            }
        } else {
            // Binary search user_code_sorted_ for prefix range
            auto lo = std::lower_bound(user_code_sorted_.begin(), user_code_sorted_.end(),
                code_prefix, [this](UserEntryId id, const std::string& val) {
                    return user_entries_[id].code < val;
                });
            uint32_t scan = 0;
            for (auto it = lo; it != user_code_sorted_.end(); ++it) {
                auto& e = user_entries_[*it];
                if (e.code.compare(0, code_prefix.size(), code_prefix) != 0)
                    break;
                if (!e.deleted)
                    ++result;
                ++scan;
            }
            if (trace)
                trace->user_scan_count += scan;
        }
    }

    return result;
}

std::string Dict::reverse_lookup(const std::string& text) {
    // Check user dict first (O(1) via text index)
    {
        std::shared_lock<std::shared_mutex> lock(user_mutex_);
        auto it = user_text_index_.find(text);
        if (it != user_text_index_.end() && it->second < user_entries_.size() &&
            !user_entries_[it->second].deleted)
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

bool Dict::has_user_entry(const std::string& text) const {
    std::shared_lock<std::shared_mutex> lock(user_mutex_);
    auto it = user_text_index_.find(text);
    return it != user_text_index_.end() && it->second < user_entries_.size() &&
           !user_entries_[it->second].deleted;
}

size_t Dict::user_entry_count() const {
    std::shared_lock<std::shared_mutex> lock(user_mutex_);
    size_t count = 0;
    for (const auto& e : user_entries_) {
        if (!e.deleted)
            ++count;
    }
    return count;
}

std::vector<UserDictEntryInfo> Dict::query_user_entries(const std::string& query,
                                                        size_t limit) const {
    std::vector<UserDictEntryInfo> results;
    if (limit == 0)
        return results;

    std::shared_lock<std::shared_mutex> lock(user_mutex_);
    for (const auto& e : user_entries_) {
        if (e.deleted)
            continue;

        bool matched = query.empty();
        if (!matched) {
            matched = e.text.find(query) != std::string::npos ||
                      e.code.find(query) != std::string::npos;
        }
        if (!matched)
            continue;

        UserDictEntryInfo info;
        info.text = e.text;
        info.code = e.code;
        info.frequency = e.frequency;
        info.sequence = e.sequence;
        results.push_back(std::move(info));
    }

    std::sort(results.begin(), results.end(),
        [](const UserDictEntryInfo& a, const UserDictEntryInfo& b) {
            if (a.sequence != b.sequence) return a.sequence > b.sequence;
            if (a.frequency != b.frequency) return a.frequency > b.frequency;
            if (a.code != b.code) return a.code < b.code;
            return a.text < b.text;
        });
    if (results.size() > limit)
        results.resize(limit);
    return results;
}

bool Dict::delete_user_entry(const std::string& text, const std::string& code) {
    std::unique_lock<std::shared_mutex> lock(user_mutex_);
    auto it = user_text_index_.find(text);
    if (it == user_text_index_.end() || it->second >= user_entries_.size())
        return false;

    auto id = static_cast<UserEntryId>(it->second);
    auto& e = user_entries_[id];
    if (e.deleted)
        return false;
    if (!code.empty() && e.code != code)
        return false;

    remove_user_from_indexes(id);
    user_text_index_.erase(text);
    user_dirty_ = true;
    user_dict_version_++;
    return true;
}

bool Dict::replace_user_entry(const std::string& old_text, const std::string& old_code,
    const std::string& new_text, const std::string& new_code) {
    if (new_text.empty() || new_code.empty())
        return false;

    std::unique_lock<std::shared_mutex> lock(user_mutex_);
    auto old_it = user_text_index_.find(old_text);
    if (old_it == user_text_index_.end() || old_it->second >= user_entries_.size())
        return false;

    auto old_id = static_cast<UserEntryId>(old_it->second);
    auto& old_entry = user_entries_[old_id];
    if (old_entry.deleted)
        return false;
    if (!old_code.empty() && old_entry.code != old_code)
        return false;

    auto existing_it = user_text_index_.find(new_text);
    if (existing_it != user_text_index_.end() && existing_it->second < user_entries_.size() &&
        existing_it->second != old_id && !user_entries_[existing_it->second].deleted) {
        return false;
    }

    remove_user_from_indexes(old_id);
    user_text_index_.erase(old_text);

    old_entry.text = new_text;
    old_entry.code = new_code;
    old_entry.deleted = false;
    old_entry.sequence = ++user_sequence_;
    user_text_index_[new_text] = old_id;
    insert_user_into_indexes(old_id);
    std::sort(user_code_sorted_.begin(), user_code_sorted_.end(),
        [this](UserEntryId a, UserEntryId b) {
            return user_entries_[a].code < user_entries_[b].code;
        });

    user_dirty_ = true;
    user_dict_version_++;
    return true;
}

void Dict::update_frequency(const std::string& text, const std::string& code) {
    // Best-effort: no syllables available from 2-arg call
    update_frequency(text, code, "");
}

void Dict::update_frequency(const std::string& text, const std::string& code,
                            const std::string& syllables) {
    std::unique_lock<std::shared_mutex> lock(user_mutex_);

    auto it = user_text_index_.find(text);
    if (it != user_text_index_.end() && it->second < user_entries_.size()) {
        auto& e = user_entries_[it->second];
        if (e.code != code) {
            // Code changed: rebuild indexes for this entry
            remove_user_from_indexes((UserEntryId)it->second);
            e.code = code;
            e.syllables = syllables;
            e.deleted = false;
            insert_user_into_indexes((UserEntryId)it->second);
            // Re-sort code_sorted_ after modification
            std::sort(user_code_sorted_.begin(), user_code_sorted_.end(),
                [this](UserEntryId a, UserEntryId b) {
                    return user_entries_[a].code < user_entries_[b].code;
                });
        }
        e.frequency++;
        e.sequence = ++user_sequence_;
        // Update syllables if we got new info (must happen before re-sort)
        if (!syllables.empty() && e.syllables.empty()) {
            e.syllables = syllables;
            // Re-insert to populate abbr/mixed indexes
            remove_user_from_indexes((UserEntryId)it->second);
            e.deleted = false;
            insert_user_into_indexes((UserEntryId)it->second);
            std::sort(user_code_sorted_.begin(), user_code_sorted_.end(),
                [this](UserEntryId a, UserEntryId b) {
                    return user_entries_[a].code < user_entries_[b].code;
                });
        }
        // Re-sort affected buckets to keep high-frequency entries at front
        re_sort_user_buckets_((UserEntryId)it->second);
    } else {
        UserEntry e;
        e.text = text;
        e.code = code;
        e.syllables = syllables;
        e.frequency = 1;
        e.sequence = ++user_sequence_;
        size_t idx = user_entries_.size();
        user_entries_.push_back(std::move(e));
        user_text_index_[text] = idx;
        insert_user_into_indexes((UserEntryId)idx);
        // Re-sort code_sorted_ after insert
        std::sort(user_code_sorted_.begin(), user_code_sorted_.end(),
            [this](UserEntryId a, UserEntryId b) {
                return user_entries_[a].code < user_entries_[b].code;
            });
    }
    user_dirty_ = true;
    user_dict_version_++;
}

// ─── Phase 5: Indexed user dict query methods ─────────────────────

std::vector<Candidate> Dict::lookup_user_exact(
    const std::string& code, int limit,
    const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const {
    std::vector<Candidate> results;
    std::shared_lock<std::shared_mutex> lock(user_mutex_);

    auto it = user_exact_index_.find(code);
    if (it == user_exact_index_.end())
        return results;

    // Collect all valid entries with scores, then sort by score descending.
    // This ensures high-frequency entries are seen within max_user_scan budget.
    struct ScoredId { UserEntryId id; int score; };
    std::vector<ScoredId> scored;
    scored.reserve(it->second.ids.size());
    for (auto id : it->second.ids) {
        auto& e = user_entries_[id];
        if (e.deleted) continue;
            scored.push_back({id, score_user_match(user_scoring_profile_,
                                                       UserMatchKind::kExact, code.size(),
                                                       e.code.size(), e.frequency,
                                                       user_sequence_, e.sequence)});
    }
    std::sort(scored.begin(), scored.end(),
        [](const ScoredId& a, const ScoredId& b) { return a.score > b.score; });

    for (auto& s : scored) {
        if (stats->scan_count >= budget.max_user_scan) {
            stats->truncated = true;
            stats->scan_budget_truncated = true;
            break;
        }
        if (budget.deadline.enabled && budget.deadline.expired()) {
            stats->deadline_exceeded = true;
            stats->truncated = true;
            break;
        }
        ++stats->scan_count;
        Candidate c;
        c.text = user_entries_[s.id].text;
        c.code = user_entries_[s.id].code;
        c.syllables = user_entries_[s.id].syllables;
        c.frequency = s.score;
        c.origin = CandidateOrigin::kUser;
        results.push_back(std::move(c));
        if ((int)results.size() >= limit)
            break;
    }

    if (trace) {
        trace->user_scan_count += stats->scan_count;
        if (stats->truncated) trace->truncated = true;
        if (stats->scan_budget_truncated) trace->scan_budget_truncated = true;
        if (stats->deadline_exceeded) trace->deadline_exceeded = true;
    }
    return results;
}

std::vector<Candidate> Dict::lookup_user_prefix(
    const std::string& prefix, int limit,
    const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const {
    std::vector<Candidate> results;
    std::shared_lock<std::shared_mutex> lock(user_mutex_);

    if (prefix.size() <= kMaxMaterializedUserPrefixLength) {
        // Use prefix index
        auto it = user_prefix_index_.find(prefix);
        if (it == user_prefix_index_.end())
            return results;

        // Collect all valid entries with scores, then sort by score descending.
        struct ScoredId { UserEntryId id; int score; };
        std::vector<ScoredId> scored;
        scored.reserve(it->second.ids.size());
        for (auto id : it->second.ids) {
            auto& e = user_entries_[id];
            if (e.deleted) continue;
            scored.push_back({id, score_user_match(user_scoring_profile_,
                                                       UserMatchKind::kPrefix,
                                                       prefix.size(), e.code.size(),
                                                       e.frequency, user_sequence_,
                                                       e.sequence)});
        }
        std::sort(scored.begin(), scored.end(),
            [](const ScoredId& a, const ScoredId& b) { return a.score > b.score; });

        for (auto& s : scored) {
            if (stats->scan_count >= budget.max_user_scan) {
                stats->truncated = true;
                stats->scan_budget_truncated = true;
                break;
            }
            if (budget.deadline.enabled && budget.deadline.expired()) {
                stats->deadline_exceeded = true;
                stats->truncated = true;
                break;
            }
            ++stats->scan_count;
            Candidate c;
            c.text = user_entries_[s.id].text;
            c.code = user_entries_[s.id].code;
            c.syllables = user_entries_[s.id].syllables;
            c.frequency = s.score;
            c.origin = CandidateOrigin::kUser;
            results.push_back(std::move(c));
            if ((int)results.size() >= limit)
                break;
        }
    } else {
        // Binary search user_code_sorted_ for prefix range
        auto lo = std::lower_bound(user_code_sorted_.begin(), user_code_sorted_.end(),
            prefix, [this](UserEntryId id, const std::string& val) {
                return user_entries_[id].code < val;
            });

        struct ScoredId { UserEntryId id; int score; };
        std::vector<ScoredId> scored;
        for (auto it = lo; it != user_code_sorted_.end(); ++it) {
            auto& e = user_entries_[*it];
            if (e.code.compare(0, prefix.size(), prefix) != 0)
                break;
            if (stats->scan_count >= budget.max_user_scan) {
                stats->truncated = true;
                stats->scan_budget_truncated = true;
                break;
            }
            if (budget.deadline.enabled && budget.deadline.expired()) {
                stats->deadline_exceeded = true;
                stats->truncated = true;
                break;
            }
            if (e.deleted) continue;
            ++stats->scan_count;
            scored.push_back({*it, score_user_match(user_scoring_profile_,
                                                    UserMatchKind::kPrefix,
                                                    prefix.size(), e.code.size(),
                                                    e.frequency, user_sequence_,
                                                    e.sequence)});
        }

        std::sort(scored.begin(), scored.end(),
            [](const ScoredId& a, const ScoredId& b) { return a.score > b.score; });

        for (auto& s : scored) {
            Candidate c;
            c.text = user_entries_[s.id].text;
            c.code = user_entries_[s.id].code;
            c.syllables = user_entries_[s.id].syllables;
            c.frequency = s.score;
            c.origin = CandidateOrigin::kUser;
            results.push_back(std::move(c));
            if ((int)results.size() >= limit)
                break;
        }
    }

    sort_candidates_by_score(results);
    if (trace) {
        trace->user_scan_count += stats->scan_count;
        if (stats->truncated) trace->truncated = true;
        if (stats->scan_budget_truncated) trace->scan_budget_truncated = true;
        if (stats->deadline_exceeded) trace->deadline_exceeded = true;
    }
    return results;
}

std::vector<Candidate> Dict::lookup_user_indexed(
    const std::string& key, int limit,
    const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const {
    std::vector<Candidate> results;
    std::shared_lock<std::shared_mutex> lock(user_mutex_);

    auto add_candidate = [&](UserEntryId id, int score) {
        auto& e = user_entries_[id];
        Candidate c;
        c.text = e.text;
        c.code = e.code;
        c.syllables = e.syllables;
        c.frequency = score;
        c.origin = CandidateOrigin::kUser;
        merge_candidate_by_score(results, std::move(c));
    };

    auto try_add = [&](UserEntryId id, UserMatchKind kind,
                       const std::string& match_key) -> bool {
        if (stats->scan_count >= budget.max_user_scan) {
            stats->truncated = true;
            stats->scan_budget_truncated = true;
            return false;
        }
        if (budget.deadline.enabled && budget.deadline.expired()) {
            stats->deadline_exceeded = true;
            stats->truncated = true;
            return false;
        }
        auto& e = user_entries_[id];
        if (e.deleted) return true;
        ++stats->scan_count;
        add_candidate(id, score_user_match(user_scoring_profile_, kind,
                                           match_key.size(), e.code.size(),
                                           e.frequency, user_sequence_, e.sequence));
        return (int)results.size() < limit;
    };

    auto try_prefix = [&](const std::string& prefix) {
        if ((int)results.size() >= limit) {
            return;
        }
        if (prefix.size() <= kMaxMaterializedUserPrefixLength) {
            auto pit = user_prefix_index_.find(prefix);
            if (pit != user_prefix_index_.end()) {
                for (auto id : pit->second.ids) {
                    if (!try_add(id, UserMatchKind::kPrefix, prefix)) {
                        break;
                    }
                }
            }
            return;
        }

        struct ScoredId {
            UserEntryId id;
            int score;
        };
        std::vector<ScoredId> scored;
        auto lower = std::lower_bound(
            user_code_sorted_.begin(), user_code_sorted_.end(), prefix,
            [this](UserEntryId id, const std::string& value) {
                return user_entries_[id].code < value;
            });
        for (auto it = lower; it != user_code_sorted_.end(); ++it) {
            auto& entry = user_entries_[*it];
            if (entry.code.compare(0, prefix.size(), prefix) != 0) {
                break;
            }
            if (stats->scan_count >= budget.max_user_scan) {
                stats->truncated = true;
                stats->scan_budget_truncated = true;
                break;
            }
            if (budget.deadline.enabled && budget.deadline.expired()) {
                stats->deadline_exceeded = true;
                stats->truncated = true;
                break;
            }
            if (entry.deleted) {
                continue;
            }
            ++stats->scan_count;
            scored.push_back({
                *it,
                score_user_match(user_scoring_profile_, UserMatchKind::kPrefix,
                                 prefix.size(), entry.code.size(), entry.frequency,
                                 user_sequence_, entry.sequence),
            });
        }
        std::sort(scored.begin(), scored.end(),
                  [](const ScoredId& lhs, const ScoredId& rhs) {
                      return lhs.score > rhs.score;
                  });
        for (const auto& item : scored) {
            add_candidate(item.id, item.score);
            if ((int)results.size() >= limit) {
                break;
            }
        }
    };

    // 1. Exact match
    auto eit = user_exact_index_.find(key);
    if (eit != user_exact_index_.end()) {
        for (auto id : eit->second.ids)
            if (!try_add(id, UserMatchKind::kExact, key)) break;
    }

    // 2. Prefix match
    try_prefix(key);

    // 3. Abbreviation match
    if ((int)results.size() < limit) {
        auto ait = user_abbr_index_.find(key);
        if (ait != user_abbr_index_.end()) {
            for (auto id : ait->second.ids)
                if (!try_add(id, UserMatchKind::kAbbreviation, key)) break;
        }
    }

    // 4. Mixed match
    if ((int)results.size() < limit) {
        auto mit = user_mixed_index_.find(key);
        if (mit != user_mixed_index_.end()) {
            size_t bucket_size = mit->second.ids.size();
            if (trace) {
                trace->mixed_bucket_size = (uint32_t)bucket_size;
                trace->mixed_cache_hit = true;
            }
            for (auto id : mit->second.ids) {
                if (!try_add(id, UserMatchKind::kMixed, key)) break;
            }
            if (trace)
                trace->mixed_scan_count += stats->scan_count;
        }
    }

    // 5. Rewrite: adjacent duplicate deletion (sddf -> sdf)
    if (results.empty() && key.size() > 2) {
        std::string rewritten = dedup_adjacent(key);
        if (rewritten != key && rewritten.size() >= 2) {
            // Try exact/prefix/abbr/mixed with rewritten key
            auto eit2 = user_exact_index_.find(rewritten);
            if (eit2 != user_exact_index_.end())
                for (auto id : eit2->second.ids)
                    if (!try_add(id, UserMatchKind::kExact, rewritten)) break;
            try_prefix(rewritten);
            if ((int)results.size() < limit) {
                auto ait2 = user_abbr_index_.find(rewritten);
                if (ait2 != user_abbr_index_.end())
                    for (auto id : ait2->second.ids)
                        if (!try_add(id, UserMatchKind::kAbbreviation, rewritten)) break;
            }
            if ((int)results.size() < limit) {
                auto mit2 = user_mixed_index_.find(rewritten);
                if (mit2 != user_mixed_index_.end())
                    for (auto id : mit2->second.ids)
                        if (!try_add(id, UserMatchKind::kMixed, rewritten)) break;
            }
        }
    }

    sort_candidates_by_score(results);
    if (trace) {
        trace->user_scan_count += stats->scan_count;
        if (stats->truncated) trace->truncated = true;
        if (stats->scan_budget_truncated) trace->scan_budget_truncated = true;
        if (stats->deadline_exceeded) trace->deadline_exceeded = true;
    }
    return results;
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
    return load_id_index_file(idx_path);
}

bool Dict::load_id_index_file(const std::string& idx_path) {
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
    if (28ULL + (uint64_t)syl_count * sizeof(uint32_t) > file_size) {
        unload_id_index();
        return false;
    }
    const char* syl_strs = (const char*)(syl_offs + syl_count);
    const char* file_end = base + file_size;
    if (syl_strs > file_end || syl_strs + syl_str_size > file_end) {
        unload_id_index();
        return false;
    }
    syllabary_.resize(syl_count);
    syllable_to_id_.clear();
    for (uint32_t i = 0; i < syl_count; ++i) {
        if (syl_offs[i] >= syl_str_size) {
            unload_id_index();
            return false;
        }
        const char* s = syl_strs + syl_offs[i];
        syllabary_[i] = s;
        syllable_to_id_[s] = i;
    }

    const uint8_t* after_syl = (const uint8_t*)(syl_strs + syl_str_size);

    if (ver == 3) {
        // v3: zero-copy — offsets table + data section
        const uint32_t* id_offsets = (const uint32_t*)after_syl;
        const uint8_t*  id_data    = after_syl + idx_count * 4;
        const uint8_t*  id_end     = id_data + idx_data_size;
        if (after_syl + (uint64_t)idx_count * 4 > (const uint8_t*)file_end ||
            id_end > (const uint8_t*)file_end) {
            unload_id_index();
            return false;
        }

        id_index_.clear();
        id_index_.reserve(idx_count);
        for (uint32_t i = 0; i < idx_count; ++i) {
            if (id_offsets[i] >= idx_data_size) {
                unload_id_index();
                return false;
            }
            const uint8_t* e = id_data + id_offsets[i];
            if (e + 8 > id_end) {
                unload_id_index();
                return false;
            }
            uint32_t cnt = *(const uint32_t*)e;
            if (e + 4 + (uint64_t)cnt * 4 + 4 > id_end) {
                unload_id_index();
                return false;
            }
            uint32_t dict_index = *(const uint32_t*)(e + 4 + cnt * 4);
            if (dict_index >= dict_entry_count_) {
                unload_id_index();
                return false;
            }
            id_index_.push_back({(const uint32_t*)(e + 4), cnt,
                                 dict_index});
        }
    } else {
        // v2: parse variable-length entries (backward compat)
        id_index_.clear();
        id_index_.reserve(idx_count);
        const uint8_t* p = after_syl;
        const uint8_t* end = p + idx_data_size;
        if (end > (const uint8_t*)file_end) {
            unload_id_index();
            return false;
        }
        for (uint32_t i = 0; i < idx_count && p < end; ++i) {
            if (p + 4 > end)
                break;
            uint32_t cnt = *(const uint32_t*)p;
            p += 4;
            if (p + cnt * 4 + 4 > end) break;
            const uint32_t* ids = (const uint32_t*)p; p += cnt * 4;
            uint32_t idx = *(const uint32_t*)p; p += 4;
            if (idx >= dict_entry_count_) {
                unload_id_index();
                return false;
            }
            id_index_.push_back({ids, cnt, idx});
        }
        if (id_index_.size() != idx_count) {
            unload_id_index();
            return false;
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

    // Determine TopK capacity: min(limit, max_results_before_merge)
    // Per Phase 2 design: collector should not collect more than the caller needs.
    size_t topk_cap = (size_t)limit;
    if (budget && budget->max_results_before_merge > 0 &&
        (size_t)budget->max_results_before_merge < topk_cap)
        topk_cap = (size_t)budget->max_results_before_merge;
    TopKCollector collector(topk_cap);

    // Dedup via linear scan on collector items (bounded by topk_cap, typically ≤128)
    bool deadline_hit = false;

    // First pass: exact matches
    auto ids_eq = [&](const IdEntry& e) {
        if (e.count != query_ids.size()) return false;
        for (size_t k = 0; k < query_ids.size(); ++k)
            if (e.ids[k] != query_ids[k]) return false;
        return true;
    };

    uint32_t pos = lo;
    uint32_t exact_count = 0;
    uint32_t check_interval = budget ? budget->deadline.check_interval : 64;

    // Phase 3: check deadline before entering scan loop (upstream may have already exhausted budget)
    if (budget && budget->deadline.enabled && budget->deadline.expired()) {
        deadline_hit = true;
        if (trace) {
            trace->deadline_exceeded = true;
            trace->truncated = true;
        }
    }

    while (!deadline_hit && pos < (uint32_t)id_index_.size() && ids_eq(id_index_[pos])) {
        // Check scan budget
        if (budget && exact_count >= budget->max_exact_scan) {
            if (trace) {
                trace->truncated = true;
                trace->scan_budget_truncated = true;
            }
            break;
        }
        // Phase 3: check deadline every check_interval entries
        if (budget && exact_count > 0 && exact_count % check_interval == 0 && budget->deadline.expired()) {
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
        set_candidate_code(c, dict_strings_ + e.syllable_ids_offset,
                           e.syllable_ids_len);
        c.frequency = e.frequency + 100000;  // exact match boost
        if (!contains_text(collector.items(), c.text)) {
            if (collector.full() && trace) {
                trace->truncated = true;
                trace->topk_truncated = true;
            }
            collector.offer(std::move(c));
        }
        ++pos;
        ++exact_count;
    }

    // Second pass: prefix matches (skip if deadline already hit)
    uint32_t prefix_count = 0;
    if (!deadline_hit) {
        // Phase 3: check deadline before entering prefix scan
        if (budget && budget->deadline.enabled && budget->deadline.expired()) {
            deadline_hit = true;
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
        }
    }
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
                    trace->scan_budget_truncated = true;
                }
                break;
            }
            // Phase 3: check deadline every check_interval entries
            if (budget && prefix_count > 0 && prefix_count % check_interval == 0 && budget->deadline.expired()) {
                if (trace) {
                    trace->deadline_exceeded = true;
                    trace->truncated = true;
                }
                break;
            }
            const auto& e = dict_entries_[id_index_[pos].index];
            Candidate c;
            c.text.assign(dict_strings_ + e.text_offset, e.text_len);
            set_candidate_code(c, dict_strings_ + e.syllable_ids_offset,
                               e.syllable_ids_len);
            c.frequency = e.frequency;
            if (!contains_text(collector.items(), c.text)) {
                if (collector.full() && trace) {
                    trace->truncated = true;
                    trace->topk_truncated = true;
                }
                collector.offer(std::move(c));
            }
            ++pos;
            ++prefix_count;
        }
    }

    if (trace) {
        trace->exact_scan_count += exact_count;
        trace->prefix_scan_count += prefix_count;
    }

    results = collector.finish();
    if ((int)results.size() > limit)
        results.resize(limit);

    return results;
}

} // namespace cxxime
