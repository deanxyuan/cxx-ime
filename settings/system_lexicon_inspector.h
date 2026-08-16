// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SYSTEM_LEXICON_INSPECTOR_H_
#define CXXIME_SYSTEM_LEXICON_INSPECTOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cxxime {

enum class SystemLexiconType {
    kPinyin,
    kWubi,
};

enum class SystemLexiconTextMatch {
    kExact,
    kPrefix,
};

struct SystemLexiconEntry {
    std::string text;
    std::string code;
    int32_t frequency = 0;
    uint32_t entry_id = 0;
};

// Read-only dictionary view used only by Settings and its focused tests.
class SystemLexiconInspector {
public:
    SystemLexiconInspector();
    ~SystemLexiconInspector();

    SystemLexiconInspector(const SystemLexiconInspector&) = delete;
    SystemLexiconInspector& operator=(const SystemLexiconInspector&) = delete;

    bool open(SystemLexiconType type, const std::string& dictionary_path,
              const std::string& reverse_index_path);
    void close();
    bool is_open() const;

    std::vector<SystemLexiconEntry> query_text(std::string_view text, SystemLexiconTextMatch match,
                                               std::size_t limit) const;
    std::vector<SystemLexiconEntry> query_code_prefix(std::string_view code,
                                                      std::size_t limit) const;

    const std::string& last_error() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cxxime

#endif // CXXIME_SYSTEM_LEXICON_INSPECTOR_H_
