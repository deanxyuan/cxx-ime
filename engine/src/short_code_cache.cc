// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/short_code_cache.h>
#include <cxxime/query_trace.h>
#include <cxxime/logging.h>
#include "short_code_cache_format.h"
#include <cstring>
#include <algorithm>
#include <windows.h>

static const char TOPN_MAGIC[] = "CXTOPN\x01\x00";

namespace cxxime {

ShortCodeCache::~ShortCodeCache() {
    unload();
}

bool ShortCodeCache::load(const std::string& path) {
    unload();
    CXXIME_LOG(L"ShortCodeCache::load path=%S", path.c_str());

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        CXXIME_LOG(L"ShortCodeCache::load CreateFileA FAILED");
        return false;
    }

    LARGE_INTEGER li;
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < (LONGLONG)sizeof(ShortCacheHeader)) {
        CloseHandle(hFile);
        CXXIME_LOG(L"ShortCodeCache::load file too small");
        return false;
    }
    data_size_ = (size_t)li.QuadPart;

    data_ = new (std::nothrow) char[data_size_];
    if (!data_) {
        CloseHandle(hFile);
        CXXIME_LOG(L"ShortCodeCache::load allocation failed (%zu bytes)", data_size_);
        return false;
    }

    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, data_, (DWORD)data_size_, &bytes_read, nullptr);
    CloseHandle(hFile);
    if (!ok || bytes_read != data_size_) {
        CXXIME_LOG(L"ShortCodeCache::load ReadFile FAILED");
        unload();
        return false;
    }

    auto* hdr = (const ShortCacheHeader*)data_;
    if (std::memcmp(hdr->magic, TOPN_MAGIC, 8) != 0) {
        CXXIME_LOG(L"ShortCodeCache::load bad magic");
        unload();
        return false;
    }
    if (hdr->version != 1) {
        CXXIME_LOG(L"ShortCodeCache::load bad version=%u", hdr->version);
        unload();
        return false;
    }

    // Bounds validation
    if (hdr->keys_offset > data_size_ ||
        hdr->candidates_offset > data_size_ ||
        hdr->strings_offset > data_size_ ||
        hdr->keys_offset + (uint64_t)hdr->key_count * sizeof(ShortKeyEntry) > data_size_ ||
        hdr->candidates_offset + (uint64_t)hdr->candidate_count * sizeof(ShortCandidateEntry) > data_size_ ||
        hdr->strings_offset + (uint64_t)hdr->string_data_size > data_size_) {
        CXXIME_LOG(L"ShortCodeCache::load bounds check FAILED");
        unload();
        return false;
    }

    key_count_ = hdr->key_count;
    candidate_count_ = hdr->candidate_count;
    keys_ = (const ShortKeyEntry*)(data_ + hdr->keys_offset);
    candidates_ = (const ShortCandidateEntry*)(data_ + hdr->candidates_offset);
    strings_ = data_ + hdr->strings_offset;

    CXXIME_LOG(L"ShortCodeCache::load OK keys=%u candidates=%u", key_count_, candidate_count_);
    return true;
}

void ShortCodeCache::unload() {
    delete[] data_;
    data_ = nullptr;
    data_size_ = 0;
    keys_ = nullptr;
    candidates_ = nullptr;
    strings_ = nullptr;
    key_count_ = 0;
    candidate_count_ = 0;
}

std::vector<Candidate> ShortCodeCache::lookup(const std::string& key, int limit,
                                                QueryTrace* trace) const {
    std::vector<Candidate> results;
    if (!keys_ || key.empty())
        return results;

    const uint32_t key_len = (uint32_t)key.size();
    const char* key_data = key.data();

    // Binary search on keys_ (sorted by key bytes)
    uint32_t lo = 0, hi = key_count_;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const auto& e = keys_[mid];
        const char* k = strings_ + e.key_offset;
        uint32_t cmp_len = e.key_len < key_len ? e.key_len : key_len;
        int cmp = std::memcmp(k, key_data, cmp_len);
        if (cmp < 0 || (cmp == 0 && e.key_len < key_len)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    // Collect all entries matching the key exactly
    while (lo < key_count_) {
        const auto& e = keys_[lo];
        if (e.key_len != key_len)
            break;
        if (std::memcmp(strings_ + e.key_offset, key_data, key_len) != 0)
            break;

        // Found a matching key — collect its candidates
        for (uint32_t i = 0; i < e.candidate_count && (int)results.size() < limit; ++i) {
            uint32_t ci = e.candidate_offset + i;
            if (ci >= candidate_count_)
                break;
            const auto& ce = candidates_[ci];
            Candidate c;
            c.text.assign(strings_ + ce.text_offset, ce.text_len);
            if (ce.comment_len > 0)
                c.comment.assign(strings_ + ce.comment_offset, ce.comment_len);
            c.frequency = ce.frequency;
            results.push_back(std::move(c));
        }

        // Only need the first matching key entry (keys are unique after dedup in build)
        break;
    }

    if (!results.empty() && trace)
        trace->cache_hit = true;

    return results;
}

bool ShortCodeCache::create_test_cache(
    const std::string& path,
    const std::vector<std::pair<std::string, std::vector<Candidate>>>& entries) {

    // Build string data and collect all candidates
    std::string strings;
    auto intern = [&strings](const std::string& s) -> std::pair<uint32_t, uint32_t> {
        uint32_t off = (uint32_t)strings.size();
        strings += s;
        return {off, (uint32_t)s.size()};
    };

    struct KeyData {
        uint32_t key_offset, key_len;
        uint16_t flags;
        uint32_t cand_offset, cand_count;
    };
    std::vector<KeyData> key_data;
    struct CandData {
        uint32_t text_off, text_len, comment_off, comment_len;
        int32_t frequency, score;
    };
    std::vector<CandData> cand_data;

    // Sort entries by key byte order (required for binary search)
    auto sorted_entries = entries;
    std::sort(sorted_entries.begin(), sorted_entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (auto& [key, cands] : sorted_entries) {
        KeyData kd;
        auto [ko, kl] = intern(key);
        kd.key_offset = ko;
        kd.key_len = (uint16_t)kl;
        kd.flags = SHORT_KEY_EXACT;
        kd.cand_offset = (uint32_t)cand_data.size();
        kd.cand_count = (uint32_t)cands.size();

        for (auto& c : cands) {
            CandData cd;
            auto [to, tl] = intern(c.text);
            cd.text_off = to;
            cd.text_len = tl;
            if (c.comment.empty()) {
                cd.comment_off = 0;
                cd.comment_len = 0;
            } else {
                auto [co, cl] = intern(c.comment);
                cd.comment_off = co;
                cd.comment_len = cl;
            }
            cd.frequency = c.frequency;
            cd.score = c.frequency;
            cand_data.push_back(cd);
        }

        key_data.push_back(kd);
    }

    uint32_t kc = (uint32_t)key_data.size();
    uint32_t cc = (uint32_t)cand_data.size();
    uint32_t keys_off = sizeof(ShortCacheHeader);
    uint32_t cands_off = keys_off + kc * sizeof(ShortKeyEntry);
    uint32_t strs_off = cands_off + cc * sizeof(ShortCandidateEntry);

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD written;
    ShortCacheHeader hdr = {};
    std::memcpy(hdr.magic, TOPN_MAGIC, 8);
    hdr.version = 1;
    hdr.key_count = kc;
    hdr.candidate_count = cc;
    hdr.string_data_size = (uint32_t)strings.size();
    hdr.keys_offset = keys_off;
    hdr.candidates_offset = cands_off;
    hdr.strings_offset = strs_off;
    WriteFile(hFile, &hdr, sizeof(hdr), &written, nullptr);

    for (auto& kd : key_data) {
        ShortKeyEntry ske = {};
        ske.candidate_offset = kd.cand_offset;
        ske.candidate_count = kd.cand_count;
        ske.key_offset = kd.key_offset;
        ske.key_len = (uint16_t)kd.key_len;
        ske.flags = kd.flags;
        WriteFile(hFile, &ske, sizeof(ske), &written, nullptr);
    }

    for (auto& cd : cand_data) {
        ShortCandidateEntry sce = {};
        sce.text_offset = cd.text_off;
        sce.text_len = cd.text_len;
        sce.comment_offset = cd.comment_off;
        sce.comment_len = cd.comment_len;
        sce.frequency = cd.frequency;
        sce.score = cd.score;
        WriteFile(hFile, &sce, sizeof(sce), &written, nullptr);
    }

    WriteFile(hFile, strings.data(), (DWORD)strings.size(), &written, nullptr);
    CloseHandle(hFile);
    return true;
}

} // namespace cxxime
