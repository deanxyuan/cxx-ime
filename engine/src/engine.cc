// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/engine.h>
#include <cxxime/output_composer.h>
#include <cxxime/wubi_processor.h>
#include <cxxime/wubi_translator.h>
#include <cxxime/mixed_translator.h>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cxxime/logging.h>

namespace cxxime {

static inline void record_total_us(QueryTrace& trace,
    std::chrono::steady_clock::time_point start, bool enabled) {
    if (enabled) {
        trace.total_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
    }
}

static const Candidate* committed_candidate_from_context(const Context& context) {
    const auto& candidates = context.candidates.candidates;
    int highlighted = context.candidates.highlighted;
    if (highlighted >= 0 && highlighted < (int)candidates.size() &&
        candidates[highlighted].text == context.committed_text) {
        return &candidates[highlighted];
    }
    auto it = std::find_if(candidates.begin(), candidates.end(),
        [&](const Candidate& c) { return c.text == context.committed_text; });
    return it != candidates.end() ? &(*it) : nullptr;
}

static bool alpha_code_is_probably_wubi(const std::string& code) {
    return code.size() == 4 && std::all_of(code.begin(), code.end(),
        [](char c) { return std::isalpha(static_cast<unsigned char>(c)); });
}

static void update_learning_entry(Dict* dict, const std::string& text,
                                  const std::string& fallback_code,
                                  const Candidate* candidate,
                                  bool allow_syllables) {
    if (!dict || text.empty())
        return;

    std::string code = fallback_code;
    if (candidate && !candidate->code.empty())
        code = candidate->code;
    if (code.empty())
        code = dict->reverse_lookup(text);
    if (code.empty())
        return;

    if (allow_syllables && candidate && !candidate->syllables.empty())
        dict->update_frequency(text, code, candidate->syllables);
    else
        dict->update_frequency(text, code);
}

// Static member for global query ID generation
std::atomic<uint64_t> Engine::next_query_id_{0};

// Self-contained: owns all resources (tests/tools).
bool Engine::initialize(const std::string& dict_path, const std::string& config_path) {
    if (!owned_dict_.open(dict_path))
        return false;

    if (!config_path.empty()) {
        owned_config_.load(config_path);
    }

    std::string sp_path = derive_spellings_path(dict_path);
    if (!sp_path.empty() && owned_spellings_.load(sp_path) && owned_spellings_.has_spellings()) {
        owned_syllabifier_ = std::make_unique<Syllabifier>(owned_spellings_);
    }

    return initialize(owned_dict_, owned_spellings_,
                      owned_syllabifier_.get(), owned_config_);
}

// Shared-resource: references pre-loaded data (server sessions).
bool Engine::initialize(Dict& dict, SpellingsIndex& spellings,
                        Syllabifier* syllabifier, const Config& config) {
    pinyin_dict_ = &dict;
    spellings_ = &spellings;
    syllabifier_ = syllabifier;
    config_ = &config;

    rebuild_pipeline(InputMode::PINYIN, true);
    init_per_session(config);
    return true;
}

void Engine::init_per_session(const Config& config) {
    ascii_composer_.load_config(config);
}

void Engine::finalize() {
    if (pinyin_dict_ == &owned_dict_) {
        owned_dict_.close();
    }
    context_.reset();
}

void Engine::reload_config(const Config& config) {
    config_ = &config;
    ascii_composer_.load_config(config);
    if (!config.candidate_learning && translator_) {
        translator_->clear_recent();
    }
}

void Engine::rebind_shared_resources(Dict& dict, SpellingsIndex& spellings,
                                     Syllabifier* syllabifier, Dict* wubi_dict) {
    pinyin_dict_ = &dict;
    spellings_ = &spellings;
    syllabifier_ = syllabifier;
    wubi_dict_ = wubi_dict;
    rebuild_pipeline(mode_, true);
}

ProcessResult Engine::process_key(const KeyEvent& event) {
    return process_key(event, OutputOptions{});
}

ProcessResult Engine::process_key(const KeyEvent& event, const OutputOptions& opts) {
    CXXIME_LOG(L"Engine::process_key: vk=%u, is_key_up=%d, composing=%d",
               event.keycode, event.is_key_up, context_.is_composing());

    // Phase 0: Initialize trace for this query (only if tracing enabled)
    // Preserve session_id/revision set by caller (server) before this call.
    std::chrono::steady_clock::time_point total_start;
    if (trace_enabled_) {
        uint32_t saved_session_id = trace_.session_id;
        uint64_t saved_revision = trace_.revision;
        trace_ = QueryTrace{};
        trace_.query_id = next_query_id_.fetch_add(1, std::memory_order_relaxed);
        trace_.session_id = saved_session_id;
        trace_.revision = saved_revision;
        total_start = std::chrono::steady_clock::now();
    }

    // Phase 1: Reset scratch buffer for this query
    scratch_.reset_for_query();

    // Create per-query deadline (Phase 3: QueryDeadline with expires_at)
    QueryDeadline per_query_deadline = QueryDeadline::from_now(query_deadline_ms_);

    // Phase 2: Let AsciiComposer track modifier key state (may toggle ascii_mode)
    ascii_composer_.process_key(event.keycode, event.is_key_up, context_, event.is_caps_lock());

    CXXIME_LOG(L"Engine::process_key: after ascii_composer, committed_text='%S'", context_.committed_text.c_str());

    // Phase 2.3: keyboard shortcuts for toggles
    if (!event.is_key_up) {
        // Shift+Space toggles full/half shape.
        if (event.keycode == 0x20 && event.is_shift() && !event.is_ctrl() && !event.is_alt()) {
            return ProcessResult::TOGGLE_SHAPE;
        }
        // Ctrl+. toggles Chinese/English punctuation.
        if (event.keycode == 0xBE && event.is_ctrl() && !event.is_alt()) {
            return ProcessResult::TOGGLE_PUNCT;
        }
    }

    // Check if AsciiComposer committed text (e.g. Shift toggle with code style)
    if (!context_.committed_text.empty()) {
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::COMMITTED;
    }

    // Propagate CapsLock style to Context for PinyinProcessor
    context_.caps_lock_style = ascii_composer_.get_binding(VK_CAPITAL);

    // Phase 2.4: CapsLock + letter commits directly with case inversion.
    // When CapsLock is not configured as an IME switch, keep the OS CapsLock
    // behavior: letters commit directly with case inversion.
    if (!event.is_key_up && event.is_caps_lock() && !ascii_composer_.is_ascii_mode() &&
        context_.caps_lock_style == AsciiModeSwitchStyle::NOOP) {
        uint32_t vk = event.keycode;
        if (vk >= 'A' && vk <= 'Z') {
            char ch = static_cast<char>(vk);
            if (event.is_shift())
                ch = static_cast<char>(tolower(ch));
            else
                ch = static_cast<char>(toupper(ch));
            context_.committed_text = std::string(1, ch);
            context_.set_commit_source(CommitSource::kRawCodePretransformed);
            record_total_us(trace_, total_start, trace_enabled_);
            return ProcessResult::COMMITTED;
        }
    }

    // Phase 2.5: intercept digit keys in English full-width mode.
    if (OutputComposer::intercept_key(event, opts, context_.committed_text)) {
        context_.set_commit_source(CommitSource::kRawCode);
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::COMMITTED;
    }

    // If in ASCII mode, handle letters/space directly
    if (ascii_composer_.is_ascii_mode() && !event.is_key_up) {
        uint32_t vk = event.keycode;

        // Letter keys (A-Z): commit as single ASCII char, respect Shift and CapsLock
        if (vk >= 'A' && vk <= 'Z') {
            char ch = static_cast<char>(vk);
            // Shift XOR CapsLock means uppercase; neither or both means lowercase.
            bool upper = event.is_shift() != event.is_caps_lock();
            if (!upper)
                ch = static_cast<char>(tolower(ch));
            context_.committed_text = opts.full_shape
                ? OutputComposer::to_full_width(ch)
                : std::string(1, ch);
            context_.set_commit_source(CommitSource::kRawCodePretransformed);
            if (ascii_composer_.is_temporary_ascii()) {
                ascii_composer_.set_ascii_mode(false);
            }
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::COMMITTED;
        }

        // Space: commit a space (full-width ideographic space when full_shape)
        if (vk == 0x20) {  // VK_SPACE
            context_.committed_text = opts.full_shape
                ? OutputComposer::to_full_width(' ')
                : " ";
            context_.set_commit_source(CommitSource::kRawCode);
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::COMMITTED;
        }

        // Enter is an application command in ASCII mode. Let the host handle it
        // so address bars and dialogs can navigate/confirm.
        if (vk == 0x0D) {  // VK_RETURN
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::REJECTED;
        }

        // Punctuation / full-shape handling
        if (handle_punctuation(event, context_, opts)) {
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::COMMITTED;
        }
        if (handle_full_shape(event, context_, opts)) {
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::COMMITTED;
        }

        // Other keys: reject (pass through to app)
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::REJECTED;
    }

    std::string committed_code_override;
    Candidate committed_candidate_override;
    bool has_committed_candidate_override = false;

    // Phase 4: PinyinProcessor
    std::chrono::steady_clock::time_point t0, t1, t2;
    if (trace_enabled_) {
        t0 = std::chrono::steady_clock::now();
    }
    auto result = processor_->process_key(event, context_);
    if (trace_enabled_) {
        t1 = std::chrono::steady_clock::now();
        trace_.processor_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        // Record raw_input AFTER processor updates the buffer (captures current state, not stale)
        if (result == ProcessResult::ACCEPTED) {
            size_t input_len = context_.pinyin_buffer.size();
            if (input_len >= sizeof(trace_.raw_input))
                input_len = sizeof(trace_.raw_input) - 1;
            std::memcpy(trace_.raw_input, context_.pinyin_buffer.data(), input_len);
            trace_.raw_input[input_len] = '\0';
        }
    }

    // Set commit_source based on pinyin_buffer state.
    // Note: pinyin_buffer.empty() is a heuristic; the authoritative fallback
    // is in Context::commit_with_source() which overrides to kCandidate
    // when candidates.highlighted is valid.
    // Skip if already set (e.g. append mode or engine-prepared ASCII output).
    if (result == ProcessResult::COMMITTED &&
        context_.commit_source() != CommitSource::kRawCodePreserveCase &&
        context_.commit_source() != CommitSource::kRawCodePretransformed) {
        if (context_.pinyin_buffer.empty()) {
            context_.set_commit_source(CommitSource::kRawCode);
        } else {
            context_.set_commit_source(CommitSource::kCandidate);
        }
    }

    // Auto-restore from temporary inline_ascii when composition ends
    if (result == ProcessResult::COMMITTED && ascii_composer_.is_temporary_ascii()) {
        ascii_composer_.set_ascii_mode(false);
    }

    // Phase 4.5: Punctuation / full-shape handling (only when processor rejected)
    if (result == ProcessResult::REJECTED) {
        const bool has_pending_candidate = context_.is_composing() &&
            context_.candidates.highlighted >= 0 &&
            context_.candidates.highlighted <
                static_cast<int>(context_.candidates.candidates.size());
        if (has_pending_candidate) {
            committed_code_override = context_.pinyin_buffer;
            committed_candidate_override =
                context_.candidates.candidates[context_.candidates.highlighted];
        }

        if (handle_punctuation(event, context_, opts)) {
            result = ProcessResult::COMMITTED;
            has_committed_candidate_override = has_pending_candidate;
        } else if (handle_full_shape(event, context_, opts)) {
            result = ProcessResult::COMMITTED;
            has_committed_candidate_override = has_pending_candidate;
        }
    }

    // Phase 5: After processing, update candidates if still composing
    if (result == ProcessResult::ACCEPTED && context_.is_composing()) {
        if (trace_enabled_) {
            trace_.page_index = context_.page_index;
            trace_.page_size = config_->page_size;
        }
        bool append_raw = context_.commit_source() == CommitSource::kRawCodePreserveCase;
        // Skip translate if deadline already expired (e.g. slow ascii_composer/processor)
        if (per_query_deadline.enabled && per_query_deadline.expired()) {
            if (trace_enabled_) {
                trace_.deadline_exceeded = true;
                trace_.truncated = true;
            }
        } else if (append_raw) {
            context_.candidates = {};
            context_.page_index = 0;
        } else {
            // Create a budget tuned for this input length, with per-query deadline
            QueryBudget effective_budget = make_budget((int)context_.pinyin_buffer.size(), config_->page_size);
            effective_budget.deadline = per_query_deadline;
            auto page = translator_->translate(context_.pinyin_buffer, context_.page_index, config_->page_size,
                                              trace_enabled_ ? &trace_ : nullptr, &effective_budget, &scratch_);
            context_.update_candidates(std::move(page));
        }
        if (trace_enabled_) {
            trace_.candidate_count = (int)context_.candidates.candidates.size();
        }

        // Auto-commit a unique 4-code candidate in Wubi or mixed mode.
        if (!append_raw && config_->wubi_auto_commit &&
            (mode_ == InputMode::WUBI || mode_ == InputMode::MIXED) &&
            result == ProcessResult::ACCEPTED) {
            if (context_.pinyin_buffer.size() == 4 &&
                context_.candidates.candidates.size() == 1) {
                committed_code_override = context_.pinyin_buffer;
                committed_candidate_override = context_.candidates.candidates[0];
                has_committed_candidate_override = true;
                context_.committed_text = context_.candidates.candidates[0].text;
                context_.set_commit_source(CommitSource::kCandidate);
                context_.pinyin_buffer.clear();
                context_.candidates = {};
                result = ProcessResult::COMMITTED;
            }
        }
    }
    if (trace_enabled_) {
        t2 = std::chrono::steady_clock::now();
        trace_.translate_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    }

    CXXIME_LOG(L"Engine::process_key: result=%d, buf='%S'",
               (int)result, context_.pinyin_buffer.c_str());

    // Phase 6: If a candidate was committed, update user frequency and recent cache.
    if (config_->candidate_learning &&
        result == ProcessResult::COMMITTED && !context_.committed_text.empty() &&
        context_.commit_source() == CommitSource::kCandidate) {
        std::string typed_code = has_committed_candidate_override
            ? committed_code_override
            : context_.pinyin_buffer;
        const Candidate* committed_candidate = has_committed_candidate_override
            ? &committed_candidate_override
            : committed_candidate_from_context(context_);

        bool is_wubi = false;
        if (mode_ == InputMode::WUBI) {
            is_wubi = true;
        } else if (mode_ == InputMode::MIXED) {
            is_wubi = committed_candidate
                ? (committed_candidate->source == CandidateSource::kWubi)
                : alpha_code_is_probably_wubi(typed_code);
        }

        Dict* active_dict = is_wubi ? wubi_dict_ : pinyin_dict_;
        const std::string& learned_text = committed_candidate
            ? committed_candidate->text
            : context_.committed_text;
        update_learning_entry(active_dict, learned_text, typed_code,
                              committed_candidate, !is_wubi);

        if (!typed_code.empty()) {
            Candidate recent;
            if (committed_candidate) {
                recent = *committed_candidate;
            }
            recent.text = learned_text;
            translator_->update_recent(typed_code, recent);
        }
    }

    // Phase 7: Finalize trace
    if (trace_enabled_) {
        auto total_end = std::chrono::steady_clock::now();
        trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
    }

    return result;
}

void Engine::commit_with_punctuation(Context& context, const std::string& output,
                                     const std::vector<std::string>*, int) {
    if (context.is_composing() && !context.candidates.candidates.empty() &&
        context.candidates.highlighted >= 0 &&
        context.candidates.highlighted < (int)context.candidates.candidates.size()) {
        context.committed_text = context.candidates.candidates[context.candidates.highlighted].text + output;
    } else {
        context.committed_text = output;
    }
    context.pinyin_buffer.clear();
    context.candidates = {};
    context.page_index = 0;
    context.set_commit_source(CommitSource::kCandidate);
    context.last_committed_char = output.back();
}

bool Engine::handle_punctuation(const KeyEvent& event, Context& context, const OutputOptions& opts) {
    // Guard: key-up
    if (event.is_key_up)
        return false;

    // Guard: modifier keys
    if (event.is_ctrl() || event.is_alt())
        return false;

    // Guard: chinese_punct or (full_shape with custom mapping)
    bool should_process = opts.chinese_punct ||
        (opts.full_shape && opts.punct_mapping && !opts.punct_mapping->full_shape.empty());
    if (!should_process)
        return false;

    // Step 1: convert virtual key to character.
    char ch = vk_to_char(event.keycode, event.is_shift());
    if (ch == '\0')
        return false;

    // Step 2: check punct_mapping
    if (!opts.punct_mapping)
        return false;

    // Step 3: select mapping table
    std::string key(1, ch);
    const PunctEntry* entry = nullptr;
    if (opts.chinese_punct) {
        auto it = opts.punct_mapping->half_shape.find(key);
        if (it != opts.punct_mapping->half_shape.end())
            entry = &it->second;
    } else if (opts.full_shape) {
        auto it = opts.punct_mapping->full_shape.find(key);
        if (it != opts.punct_mapping->full_shape.end())
            entry = &it->second;
    }

    // Step 4: not found
    if (!entry)
        return false;

    // Step 5: digit separator guard
    if (context.pinyin_buffer.empty() &&
        std::isdigit(static_cast<unsigned char>(context.last_committed_char))) {
        if (key == "." || key == ",")
            return false;
    }

    // Step 6: process punct type
    std::string output;
    switch (entry->type) {
    case PunctType::COMMIT:
        output = entry->commit;
        break;
    case PunctType::PAIR: {
        bool opened = context.pair_open[key];
        int idx = opened ? 1 : 0;
        output = entry->pair[idx];
        context.pair_open[key] = !opened;
        break;
    }
    case PunctType::ALTERNATIVES: {
        int idx = context.alt_index[key] % static_cast<int>(entry->alternatives.size());
        output = entry->alternatives[idx];
        context.alt_index[key] = idx + 1;
        break;
    }
    }

    // Step 7: commit
    commit_with_punctuation(context, output, nullptr, 0);
    CXXIME_LOG(L"handle_punctuation: key='%c' -> '%S'", ch, output.c_str());
    return true;
}

bool Engine::handle_full_shape(const KeyEvent& event, Context& context, const OutputOptions& opts) {
    // Guard: key-up
    if (event.is_key_up)
        return false;

    // Guard: modifier keys
    if (event.is_ctrl() || event.is_alt())
        return false;

    // Guard: full_shape mode
    if (!opts.full_shape)
        return false;

    // Step 1: convert virtual key to character.
    char ch = vk_to_char(event.keycode, event.is_shift());
    if (ch == '\0') {
        // vk_to_char handles OEM punctuation + digit keys; only letters remain
        uint32_t vk = event.keycode;
        if (vk >= 'A' && vk <= 'Z') {
            ch = event.is_shift()
                ? static_cast<char>(vk)
                : static_cast<char>(vk + 32);  // to lowercase
        } else {
            return false;
        }
    }

    // Step 2: range check
    if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7e)
        return false;

    // Step 3: full-width conversion
    std::string output = OutputComposer::to_full_width(ch);

    // Step 4: commit
    commit_with_punctuation(context, output, nullptr, 0);
    CXXIME_LOG(L"handle_full_shape: '%c' -> full-width", ch);
    return true;
}

const Context& Engine::context() const {
    return context_;
}

Context& Engine::context() {
    return context_;
}

bool Engine::select_candidate(int index) {
    if (index < 0 || index >= (int)context_.candidates.candidates.size())
        return false;

    context_.candidates.highlighted = index;
    context_.committed_text = context_.candidates.candidates[index].text;
    context_.set_commit_source(CommitSource::kCandidate);

    if (config_->candidate_learning) {
        auto& cand = context_.candidates.candidates[index];
        std::string code = context_.pinyin_buffer;
        bool is_wubi = mode_ == InputMode::WUBI ||
            (mode_ == InputMode::MIXED && cand.source == CandidateSource::kWubi);
        Dict* active_dict = is_wubi ? wubi_dict_ : pinyin_dict_;
        update_learning_entry(active_dict, context_.committed_text, code, &cand, !is_wubi);

        // Update the session recent cache only when adaptive ordering is enabled.
        if (!context_.pinyin_buffer.empty()) {
            translator_->update_recent(context_.pinyin_buffer,
                                    context_.candidates.candidates[index]);
        }
    }

    return true;
}

std::string Engine::get_commit_text() {
    std::string text = context_.committed_text;
    context_.pinyin_buffer.clear();
    context_.committed_text.clear();
    context_.candidates = {};
    context_.page_index = 0;
    context_.set_commit_source(CommitSource::kRawCode);
    return text;
}

std::pair<std::string, CommitSource> Engine::take_commit_text_with_source() {
    auto result = std::make_pair(std::move(context_.committed_text), context_.commit_source());
    context_.reset();
    return result;
}

std::pair<std::string, CommitSource> Engine::commit_composition_with_source() {
    return context_.commit_with_source();
}

void Engine::clear() {
    context_.reset();
    translator_->clear_recent();
}

void Engine::clear_composition() {
    context_.reset();
    // Preserve session recent cache; do not call translator_->clear_recent().
}

void Engine::set_wubi_dict(Dict* dict) {
    wubi_dict_ = dict;
    if (wubi_dict_)
        wubi_dict_->set_user_scoring_profile(UserScoringProfile::kWubi);
}

void Engine::set_fuzzy_enabled(bool enabled) {
    if (!spellings_)
        return;
    spellings_->set_fuzzy_enabled(enabled);
    rebuild_pipeline(mode_, true);
}

void Engine::switch_mode(InputMode mode) {
    rebuild_pipeline(mode);
}

void Engine::rebuild_pipeline(InputMode mode, bool force) {
    // Fall back to pinyin when the optional Wubi dictionary is unavailable.
    if ((mode == InputMode::WUBI || mode == InputMode::MIXED) && !wubi_dict_)
        mode = InputMode::PINYIN;

    if (!force && mode == mode_) return;

    context_.reset();

    mode_ = mode;
    if (mode == InputMode::WUBI) {
        processor_ = std::make_unique<WubiProcessor>();
        auto wubi_trans = std::make_unique<WubiTranslator>();
        wubi_trans->set_dict(wubi_dict_);
        translator_ = std::move(wubi_trans);
    } else if (mode == InputMode::MIXED) {
        processor_ = std::make_unique<PinyinProcessor>();
        auto mixed_trans = std::make_unique<MixedTranslator>();
        mixed_trans->set_pinyin_dict(pinyin_dict_);
        mixed_trans->set_wubi_dict(wubi_dict_);
        if (syllabifier_) {
            mixed_trans->set_syllabifier(syllabifier_);
        }
        if (pinyin_dict_->has_short_cache()) {
            mixed_trans->set_short_cache(&pinyin_dict_->short_cache());
        }
        translator_ = std::move(mixed_trans);
    } else {
        processor_ = std::make_unique<PinyinProcessor>();
        auto pinyin_trans = std::make_unique<PinyinTranslator>();
        pinyin_trans->set_dict(pinyin_dict_);
        if (syllabifier_) {
            pinyin_trans->set_syllabifier(syllabifier_);
        }
        if (pinyin_dict_->has_short_cache()) {
            pinyin_trans->set_short_cache(&pinyin_dict_->short_cache());
        }
        translator_ = std::move(pinyin_trans);
    }
}

std::string Engine::derive_spellings_path(const std::string& dict_path) {
    // pinyin.dict.bin -> pinyin.spellings.bin
    // pinyin.dict.db  -> pinyin.spellings.bin
    static const char kDictBinExt[] = ".dict.bin";
    static const char kDictDbExt[] = ".dict.db";
    static const char kSpellingsExt[] = ".spellings.bin";

    std::string path = dict_path;
    auto replace_ext = [&](const char* from, const char* to) {
        size_t pos = path.rfind(from);
        if (pos != std::string::npos && pos + strlen(from) == path.size()) {
            path.replace(pos, strlen(from), to);
            return true;
        }
        return false;
    };

    if (replace_ext(kDictBinExt, kSpellingsExt))
        return path;
    if (replace_ext(kDictDbExt, kSpellingsExt))
        return path;

    // Unknown extension: append .spellings.bin.
    return path + kSpellingsExt;
}

} // namespace cxxime
