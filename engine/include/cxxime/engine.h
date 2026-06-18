// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ENGINE_H_
#define CXXIME_ENGINE_H_

#include <string>
#include <utility>
#include <memory>
#include <atomic>
#include <cxxime/processor.h>
#include <cxxime/translator.h>
#include <cxxime/query_scratch.h>
#include <cxxime/dict.h>
#include <cxxime/context.h>
#include <cxxime/config.h>
#include <cxxime/ascii_composer.h>
#include <cxxime/output_options.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>
#include <cxxime/ipc_protocol.h>

namespace cxxime {

class Engine {
public:
    // Self-contained init: Engine owns all resources (tests/tools use this).
    bool initialize(const std::string& dict_path, const std::string& config_path = "");

    // Shared-resource init: Engine references pre-loaded resources (server sessions).
    bool initialize(Dict& dict, SpellingsIndex& spellings,
                    Syllabifier* syllabifier, const Config& config);

    void finalize();

    // Hot-reload config: update config_ pointer and rebuild AsciiComposer.
    // The new Config object must outlive this Engine (managed by SessionEntry::config_snapshot).
    void reload_config(const Config& config);

    ProcessResult process_key(const KeyEvent& event);
    ProcessResult process_key(const KeyEvent& event, const OutputOptions& opts);
    const Context& context() const;
    Context& context();
    bool select_candidate(int index);
    std::string get_commit_text();
    std::pair<std::string, CommitSource> take_commit_text_with_source();
    std::pair<std::string, CommitSource> commit_composition_with_source();
    void clear();
    void clear_composition();  // clear composing state only, preserve session recent cache

    const AsciiComposer& ascii_composer() const { return ascii_composer_; }
    AsciiComposer& ascii_composer() { return ascii_composer_; }

    // Query trace access
    const QueryTrace& last_trace() const { return trace_; }
    bool has_short_cache() const { return pinyin_dict_ && pinyin_dict_->has_short_cache(); }
    void set_trace_enabled(bool enabled) { trace_enabled_ = enabled; }
    void set_trace_session_id(uint32_t id) { trace_.session_id = id; }

    // Override config page_size (only for self-contained init)
    void set_config_page_size(int size) {
        if (config_ == &owned_config_)
            owned_config_.page_size = size;
    }

    // Query budget (scan limits) — deadline is set separately via set_query_deadline_ms()
    void set_query_budget(const QueryBudget& budget) { budget_ = budget; }
    const QueryBudget& query_budget() const { return budget_; }

    // Deadline protection (Phase 3)
    void set_query_deadline_ms(uint32_t deadline_ms) { query_deadline_ms_ = deadline_ms; }
    uint32_t query_deadline_ms() const { return query_deadline_ms_; }

    // Wubi dict optional load
    void set_wubi_dict(Dict* dict);

    // Fuzzy pinyin toggle
    void set_fuzzy_enabled(bool enabled);

    // Mode switching
    void switch_mode(InputMode mode);
    InputMode mode() const { return mode_; }

    static std::string derive_spellings_path(const std::string& dict_path);

private:
    void init_per_session(const Config& config);

    std::unique_ptr<IProcessor> processor_;
    std::unique_ptr<ITranslator> translator_;

    Context context_;
    AsciiComposer ascii_composer_;

    // Self-contained resources (owned when initialized from file paths).
    Dict owned_dict_;
    SpellingsIndex owned_spellings_;
    Config owned_config_;
    std::unique_ptr<Syllabifier> owned_syllabifier_;

    // Active resource references (point to owned_* or shared_* depending on init path).
    Dict* pinyin_dict_ = nullptr;
    Dict* wubi_dict_ = nullptr;
    SpellingsIndex* spellings_ = nullptr;
    Syllabifier* syllabifier_ = nullptr;
    const Config* config_ = nullptr;

    // Input mode
    InputMode mode_ = InputMode::PINYIN;

    // Query trace (explicit ownership, not thread_local - see TraceContext constraints)
    QueryTrace trace_;
    bool trace_enabled_ = true;
    static std::atomic<uint64_t> next_query_id_;

    // Query budget (scan limits only — deadline is per-query via QueryDeadline)
    QueryBudget budget_;
    uint32_t query_deadline_ms_ = 30;  // default 30ms deadline

    // Per-engine reusable scratch buffer for translate() queries
    QueryScratch scratch_;

    // Punctuation handling (Phase 2.5 / 4.5)
    bool handle_punctuation(const KeyEvent& event, Context& context, const OutputOptions& opts);
    bool handle_full_shape(const KeyEvent& event, Context& context, const OutputOptions& opts);
    void commit_with_punctuation(Context& context, const std::string& output,
                                 const std::vector<std::string>* candidates, int highlighted);
};

} // namespace cxxime

#endif // CXXIME_ENGINE_H_
