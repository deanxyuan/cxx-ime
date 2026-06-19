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

    // Create processor and translator instances
    processor_ = std::make_unique<PinyinProcessor>();
    auto pinyin_trans = std::make_unique<PinyinTranslator>();
    pinyin_trans->set_dict(pinyin_dict_);
    if (syllabifier_) {
        pinyin_trans->set_syllabifier(syllabifier_);
    }
    if (pinyin_dict_->has_short_cache()) {
        pinyin_trans->set_short_cache(&pinyin_dict_->short_cache());
        CXXIME_LOG(L"Engine: short_cache loaded");
    } else {
        CXXIME_LOG(L"Engine: short_cache NOT loaded");
    }
    translator_ = std::move(pinyin_trans);

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
    ascii_composer_.process_key(event.keycode, event.is_key_up, context_);

    CXXIME_LOG(L"Engine::process_key: after ascii_composer, committed_text='%S'", context_.committed_text.c_str());

    // Phase 2.3: keyboard shortcuts for toggles
    if (!event.is_key_up) {
        // Shift+Space → toggle full/half shape
        if (event.keycode == 0x20 && event.is_shift() && !event.is_ctrl() && !event.is_alt()) {
            return ProcessResult::TOGGLE_SHAPE;
        }
        // Ctrl+. → toggle Chinese/English punctuation
        if (event.keycode == 0xBE && event.is_ctrl() && !event.is_alt()) {
            return ProcessResult::TOGGLE_PUNCT;
        }
    }

    // Check if AsciiComposer committed text (e.g. Shift toggle with commit_text)
    if (!context_.committed_text.empty()) {
        context_.set_commit_source(CommitSource::kRawCode);
        if (trace_enabled_) {
            auto total_end = std::chrono::steady_clock::now();
            trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
        }
        return ProcessResult::COMMITTED;
    }

    // Phase 2.5: intercept_key — in English + full-width mode, intercept digit keys
    if (OutputComposer::intercept_key(event, opts, config_->good_old_caps_lock,
                                      context_.committed_text)) {
        context_.set_commit_source(CommitSource::kRawCode);
        if (trace_enabled_) {
            auto total_end = std::chrono::steady_clock::now();
            trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
        }
        return ProcessResult::COMMITTED;
    }

    // If in ASCII mode, handle letters/space directly
    if (ascii_composer_.is_ascii_mode() && !event.is_key_up) {
        uint32_t vk = event.keycode;

        // Letter keys (A-Z): commit as single ASCII char, respect Shift
        if (vk >= 'A' && vk <= 'Z') {
            char ch = static_cast<char>(vk);
            if (!event.is_shift())
                ch = static_cast<char>(tolower(ch));
            context_.committed_text = opts.full_shape
                ? OutputComposer::to_full_width(ch)
                : std::string(1, ch);
            context_.set_commit_source(CommitSource::kRawCode);
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

        // Enter: commit pending text and newline
        if (vk == 0x0D) {  // VK_RETURN
            context_.committed_text = "\r\n";
            context_.set_commit_source(CommitSource::kRawCode);
            if (ascii_composer_.is_temporary_ascii()) {
                ascii_composer_.set_ascii_mode(false);
            }
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::COMMITTED;
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
        if (trace_enabled_) {
            auto total_end = std::chrono::steady_clock::now();
            trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
        }
        return ProcessResult::REJECTED;
    }

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
    // Note: pinyin_buffer.empty() is a heuristic — the authoritative fallback
    // is in Context::commit_with_source() which overrides to kCandidate
    // when candidates.highlighted is valid.
    if (result == ProcessResult::COMMITTED) {
        if (context_.pinyin_buffer.empty()) {
            context_.set_commit_source(CommitSource::kRawCode);   // Enter 提交拼音
        } else {
            context_.set_commit_source(CommitSource::kCandidate); // 选词
        }
    }

    // Auto-restore from temporary inline_ascii when composition ends
    if (result == ProcessResult::COMMITTED && ascii_composer_.is_temporary_ascii()) {
        ascii_composer_.set_ascii_mode(false);
    }

    // Phase 4.5: Punctuation / full-shape handling (only when processor rejected)
    if (result == ProcessResult::REJECTED) {
        if (handle_punctuation(event, context_, opts))
            result = ProcessResult::COMMITTED;
        else if (handle_full_shape(event, context_, opts))
            result = ProcessResult::COMMITTED;
    }

    // Phase 5: After processing, update candidates if still composing
    if (result == ProcessResult::ACCEPTED && context_.is_composing()) {
        if (trace_enabled_) {
            trace_.page_index = context_.page_index;
            trace_.page_size = config_->page_size;
        }
        // Skip translate if deadline already expired (e.g. slow ascii_composer/processor)
        if (per_query_deadline.enabled && per_query_deadline.expired()) {
            if (trace_enabled_) {
                trace_.deadline_exceeded = true;
                trace_.truncated = true;
            }
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

        // 五笔/混输模式：四码唯一候选自动上屏
        if (config_->wubi_auto_commit_4code && (mode_ == InputMode::WUBI || mode_ == InputMode::MIXED) && result == ProcessResult::ACCEPTED) {
            if (context_.pinyin_buffer.size() == 4 &&
                context_.candidates.candidates.size() == 1) {
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

    // Phase 6: If committed, update user frequency and recent cache
    if (result == ProcessResult::COMMITTED && !context_.committed_text.empty()) {
        std::string code = context_.pinyin_buffer;
        Dict* active_dict;
        if (mode_ == InputMode::WUBI) {
            active_dict = wubi_dict_;
        } else if (mode_ == InputMode::MIXED) {
            // Find the committed candidate to determine its source
            auto& cands = context_.candidates.candidates;
            auto it = std::find_if(cands.begin(), cands.end(),
                [&](const Candidate& c) { return c.text == context_.committed_text; });
            bool is_wubi = (it != cands.end())
                ? (it->source == CandidateSource::kWubi)
                : (code.size() == 4 && std::all_of(code.begin(), code.end(),
                    [](char c) { return std::isalpha(static_cast<unsigned char>(c)); }));
            active_dict = is_wubi ? wubi_dict_ : pinyin_dict_;
        } else {
            active_dict = pinyin_dict_;
        }
        if (code.empty() && active_dict) {
            code = active_dict->reverse_lookup(context_.committed_text);
        }
        if (active_dict) {
            active_dict->update_frequency(context_.committed_text, code);
        }
        // Phase 4: update session recent cache for short input fast path
        if (!context_.pinyin_buffer.empty()) {
            Candidate c;
            c.text = context_.committed_text;
            c.frequency = 0;
            translator_->update_recent(context_.pinyin_buffer, c);
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

    // Step 1: VK → character
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

    // Step 1: VK → character
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

    std::string code = context_.pinyin_buffer;
    auto& cand = context_.candidates.candidates[index];
    Dict* active_dict;
    if (mode_ == InputMode::WUBI) {
        active_dict = wubi_dict_;
    } else if (mode_ == InputMode::MIXED) {
        active_dict = (cand.source == CandidateSource::kWubi) ? wubi_dict_ : pinyin_dict_;
    } else {
        active_dict = pinyin_dict_;
    }
    if (code.empty() && active_dict)
        code = active_dict->reverse_lookup(context_.committed_text);
    if (!code.empty() && active_dict)
        active_dict->update_frequency(context_.committed_text, code);

    // Phase 4: update session recent cache for short input fast path
    if (!context_.pinyin_buffer.empty()) {
        translator_->update_recent(context_.pinyin_buffer,
                                  context_.candidates.candidates[index]);
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
    // Preserve session recent cache — don't call translator_->clear_recent()
}

void Engine::set_wubi_dict(Dict* dict) {
    wubi_dict_ = dict;
}

void Engine::set_fuzzy_enabled(bool enabled) {
    if (spellings_) spellings_->set_fuzzy_enabled(enabled);
}

void Engine::switch_mode(InputMode mode) {
    // 五笔词典未加载时，强制回退拼音模式
    if ((mode == InputMode::WUBI || mode == InputMode::MIXED) && !wubi_dict_)
        mode = InputMode::PINYIN;

    if (mode == mode_) return;

    context_.reset();

    mode_ = mode;
    if (mode == InputMode::WUBI) {
        // 五笔模式：创建 WubiProcessor 和 WubiTranslator
        processor_ = std::make_unique<WubiProcessor>();
        auto wubi_trans = std::make_unique<WubiTranslator>();
        wubi_trans->set_dict(wubi_dict_);
        translator_ = std::move(wubi_trans);
    } else if (mode == InputMode::MIXED) {
        // 混输模式：复用 PinyinProcessor，新建 MixedTranslator 同时查询两个词典
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
        // 拼音模式：恢复 PinyinProcessor 和 PinyinTranslator
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
    // pinyin.dict.bin → pinyin.spellings.bin
    // pinyin.dict.db  → pinyin.spellings.bin
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

    // Unknown extension — append .spellings.bin
    return path + kSpellingsExt;
}

} // namespace cxxime
