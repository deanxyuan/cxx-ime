// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ENGINE_H_
#define CXXIME_ENGINE_H_

#include <string>
#include <cxxime/processor.h>
#include <cxxime/translator.h>
#include <cxxime/segmentor.h>
#include <cxxime/dict.h>
#include <cxxime/context.h>
#include <cxxime/config.h>

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

private:
    PinyinProcessor processor_;
    PinyinTranslator translator_;
    PinyinSegmentor segmentor_;
    Dict dict_;
    Context context_;
    Config config_;
};

} // namespace cxxime

#endif // CXXIME_ENGINE_H_
