// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_SUPPORT_TEST_RUNTIME_H_
#define CXXIME_TEST_SUPPORT_TEST_RUNTIME_H_

#include <memory>
#include <string>
#include <utility>

#include <cxxime/composition_learning.h>
#include <cxxime/engine.h>
#include <cxxime/engine_runtime.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/pinyin_scheme.h>
#include <cxxime/symbol_table.h>

namespace test {

inline std::shared_ptr<const cxxime::EngineRuntimeState>
make_runtime(std::shared_ptr<cxxime::Dict> pinyin_dict, const cxxime::Config& config,
             const std::string& spellings_path = {},
             std::shared_ptr<cxxime::Dict> wubi_dict = nullptr,
             std::shared_ptr<const cxxime::SymbolTable> symbol_table = nullptr,
             std::shared_ptr<cxxime::CompositionLearningService> composition_learning = nullptr) {
    const auto& scheme = cxxime::resolve_pinyin_scheme(config.pinyin_scheme);
    const auto requirement = spellings_path.empty()
                                 ? cxxime::PinyinSpellingRequirement::kOptionalForFullPinyin
                                 : cxxime::PinyinSpellingRequirement::kRequired;
    auto pinyin_resources =
        cxxime::PinyinResourceSet::create(scheme.id, scheme.kind, spellings_path, requirement);
    return cxxime::EngineRuntimeState::create(config, std::move(pinyin_dict), std::move(wubi_dict),
                                              std::move(pinyin_resources), std::move(symbol_table),
                                              std::move(composition_learning));
}

inline bool initialize_engine(
    cxxime::Engine& engine, std::shared_ptr<cxxime::Dict> pinyin_dict, const cxxime::Config& config,
    const std::string& spellings_path = {}, std::shared_ptr<cxxime::Dict> wubi_dict = nullptr,
    std::shared_ptr<const cxxime::SymbolTable> symbol_table = nullptr,
    std::shared_ptr<cxxime::CompositionLearningService> composition_learning = nullptr) {
    return engine.initialize(make_runtime(pinyin_dict, config, spellings_path, wubi_dict,
                                          symbol_table, composition_learning));
}

inline bool initialize_engine(cxxime::Engine& engine, const std::string& pinyin_dict_path,
                              const cxxime::Config& config, const std::string& spellings_path = {},
                              const std::string& wubi_dict_path = {}) {
    auto pinyin_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    if (!pinyin_dict->open_dict(pinyin_dict_path)) {
        return false;
    }
    std::shared_ptr<cxxime::Dict> wubi_dict;
    if (!wubi_dict_path.empty()) {
        wubi_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
        if (!wubi_dict->open_dict(wubi_dict_path)) {
            return false;
        }
    }
    return engine.initialize(
        make_runtime(std::move(pinyin_dict), config, spellings_path, std::move(wubi_dict)));
}

inline bool apply_runtime(cxxime::Engine& engine, const std::string& pinyin_dict_path,
                          const cxxime::Config& config,
                          std::shared_ptr<cxxime::Dict> wubi_dict = nullptr,
                          const std::string& spellings_path = {}) {
    auto pinyin_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    if (!pinyin_dict->open_dict(pinyin_dict_path)) {
        return false;
    }
    return engine.apply_runtime_state(
        make_runtime(std::move(pinyin_dict), config, spellings_path, std::move(wubi_dict)));
}

} // namespace test

#endif // CXXIME_TEST_SUPPORT_TEST_RUNTIME_H_
