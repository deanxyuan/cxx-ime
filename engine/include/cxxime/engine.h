// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ENGINE_H_
#define CXXIME_ENGINE_H_

#include <string>
#include <memory>
#include <cxxime/processor.h>
#include <cxxime/translator.h>
#include <cxxime/dict.h>
#include <cxxime/context.h>
#include <cxxime/config.h>
#include <cxxime/ascii_composer.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>

namespace cxxime {

class Engine {
public:
    bool initialize(const std::string& dict_path, const std::string& config_path = "");
    void finalize();

    ProcessResult process_key(const KeyEvent& event);
    const Context& context() const;
    Context& context();
    bool select_candidate(int index);
    std::string get_commit_text();
    void clear();

    const AsciiComposer& ascii_composer() const { return ascii_composer_; }
    AsciiComposer& ascii_composer() { return ascii_composer_; }

private:
    std::string derive_spellings_path(const std::string& dict_path);

    PinyinProcessor processor_;
    PinyinTranslator translator_;
    Dict dict_;
    Context context_;
    Config config_;
    AsciiComposer ascii_composer_;
    SpellingsIndex spellings_;
    std::unique_ptr<Syllabifier> syllabifier_;
};

} // namespace cxxime

#endif // CXXIME_ENGINE_H_
