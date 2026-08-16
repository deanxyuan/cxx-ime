// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/user_lexicon.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cxxime {
namespace {

constexpr std::size_t kMaxMaterializedPrefixLength = 6;
constexpr std::size_t kMaxMixedBucketSize = 64;

std::string make_abbreviation(const std::string& syllables) {
    std::string abbreviation;
    for (std::size_t index = 0; index < syllables.size(); ++index) {
        if (index == 0 || syllables[index - 1] == ':') {
            abbreviation.push_back(syllables[index]);
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
    for (std::size_t index = 1; index < parts.size(); ++index) {
        trailing_initials.push_back(parts[index][0]);
    }
    keys.insert(parts[0] + trailing_initials);

    if (parts.size() >= 3) {
        std::string remaining_initials;
        for (std::size_t index = 2; index < parts.size(); ++index) {
            remaining_initials.push_back(parts[index][0]);
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

} // namespace

void UserLexicon::sort_bucket(Snapshot* snapshot, Bucket* bucket) {
    std::sort(bucket->ids.begin(), bucket->ids.end(), [&](EntryId left, EntryId right) {
        const Entry& a = snapshot->entries[left];
        const Entry& b = snapshot->entries[right];
        if (a.frequency != b.frequency) {
            return a.frequency > b.frequency;
        }
        if (a.sequence != b.sequence) {
            return a.sequence > b.sequence;
        }
        return left < right;
    });
}

void UserLexicon::insert_into_indexes(Snapshot* snapshot, EntryId id) {
    Entry& entry = snapshot->entries[id];
    snapshot->exact_index[entry.code].ids.push_back(id);
    const std::size_t max_prefix = (std::min)(entry.code.size(), kMaxMaterializedPrefixLength);
    for (std::size_t length = 1; length <= max_prefix; ++length) {
        snapshot->prefix_index[entry.code.substr(0, length)].ids.push_back(id);
    }
    if (!entry.syllables.empty()) {
        entry.abbr_code = make_abbreviation(entry.syllables);
        if (!entry.abbr_code.empty()) {
            snapshot->abbr_index[entry.abbr_code].ids.push_back(id);
        }
        entry.mixed_keys = generate_mixed_keys(entry.syllables);
        for (const auto& key : entry.mixed_keys) {
            snapshot->mixed_index[key].ids.push_back(id);
        }
    }
    snapshot->code_sorted.push_back(id);
}

UserLexicon::Snapshot UserLexicon::prepare_snapshot(Snapshot snapshot) {
    snapshot.text_index.clear();
    snapshot.entry_index.clear();
    snapshot.exact_index.clear();
    snapshot.prefix_index.clear();
    snapshot.abbr_index.clear();
    snapshot.mixed_index.clear();
    snapshot.code_sorted.clear();

    for (std::size_t index = 0; index < snapshot.entries.size(); ++index) {
        Entry& entry = snapshot.entries[index];
        if (entry.deleted) {
            continue;
        }
        const EntryId id = static_cast<EntryId>(index);
        const std::string key = entry_key(entry.text, entry.code);
        const auto duplicate = snapshot.entry_index.find(key);
        if (duplicate != snapshot.entry_index.end()) {
            Entry& existing = snapshot.entries[duplicate->second];
            existing.frequency = (std::max)(existing.frequency, entry.frequency);
            entry.deleted = true;
            continue;
        }
        snapshot.entry_index[key] = id;
        snapshot.text_index[entry.text].push_back(id);
        insert_into_indexes(&snapshot, id);
    }
    for (auto& item : snapshot.exact_index) {
        sort_bucket(&snapshot, &item.second);
    }
    for (auto& item : snapshot.prefix_index) {
        sort_bucket(&snapshot, &item.second);
    }
    for (auto& item : snapshot.abbr_index) {
        sort_bucket(&snapshot, &item.second);
    }
    for (auto& item : snapshot.mixed_index) {
        sort_bucket(&snapshot, &item.second);
        if (item.second.ids.size() > kMaxMixedBucketSize) {
            item.second.ids.resize(kMaxMixedBucketSize);
        }
    }
    std::sort(snapshot.code_sorted.begin(), snapshot.code_sorted.end(),
              [&](EntryId left, EntryId right) {
                  return snapshot.entries[left].code < snapshot.entries[right].code;
              });
    return snapshot;
}

} // namespace cxxime
