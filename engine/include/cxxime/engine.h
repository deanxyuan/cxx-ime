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
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>

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

    // Query trace access
    const QueryTrace& last_trace() const { return trace_; }
    void set_trace_enabled(bool enabled) { trace_enabled_ = enabled; }
    void set_trace_session_id(uint32_t id) { trace_.session_id = id; }

    // Override config page_size (only for self-contained init)
    void set_config_page_size(int size) {
        if (config_ == &owned_config_)
            owned_config_.page_size = size;
    }

    // Query budget (deadline + scan limits)
    void set_query_budget(const QueryBudget& budget) { budget_ = budget; }
    const QueryBudget& query_budget() const { return budget_; }

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

    // Query trace (explicit ownership, not thread_local - see TraceContext constraints)
    QueryTrace trace_;
    bool trace_enabled_ = true;
    static uint64_t next_query_id_;

    // Query budget (deadline + scan limits)
    QueryBudget budget_;
};

} // namespace cxxime

#endif // CXXIME_ENGINE_H_
