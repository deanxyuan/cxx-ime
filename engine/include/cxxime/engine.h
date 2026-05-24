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
    // Self-contained init: Engine owns all resources (tests/tools use this).
    bool initialize(const std::string& dict_path, const std::string& config_path = "");

    // Shared-resource init: Engine references pre-loaded resources (server sessions).
    bool initialize(Dict& dict, SpellingsIndex& spellings,
                    Syllabifier* syllabifier, const Config& config);

    void finalize();

    ProcessResult process_key(const KeyEvent& event);
    const Context& context() const;
    Context& context();
    bool select_candidate(int index);
    std::string get_commit_text();
    void clear();

    const AsciiComposer& ascii_composer() const { return ascii_composer_; }
    AsciiComposer& ascii_composer() { return ascii_composer_; }

    static std::string derive_spellings_path(const std::string& dict_path);

private:
    void init_per_session(const Config& config);

    PinyinProcessor processor_;
    PinyinTranslator translator_;
    Context context_;
    AsciiComposer ascii_composer_;

    // Self-contained resources (owned when initialized from file paths).
    Dict owned_dict_;
    SpellingsIndex owned_spellings_;
    Config owned_config_;
    std::unique_ptr<Syllabifier> owned_syllabifier_;

    // Active resource references (point to owned_* or shared_* depending on init path).
    Dict* dict_ = nullptr;
    SpellingsIndex* spellings_ = nullptr;
    Syllabifier* syllabifier_ = nullptr;
    const Config* config_ = nullptr;
};

} // namespace cxxime

#endif // CXXIME_ENGINE_H_
