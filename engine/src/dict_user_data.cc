// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/dict.h>

#include <algorithm>
#include <cstring>
#include <string_view>

#include <windows.h>
#include <shlobj.h>

#include <cxxime/candidate_preference.h>
#include <cxxime/disabled_system_lexicon.h>
#include <cxxime/user_lexicon.h>

#include "binary_format.h"

namespace cxxime {
namespace {

constexpr int kFallbackCandidateScore = 1;

std::string default_user_dict_path() {
    wchar_t profile[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, profile) != S_OK) {
        return {};
    }
    const std::wstring user_dir = std::wstring(profile) + L"\\cxxime";
    CreateDirectoryW(user_dir.c_str(), nullptr);
    const std::wstring path = user_dir + L"\\user_pinyin.tsv";
    char path_utf8[MAX_PATH * 3] = {};
    if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, path_utf8,
                             static_cast<int>(sizeof(path_utf8)), nullptr, nullptr)) {
        return {};
    }
    return path_utf8;
}

} // namespace

bool Dict::load_user_dict(const std::string& path) {
    return user_lexicon_->load(path.empty() ? default_user_dict_path() : path);
}

bool Dict::save_user_dict() { return user_lexicon_->save(); }

bool Dict::add_user_entry(const std::string& text, const std::string& code,
                          const std::string& syllables) {
    return user_lexicon_->add_entry(text, code, syllables);
}

std::vector<UserDictEntryInfo> Dict::query_user_entries(const std::string& query, size_t offset,
                                                        size_t limit, size_t* match_total) const {
    return user_lexicon_->query_entries(query, offset, limit, match_total);
}

bool Dict::delete_user_entry(const std::string& text, const std::string& code) {
    return user_lexicon_->delete_entry(text, code);
}

bool Dict::replace_user_entry(const std::string& old_text, const std::string& old_code,
                              const std::string& new_text, const std::string& new_code) {
    return user_lexicon_->replace_entry(old_text, old_code, new_text, new_code);
}

bool Dict::add_user_entry_and_save(const std::string& text, const std::string& code,
                                   const std::string& syllables) {
    return user_lexicon_->add_entry_and_save(text, code, syllables);
}

bool Dict::delete_user_entry_and_save(const std::string& text, const std::string& code) {
    return user_lexicon_->delete_entry_and_save(text, code);
}

bool Dict::replace_user_entry_and_save(const std::string& old_text, const std::string& old_code,
                                       const std::string& new_text, const std::string& new_code) {
    return user_lexicon_->replace_entry_and_save(old_text, old_code, new_text, new_code);
}

bool Dict::import_user_dict(const std::string& source_path) {
    return user_lexicon_->import_file(source_path);
}

std::string Dict::reverse_lookup(const std::string& text) {
    std::string code = user_lexicon_->reverse_lookup(text);
    if (!code.empty()) {
        return code;
    }
    if (!dict_entries_) {
        return {};
    }
    for (uint32_t i = 0; i < dict_entry_count_; ++i) {
        const auto& entry = dict_entries_[i];
        if (entry.text_len == text.size() &&
            std::memcmp(dict_strings_ + entry.text_offset, text.data(), entry.text_len) == 0) {
            return std::string(dict_strings_ + entry.syllable_ids_offset, entry.syllable_ids_len);
        }
    }
    return {};
}

bool Dict::has_user_entry(const std::string& text) const {
    return user_lexicon_->contains_text(text);
}

size_t Dict::user_entry_count() const { return user_lexicon_->entry_count(); }

uint64_t Dict::user_dict_version() const { return user_lexicon_->version(); }

std::vector<Candidate> Dict::lookup_user_exact(const std::string& code, int limit,
                                               const QueryBudget& budget, QueryTrace* trace,
                                               UserLookupStats* stats) const {
    return user_lexicon_->lookup_exact(code, limit, budget, trace, stats);
}

std::vector<Candidate> Dict::lookup_user_prefix(const std::string& prefix, int limit,
                                                const QueryBudget& budget, QueryTrace* trace,
                                                UserLookupStats* stats) const {
    return user_lexicon_->lookup_prefix(prefix, limit, budget, trace, stats);
}

std::vector<Candidate> Dict::lookup_user_indexed(const std::string& key, int limit,
                                                 const QueryBudget& budget, QueryTrace* trace,
                                                 UserLookupStats* stats) const {
    return user_lexicon_->lookup_indexed(key, limit, budget, trace, stats);
}

bool Dict::load_candidate_preferences(const std::string& path) {
    return candidate_preference_->load(path);
}

bool Dict::save_candidate_preferences() { return candidate_preference_->save(); }

bool Dict::save_candidate_preferences_if_due(std::chrono::milliseconds delay) {
    return candidate_preference_->save_if_due(delay);
}

void Dict::freeze_candidate_preferences() { candidate_preference_->freeze(); }

bool Dict::record_candidate_preference(const Candidate& candidate, const std::string& code) {
    return candidate_preference_->record(candidate, code);
}

void Dict::apply_candidate_preferences(const std::string& code, CandidateSource source,
                                       std::vector<Candidate>& candidates, int limit) const {
    if (limit <= 0) {
        return;
    }
    auto preferences = candidate_preference_->preferred_candidates(code, source);
    const bool filter_disabled = disabled_system_entry_count() != 0;
    for (auto& preference : preferences) {
        if (filter_disabled && is_system_entry_disabled(preference.text) &&
            preference.origin != CandidateOrigin::kUser) {
            continue;
        }
        auto existing =
            std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& candidate) {
                return candidate.text == preference.text;
            });
        if (existing != candidates.end()) {
            if (existing->origin != CandidateOrigin::kLearned) {
                existing->frequency = (std::max)(existing->frequency, preference.frequency);
            }
            continue;
        }
        if (!preference_candidate_available(preference, source)) {
            preference.frequency = kFallbackCandidateScore;
        }
        candidates.push_back(std::move(preference));
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& left, const Candidate& right) {
                         return left.frequency > right.frequency;
                     });
    if (static_cast<int>(candidates.size()) > limit) {
        candidates.resize(static_cast<std::size_t>(limit));
    }
}

bool Dict::preference_candidate_available(const Candidate& candidate,
                                          CandidateSource source) const {
    if (user_lexicon_->contains_candidate(candidate.text, candidate.code, candidate.syllables)) {
        return true;
    }
    if (!dict_entries_) {
        return false;
    }

    if (candidate.syllables.empty() && source == CandidateSource::kPinyin &&
        short_cache_.is_loaded()) {
        const auto cached = short_cache_.lookup(candidate.code, 128);
        if (std::any_of(cached.begin(), cached.end(),
                        [&](const Candidate& item) { return item.text == candidate.text; })) {
            return true;
        }
    }

    const std::string_view target_code = candidate.syllables.empty()
        ? std::string_view(candidate.code)
        : std::string_view(candidate.syllables);
    auto entry_code = [&](std::uint32_t index) {
        const auto& entry = dict_entries_[index];
        return std::string_view(dict_strings_ + entry.syllable_ids_offset, entry.syllable_ids_len);
    };
    std::uint32_t lower = 0;
    std::uint32_t upper = dict_entry_count_;
    while (lower < upper) {
        const std::uint32_t middle = lower + (upper - lower) / 2;
        if (entry_code(middle) < target_code) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }
    for (std::uint32_t index = lower; index < dict_entry_count_ && entry_code(index) == target_code;
         ++index) {
        const auto& entry = dict_entries_[index];
        if (entry.text_len == candidate.text.size() &&
            std::memcmp(dict_strings_ + entry.text_offset, candidate.text.data(), entry.text_len) ==
                0) {
            return true;
        }
    }
    return false;
}

std::vector<UserDictEntryInfo> Dict::query_candidate_preferences(const std::string& query,
                                                                 size_t offset, size_t limit,
                                                                 size_t* match_total) const {
    return candidate_preference_->query(query, offset, limit, match_total);
}

bool Dict::delete_candidate_preference(const std::string& text, const std::string& code) {
    return candidate_preference_->erase(text, code);
}

bool Dict::clear_candidate_preferences() { return candidate_preference_->clear(); }

bool Dict::delete_candidate_preference_and_save(const std::string& text, const std::string& code) {
    return candidate_preference_->erase_and_save(text, code);
}

bool Dict::clear_candidate_preferences_and_save() {
    return candidate_preference_->clear_and_save();
}

size_t Dict::candidate_preference_count() const { return candidate_preference_->entry_count(); }

uint64_t Dict::candidate_preference_version() const { return candidate_preference_->version(); }

bool Dict::load_disabled_system_entries(const std::string& path) {
    return disabled_system_lexicon_->load(path);
}

bool Dict::save_disabled_system_entries() { return disabled_system_lexicon_->save(); }

bool Dict::disable_system_entry(const std::string& text) {
    return disabled_system_lexicon_->disable(text);
}

bool Dict::restore_system_entry(const std::string& text) {
    return disabled_system_lexicon_->restore(text);
}

bool Dict::disable_system_entry_and_save(const std::string& text) {
    return disabled_system_lexicon_->disable_and_save(text);
}

bool Dict::restore_system_entry_and_save(const std::string& text) {
    return disabled_system_lexicon_->restore_and_save(text);
}

bool Dict::is_system_entry_disabled(const std::string& text) const {
    return disabled_system_lexicon_->contains(text);
}

void Dict::filter_disabled_system_candidates(std::vector<Candidate>& candidates) const {
    disabled_system_lexicon_->filter(candidates);
}

std::vector<UserDictEntryInfo> Dict::query_disabled_system_entries(
    const std::string& query, size_t offset, size_t limit, size_t* match_total) const {
    return disabled_system_lexicon_->query(query, offset, limit, match_total);
}

size_t Dict::disabled_system_entry_count() const {
    return disabled_system_lexicon_->entry_count();
}

uint64_t Dict::disabled_system_entry_version() const {
    return disabled_system_lexicon_->version();
}

} // namespace cxxime
