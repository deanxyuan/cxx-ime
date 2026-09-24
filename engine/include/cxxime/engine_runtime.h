// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ENGINE_RUNTIME_H_
#define CXXIME_ENGINE_RUNTIME_H_

#include <memory>

#include <cxxime/config.h>
#include <cxxime/dict.h>
#include <cxxime/pinyin_resource.h>

namespace cxxime {

class CompositionLearningService;
class SymbolTable;

class EngineRuntimeState final {
public:
    static std::shared_ptr<const EngineRuntimeState>
    create(Config config, std::shared_ptr<Dict> pinyin_dict, std::shared_ptr<Dict> wubi_dict,
           std::shared_ptr<const PinyinResourceSet> pinyin_resources,
           std::shared_ptr<const SymbolTable> symbol_table = nullptr,
           std::shared_ptr<CompositionLearningService> composition_learning = nullptr);

    EngineRuntimeState(const EngineRuntimeState&) = delete;
    EngineRuntimeState& operator=(const EngineRuntimeState&) = delete;
    EngineRuntimeState(EngineRuntimeState&&) = delete;
    EngineRuntimeState& operator=(EngineRuntimeState&&) = delete;

    const Config& config() const { return config_; }
    Dict& pinyin_dict() const { return *pinyin_dict_; }
    Dict* wubi_dict() const { return wubi_dict_.get(); }
    const PinyinResourceSet& pinyin_resources() const { return *pinyin_resources_; }
    const SymbolTable* symbol_table() const { return symbol_table_.get(); }
    CompositionLearningService* composition_learning() const { return composition_learning_.get(); }
    PinyinQueryPolicy pinyin_query_policy() const { return pinyin_query_policy_; }
    const std::shared_ptr<Dict>& pinyin_dict_ptr() const { return pinyin_dict_; }
    const std::shared_ptr<Dict>& wubi_dict_ptr() const { return wubi_dict_; }
    const std::shared_ptr<const PinyinResourceSet>& pinyin_resources_ptr() const {
        return pinyin_resources_;
    }
    const std::shared_ptr<const SymbolTable>& symbol_table_ptr() const { return symbol_table_; }
    const std::shared_ptr<CompositionLearningService>& composition_learning_ptr() const {
        return composition_learning_;
    }

private:
    EngineRuntimeState(Config config, std::shared_ptr<Dict> pinyin_dict,
                       std::shared_ptr<Dict> wubi_dict,
                       std::shared_ptr<const PinyinResourceSet> pinyin_resources,
                       std::shared_ptr<const SymbolTable> symbol_table,
                       std::shared_ptr<CompositionLearningService> composition_learning);

    const Config config_;
    const std::shared_ptr<Dict> pinyin_dict_;
    const std::shared_ptr<Dict> wubi_dict_;
    const std::shared_ptr<const PinyinResourceSet> pinyin_resources_;
    const std::shared_ptr<const SymbolTable> symbol_table_;
    const std::shared_ptr<CompositionLearningService> composition_learning_;
    const PinyinQueryPolicy pinyin_query_policy_;
};

} // namespace cxxime

#endif // CXXIME_ENGINE_RUNTIME_H_
