// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/user_lexicon.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <set>
#include <sstream>
#include <utility>

#include "user_data_file.h"

namespace cxxime {
namespace {

constexpr std::size_t kMaxMaterializedPrefixLength = 6;
constexpr std::size_t kMaxMixedBucketSize = 64;

std::string make_abbreviation(const std::string& syllables) {
    std::string abbreviation;
    for (std::size_t i = 0; i < syllables.size(); ++i) {
        if (i == 0 || syllables[i - 1] == ':') {
            abbreviation.push_back(syllables[i]);
        }
    }
    return abbreviation;
}

std::vector<std::string> split_syllables(const std::string& syllables) {
    std::vector<std::string> result;
    std::string current;
    for (char ch : syllables) {
        if (ch == ':') {
            if (!current.empty()) {
                result.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        result.push_back(std::move(current));
    }
    return result;
}

std::vector<std::string> generate_mixed_keys(const std::string& syllables,
                                             std::size_t max_keys = 8) {
    const std::vector<std::string> parts = split_syllables(syllables);
    if (parts.size() < 2) {
        return {};
    }

    std::set<std::string> keys;
    std::string enhanced;
    for (const auto& part : parts) {
        if (part.size() >= 2 && part[1] == 'h' &&
            (part[0] == 'z' || part[0] == 'c' || part[0] == 's')) {
            enhanced.append(part, 0, 2);
        } else {
            enhanced.push_back(part[0]);
        }
    }
    keys.insert(std::move(enhanced));

    if (parts.size() >= 5) {
        std::string initials;
        for (const auto& part : parts) {
            initials.push_back(part[0]);
        }
        keys.insert(std::move(initials));
    }

    std::string trailing_initials;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        trailing_initials.push_back(parts[i][0]);
    }
    keys.insert(parts[0] + trailing_initials);

    if (parts.size() >= 3) {
        std::string remaining_initials;
        for (std::size_t i = 2; i < parts.size(); ++i) {
            remaining_initials.push_back(parts[i][0]);
        }
        keys.insert(parts[0] + parts[1] + remaining_initials);
        keys.insert(std::string(1, parts[0][0]) + parts[1] + remaining_initials);
    }
    if (parts[0].size() >= 2) {
        keys.insert(parts[0].substr(0, 2) + trailing_initials);
    }

    std::string exact;
    for (char ch : syllables) {
        if (ch != ':') {
            exact.push_back(ch);
        }
    }
    keys.erase(exact);
    keys.erase("");

    std::vector<std::string> result(keys.begin(), keys.end());
    if (result.size() > max_keys) {
        result.resize(max_keys);
    }
    return result;
}

std::vector<std::string> split_tsv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t separator = line.find('\t', start);
        fields.push_back(line.substr(start, separator - start));
        if (separator == std::string::npos) {
            return fields;
        }
        start = separator + 1;
    }
}

int parse_frequency(const std::string& value) {
    if (value.empty()) {
        return 1;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed < 1) {
        return 1;
    }
    return parsed > INT_MAX ? INT_MAX : static_cast<int>(parsed);
}

void trim_trailing_space(std::string& value) {
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
}

} // namespace

std::string UserLexicon::entry_key(const std::string& text, const std::string& code) {
    std::string key;
    key.reserve(text.size() + code.size() + 1);
    key.append(code);
    key.push_back('\x1f');
    key.append(text);
    return key;
}

bool UserLexicon::load(const std::string& path) {
    std::lock_guard<std::mutex> save_lock(save_mutex_);
    std::string contents;
    if (!read_user_data_file(path, &contents)) {
        return false;
    }

    std::vector<Entry> entries;
    std::uint64_t sequence = 0;
    std::istringstream input(contents);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::vector<std::string> fields = split_tsv_line(line);
        if ((fields.size() != 3 && fields.size() != 4) || fields[0].empty() ||
            fields[1].empty()) {
            continue;
        }

        Entry entry;
        entry.text = fields[0];
        entry.code = fields[1];
        entry.frequency = parse_frequency(fields[2]);
        entry.syllables = fields.size() >= 4 ? fields[3] : std::string{};
        trim_trailing_space(entry.syllables);
        entry.sequence = ++sequence;
        entries.push_back(std::move(entry));
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    entries_ = std::move(entries);
    path_ = path;
    sequence_ = sequence;
    rebuild_indexes_locked();
    dirty_.store(false, std::memory_order_release);
    version_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

bool UserLexicon::save() {
    std::lock_guard<std::mutex> save_lock(save_mutex_);
    std::string path;
    std::string contents;
    std::uint64_t saved_version = 0;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (!dirty_.load(std::memory_order_acquire) || path_.empty()) {
            return true;
        }
        std::ostringstream output;
        for (const auto& entry : entries_) {
            if (entry.deleted) {
                continue;
            }
            output << entry.text << '\t' << entry.code << '\t' << entry.frequency;
            if (!entry.syllables.empty()) {
                output << '\t' << entry.syllables;
            }
            output << '\n';
        }
        path = path_;
        contents = output.str();
        saved_version = version_.load(std::memory_order_acquire);
    }
    if (!write_user_data_file_atomically(path, contents)) {
        return false;
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (version_.load(std::memory_order_acquire) == saved_version) {
        dirty_.store(false, std::memory_order_release);
    }
    return true;
}

void UserLexicon::set_scoring_profile(UserScoringProfile profile) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    scoring_profile_ = profile;
}

bool UserLexicon::add_entry(const std::string& text, const std::string& code,
                            const std::string& syllables) {
    if (text.empty() || code.empty()) {
        return false;
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const std::string key = entry_key(text, code);
    if (entry_index_.find(key) != entry_index_.end()) {
        return false;
    }

    Entry entry;
    entry.text = text;
    entry.code = code;
    entry.syllables = syllables;
    entry.sequence = ++sequence_;
    const EntryId id = static_cast<EntryId>(entries_.size());
    entries_.push_back(std::move(entry));
    entry_index_[key] = id;
    text_index_[text].push_back(id);
    insert_into_indexes(id);
    std::sort(code_sorted_.begin(), code_sorted_.end(), [this](EntryId left, EntryId right) {
        return entries_[left].code < entries_[right].code;
    });
    dirty_.store(true, std::memory_order_release);
    version_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

bool UserLexicon::delete_entry(const std::string& text, const std::string& code) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    EntryId id = 0;
    if (!code.empty()) {
        const auto found = entry_index_.find(entry_key(text, code));
        if (found == entry_index_.end()) {
            return false;
        }
        id = found->second;
    } else {
        const auto found = text_index_.find(text);
        if (found == text_index_.end() || found->second.empty()) {
            return false;
        }
        id = found->second.front();
    }
    if (id >= entries_.size() || entries_[id].deleted) {
        return false;
    }

    remove_from_indexes(id);
    entry_index_.erase(entry_key(entries_[id].text, entries_[id].code));
    auto text_found = text_index_.find(entries_[id].text);
    if (text_found != text_index_.end()) {
        auto& ids = text_found->second;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        if (ids.empty()) {
            text_index_.erase(text_found);
        }
    }
    dirty_.store(true, std::memory_order_release);
    version_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

bool UserLexicon::replace_entry(const std::string& old_text, const std::string& old_code,
                                const std::string& new_text, const std::string& new_code) {
    if (new_text.empty() || new_code.empty()) {
        return false;
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto found = entry_index_.find(entry_key(old_text, old_code));
    if (found == entry_index_.end() || found->second >= entries_.size()) {
        return false;
    }
    const EntryId id = found->second;
    const std::string next_key = entry_key(new_text, new_code);
    const auto duplicate = entry_index_.find(next_key);
    if (duplicate != entry_index_.end() && duplicate->second != id) {
        return false;
    }

    remove_from_indexes(id);

    Entry& entry = entries_[id];
    entry.text = new_text;
    entry.code = new_code;
    entry.sequence = ++sequence_;
    entry.deleted = false;
    entry.abbr_code.clear();
    entry.mixed_keys.clear();
    entry_index_[next_key] = id;
    text_index_[new_text].push_back(id);
    insert_into_indexes(id);
    std::sort(code_sorted_.begin(), code_sorted_.end(), [this](EntryId left, EntryId right) {
        return entries_[left].code < entries_[right].code;
    });
    dirty_.store(true, std::memory_order_release);
    version_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

std::vector<UserDictEntryInfo> UserLexicon::query_entries(const std::string& query,
                                                          std::size_t offset, std::size_t limit,
                                                          std::size_t* match_total) const {
    std::vector<UserDictEntryInfo> results;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& entry : entries_) {
        if (entry.deleted || (!query.empty() && entry.text.find(query) == std::string::npos &&
             entry.code.find(query) == std::string::npos)) {
            continue;
        }
        results.push_back({entry.text, entry.code, entry.frequency, entry.sequence});
    }
    std::sort(results.begin(), results.end(),
              [](const UserDictEntryInfo& left, const UserDictEntryInfo& right) {
                  if (left.sequence != right.sequence) {
                      return left.sequence > right.sequence;
                  }
                  if (left.frequency != right.frequency) {
                      return left.frequency > right.frequency;
                  }
                  if (left.code != right.code) {
                      return left.code < right.code;
                  }
                  return left.text < right.text;
              });
    if (match_total) {
        *match_total = results.size();
    }
    if (offset >= results.size() || limit == 0) {
        return {};
    }
    const std::size_t end = (std::min)(results.size(), offset + limit);
    return std::vector<UserDictEntryInfo>(results.begin() + offset, results.begin() + end);
}

std::string UserLexicon::reverse_lookup(const std::string& text) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto found = text_index_.find(text);
    if (found == text_index_.end()) {
        return {};
    }
    for (EntryId id : found->second) {
        if (id < entries_.size() && !entries_[id].deleted) {
            return entries_[id].code;
        }
    }
    return {};
}

bool UserLexicon::contains_text(const std::string& text) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto found = text_index_.find(text);
    return found != text_index_.end() && !found->second.empty();
}

bool UserLexicon::contains_candidate(const std::string& text, const std::string& code,
                                     const std::string& syllables) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto found = text_index_.find(text);
    if (found == text_index_.end()) {
        return false;
    }
    for (EntryId id : found->second) {
        const Entry& entry = entries_[id];
        if (!entry.deleted &&
            (entry.code == code || (!syllables.empty() && entry.syllables == syllables))) {
            return true;
        }
    }
    return false;
}

std::size_t UserLexicon::entry_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return entry_index_.size();
}

std::uint64_t UserLexicon::version() const { return version_.load(std::memory_order_acquire); }

void UserLexicon::bucket_insert_sorted(Bucket& bucket, EntryId id) {
    auto better = [this](EntryId left, EntryId right) {
        const Entry& a = entries_[left];
        const Entry& b = entries_[right];
        if (a.frequency != b.frequency) {
            return a.frequency > b.frequency;
        }
        if (a.sequence != b.sequence) {
            return a.sequence > b.sequence;
        }
        return left < right;
    };
    bucket.ids.insert(std::lower_bound(bucket.ids.begin(), bucket.ids.end(), id, better), id);
}

void UserLexicon::sort_bucket(Bucket& bucket) {
    std::sort(bucket.ids.begin(), bucket.ids.end(), [this](EntryId left, EntryId right) {
        const Entry& a = entries_[left];
        const Entry& b = entries_[right];
        if (a.frequency != b.frequency) {
            return a.frequency > b.frequency;
        }
        if (a.sequence != b.sequence) {
            return a.sequence > b.sequence;
        }
        return left < right;
    });
}

void UserLexicon::rebuild_indexes_locked() {
    text_index_.clear();
    entry_index_.clear();
    exact_index_.clear();
    prefix_index_.clear();
    abbr_index_.clear();
    mixed_index_.clear();
    code_sorted_.clear();

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& entry = entries_[i];
        if (entry.deleted) {
            continue;
        }
        const EntryId id = static_cast<EntryId>(i);
        const std::string key = entry_key(entry.text, entry.code);
        const auto duplicate = entry_index_.find(key);
        if (duplicate != entry_index_.end()) {
            entries_[duplicate->second].frequency =
                (std::max)(entries_[duplicate->second].frequency, entry.frequency);
            entry.deleted = true;
            continue;
        }
        entry_index_[key] = id;
        text_index_[entry.text].push_back(id);
        insert_into_indexes(id);
    }
    for (auto& item : exact_index_) {
        sort_bucket(item.second);
    }
    for (auto& item : prefix_index_) {
        sort_bucket(item.second);
    }
    for (auto& item : abbr_index_) {
        sort_bucket(item.second);
    }
    for (auto& item : mixed_index_) {
        sort_bucket(item.second);
    }
    std::sort(code_sorted_.begin(), code_sorted_.end(), [this](EntryId left, EntryId right) {
        return entries_[left].code < entries_[right].code;
    });
}

void UserLexicon::insert_into_indexes(EntryId id) {
    Entry& entry = entries_[id];
    bucket_insert_sorted(exact_index_[entry.code], id);
    const std::size_t max_prefix = (std::min)(entry.code.size(), kMaxMaterializedPrefixLength);
    for (std::size_t length = 1; length <= max_prefix; ++length) {
        bucket_insert_sorted(prefix_index_[entry.code.substr(0, length)], id);
    }
    if (!entry.syllables.empty()) {
        entry.abbr_code = make_abbreviation(entry.syllables);
        if (!entry.abbr_code.empty()) {
            bucket_insert_sorted(abbr_index_[entry.abbr_code], id);
        }
        entry.mixed_keys = generate_mixed_keys(entry.syllables);
        for (const auto& key : entry.mixed_keys) {
            Bucket& bucket = mixed_index_[key];
            bucket_insert_sorted(bucket, id);
            if (bucket.ids.size() > kMaxMixedBucketSize) {
                bucket.ids.resize(kMaxMixedBucketSize);
            }
        }
    }
    code_sorted_.push_back(id);
}

void UserLexicon::remove_from_indexes(EntryId id) {
    entries_[id].deleted = true;
    rebuild_indexes_locked();
}

} // namespace cxxime
