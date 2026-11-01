// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/custom_phrases.h>

#include <algorithm>
#include <sstream>
#include <utility>

#include <cxxime/user_dict_validation.h>

#include "user_data_file.h"

namespace cxxime {
namespace {

constexpr std::size_t kMaxEntries = 4096;
constexpr std::size_t kMaxFileBytes = 4 * 1024 * 1024;

} // namespace

bool CustomPhrases::parse(const std::string& contents, Entries* entries) {
    if (!entries || contents.size() > kMaxFileBytes) {
        return false;
    }
    Entries parsed;
    const bool bom = contents.compare(0, 3, "\xef\xbb\xbf") == 0;
    std::istringstream input(bom ? contents.substr(3) : contents);
    std::string line;
    std::size_t count = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('\t');
        if (separator == std::string::npos || ++count > kMaxEntries) {
            return false;
        }
        const std::string code = line.substr(0, separator);
        const std::string text = line.substr(separator + 1);
        if (!is_valid_user_dict_entry(text, code)) {
            return false;
        }
        auto& phrases = parsed[code];
        if (std::find(phrases.begin(), phrases.end(), text) != phrases.end()) {
            return false;
        }
        phrases.push_back(text);
    }
    *entries = std::move(parsed);
    return true;
}

bool CustomPhrases::validate_contents(const std::string& contents) {
    Entries entries;
    return parse(contents, &entries);
}

std::string CustomPhrases::serialize(const Entries& entries) {
    std::string contents = "# CxxIME custom phrases: code<TAB>text; same-code order is rank\n";
    for (const auto& group : entries) {
        for (const auto& text : group.second) {
            contents += group.first + '\t' + text + '\n';
        }
    }
    return contents;
}

bool CustomPhrases::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string contents;
    Entries entries;
    if (!read_user_data_file(path, kMaxFileBytes, &contents) || !parse(contents, &entries)) {
        return false;
    }
    path_ = path;
    std::atomic_store(&entries_, std::make_shared<const Entries>(std::move(entries)));
    return true;
}

bool CustomPhrases::publish(Entries entries) {
    // Allocate before saving. Readers retain an immutable snapshot and never wait for disk I/O.
    auto next = std::make_shared<const Entries>(std::move(entries));
    const std::string contents = serialize(*next);
    if (path_.empty() || !validate_contents(contents) ||
        !write_user_data_file_atomically(path_, contents)) {
        return false;
    }
    std::atomic_store(&entries_, std::move(next));
    return true;
}

std::vector<std::string> CustomPhrases::lookup(const std::string& code) const {
    const auto entries = std::atomic_load(&entries_);
    const auto found = entries->find(code);
    return found == entries->end() ? std::vector<std::string>{} : found->second;
}

bool CustomPhrases::has_prefix(const std::string& code) const {
    if (code.empty()) {
        return false;
    }
    const auto entries = std::atomic_load(&entries_);
    const auto found = entries->lower_bound(code);
    return found != entries->end() && found->first.compare(0, code.size(), code) == 0;
}

UserDictQueryResult CustomPhrases::query(const std::string& query, std::size_t offset,
                                         std::size_t limit) const {
    const auto entries = std::atomic_load(&entries_);
    UserDictQueryResult result;
    result.offset = offset;
    for (const auto& group : *entries) {
        std::uint64_t rank = 0;
        for (const auto& text : group.second) {
            ++rank;
            ++result.resource_total;
            if (group.first.find(query) == std::string::npos &&
                text.find(query) == std::string::npos) {
                continue;
            }
            if (result.match_total++ >= offset && result.entries.size() < limit) {
                result.entries.push_back({text, group.first, 1, rank, {}, group.second.size()});
            }
        }
    }
    result.has_more =
        offset < result.match_total && result.match_total - offset > result.entries.size();
    return result;
}

bool CustomPhrases::add(const std::string& code, const std::string& text) {
    if (!is_valid_user_dict_entry(text, code)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Entries next = *std::atomic_load(&entries_);
    auto& group = next[code];
    if (std::find(group.begin(), group.end(), text) != group.end()) {
        return false;
    }
    group.push_back(text);
    return publish(std::move(next));
}

bool CustomPhrases::replace(const LexiconEntryKey& old_entry, const LexiconEntryKey& entry) {
    if (!is_valid_user_dict_entry(entry.text, entry.code)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Entries next = *std::atomic_load(&entries_);
    auto old = next.find(old_entry.code);
    if (old == next.end()) {
        return false;
    }
    auto item = std::find(old->second.begin(), old->second.end(), old_entry.text);
    if (item == old->second.end()) {
        return false;
    }
    if (entry.code == old_entry.code && entry.text == old_entry.text) {
        return true;
    }
    auto& target = next[entry.code];
    if (std::find(target.begin(), target.end(), entry.text) != target.end()) {
        return false;
    }
    if (entry.code == old_entry.code) {
        *item = entry.text;
    } else {
        target.push_back(entry.text);
        old->second.erase(item);
        if (old->second.empty()) {
            next.erase(old);
        }
    }
    return publish(std::move(next));
}

bool CustomPhrases::erase(const std::vector<LexiconEntryKey>& entries) {
    if (entries.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Entries next = *std::atomic_load(&entries_);
    for (const auto& entry : entries) {
        auto group = next.find(entry.code);
        if (group == next.end()) {
            return false;
        }
        auto item = std::find(group->second.begin(), group->second.end(), entry.text);
        if (item == group->second.end()) {
            return false;
        }
        group->second.erase(item);
        if (group->second.empty()) {
            next.erase(group);
        }
    }
    return publish(std::move(next));
}

bool CustomPhrases::move(const LexiconEntryKey& entry, bool up) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entries next = *std::atomic_load(&entries_);
    auto group = next.find(entry.code);
    if (group == next.end()) {
        return false;
    }
    auto item = std::find(group->second.begin(), group->second.end(), entry.text);
    if (item == group->second.end() || (up && item == group->second.begin()) ||
        (!up && item + 1 == group->second.end())) {
        return false;
    }
    std::iter_swap(item, up ? item - 1 : item + 1);
    return publish(std::move(next));
}

bool CustomPhrases::merge_contents(const std::string& contents, UserDataMergeResult* result) {
    Entries imported;
    if (!parse(contents, &imported)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Entries next = *std::atomic_load(&entries_);
    UserDataMergeResult merged;
    for (const auto& group : imported) {
        auto& target = next[group.first];
        for (const auto& text : group.second) {
            if (std::find(target.begin(), target.end(), text) != target.end()) {
                ++merged.skipped_count;
            } else {
                target.push_back(text);
                ++merged.imported_count;
            }
        }
    }
    if (!publish(std::move(next))) {
        return false;
    }
    if (result) {
        *result = merged;
    }
    return true;
}

bool CustomPhrases::import_file(const std::string& path) {
    std::string contents;
    return read_existing_user_data_file(path, kMaxFileBytes, &contents) &&
           merge_contents(contents, nullptr);
}

bool CustomPhrases::export_file(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return write_user_data_file_atomically(path, serialize(*std::atomic_load(&entries_)));
}

} // namespace cxxime
