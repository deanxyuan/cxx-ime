// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/engine.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <optional>

#include <windows.h>

#include <cxxime/input_limits.h>
#include <cxxime/logging.h>
#include <cxxime/mixed_translator.h>
#include <cxxime/output_composer.h>
#include <cxxime/wubi_processor.h>
#include <cxxime/wubi_translator.h>

namespace cxxime {

static inline void record_total_us(QueryTrace& trace,
    std::chrono::steady_clock::time_point start, bool enabled) {
    if (enabled) {
        trace.total_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
    }
}

static void add_wubi_code_hints(const std::string& input, CandidatePage& page) {
    for (auto& candidate : page.candidates) {
        if (candidate.source != CandidateSource::kWubi || candidate.code.size() <= input.size() ||
            candidate.code.compare(0, input.size(), input) != 0) {
            continue;
        }

        const size_t suffix_size = candidate.code.size() - input.size();
        if (suffix_size > 3) {
            continue;
        }
        const auto suffix_begin = candidate.code.begin() + input.size();
        if (!std::all_of(suffix_begin, candidate.code.end(),
            [](char ch) { return ch >= 'a' && ch <= 'z'; })) {
            continue;
        }
        candidate.comment.assign(suffix_begin, candidate.code.end());
    }
}

static bool record_candidate_preference(Dict* dict, const Candidate* candidate,
                                        const std::string& typed_code) {
    return dict && candidate && !typed_code.empty() &&
           dict->record_candidate_preference(*candidate, typed_code);
}

static bool is_main_candidate_command(const KeyEvent& event, const Context& context) {
    if (context.candidates.candidates.empty() || event.is_shift() || event.is_ctrl() ||
        event.is_alt()) {
        return false;
    }
    return (event.keycode >= '1' && event.keycode <= '9') || event.keycode == VK_OEM_MINUS ||
           event.keycode == VK_OEM_PLUS;
}

static bool is_numpad_text_key(uint32_t keycode) {
    return (keycode >= VK_NUMPAD0 && keycode <= VK_NUMPAD9) || keycode == VK_ADD ||
           keycode == VK_SUBTRACT || keycode == VK_MULTIPLY || keycode == VK_DIVIDE ||
           keycode == VK_DECIMAL;
}

static bool is_inline_ascii_character(char ch) {
    static constexpr char kInlineCharacters[] = "`~@#$%^&*_+{}|";
    return std::strchr(kInlineCharacters, ch) != nullptr;
}

static bool is_ascii_digit_or_symbol(char ch) {
    return ch >= 0x21 && ch <= 0x7e && !std::isalpha(static_cast<unsigned char>(ch));
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
                        Syllabifier* syllabifier, const Config& config,
                        const SymbolTable* symbol_table) {
    pinyin_dict_ = &dict;
    spellings_ = &spellings;
    syllabifier_ = syllabifier;
    config_ = &config;
    symbol_table_ = symbol_table;

    rebuild_pipeline(InputMode::PINYIN, true);
    init_per_session(config);
    return true;
}

void Engine::init_per_session(const Config& config) {
    ascii_composer_.load_config(config);
    input_mode_switch_shortcut_ = config.input_mode_switch_shortcut;
}

void Engine::finalize() {
    if (pinyin_dict_ == &owned_dict_) {
        owned_dict_.close();
    }
    reset_composition_state();
    handled_shortcut_key_ = 0;
}

CandidatePage Engine::translate_for_search(const std::string& input, int limit) {
    if (!translator_ || input.empty() || limit <= 0) {
        return {};
    }

    QueryScratch scratch;
    QueryBudget budget = budget_;
    budget.deadline = QueryDeadline{};
    return translator_->translate(input, 0, limit, nullptr, &budget, &scratch, 0);
}

bool Engine::record_search_result(const std::string& input, const std::string& result) {
    if (!translator_ || input.empty() || result.empty()) {
        return false;
    }

    const CandidatePage page = translate_for_search(input, 64);
    const auto candidate = std::find_if(
        page.candidates.begin(), page.candidates.end(),
        [&](const Candidate& item) { return item.text == result; });
    if (candidate == page.candidates.end()) {
        return false;
    }
    Dict* dict = candidate->source == CandidateSource::kWubi ? wubi_dict_ : pinyin_dict_;
    return record_candidate_preference(dict, &*candidate, input);
}

void Engine::reload_config(const Config& config) {
    config_ = &config;
    ascii_composer_.load_config(config);
    input_mode_switch_shortcut_ = config.input_mode_switch_shortcut;
    if (mode_ == InputMode::MIXED && translator_) {
        static_cast<MixedTranslator*>(translator_.get())
            ->set_candidate_preference(config.mixed_candidate_preference);
    }
    if (translator_) {
        translator_->set_candidate_learning_enabled(config.candidate_learning);
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

ProcessResult Engine::process_key(const KeyEvent& event, const OutputOptions& opts,
                                  int visible_candidate_count) {
    CXXIME_LOG(L"Engine::process_key: vk=%u, is_key_up=%d, composing=%d",
               event.keycode, event.is_key_up, context_.is_composing());
    commit_continues_composition_ = false;
    context_.clear_commit_evidence();

    const uint64_t input_revision_before = context_.preedit_revision();
    const int page_index_before = context_.page_index;
    const int page_offset_before = context_.page_offset;
    context_.visible_candidate_count = (std::max)(0, visible_candidate_count);

    // Initialize the trace for this query when tracing is enabled.
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

    // Reset the scratch buffer for this query.
    scratch_.reset_for_query();

    // Create the per-query deadline.
    QueryDeadline per_query_deadline = QueryDeadline::from_now(query_deadline_ms_);

    // Let AsciiComposer track modifier key state and update ascii_mode when needed.
    const bool modifier_binding_applied =
        ascii_composer_.process_key(event.keycode, event.is_key_up, context_,
                                    event.is_caps_lock());

    CXXIME_LOG(L"Engine::process_key: after ascii_composer, committed_text='%S'", context_.committed_text.c_str());

    if (event.is_key_up && handled_shortcut_key_ != 0 &&
        event.keycode == handled_shortcut_key_) {
        handled_shortcut_key_ = 0;
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::INPUT_MODE_SHORTCUT_HANDLED;
    }
    if (!event.is_key_up && handled_shortcut_key_ != 0 &&
        event.keycode != handled_shortcut_key_) {
        handled_shortcut_key_ = 0;
    }
    if (!event.is_key_up && input_mode_switch_shortcut_.matches(event)) {
        if (handled_shortcut_key_ == 0) {
            handled_shortcut_key_ = event.keycode;
            reset_composition_state();
            record_total_us(trace_, total_start, trace_enabled_);
            return ProcessResult::SWITCH_INPUT_MODE;
        }
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::INPUT_MODE_SHORTCUT_HANDLED;
    }

    // Handle keyboard shortcuts for mode toggles.
    if (!event.is_key_up) {
        // Shift+Space toggles full/half shape.
        if (event.keycode == 0x20 && event.is_shift() && !event.is_ctrl() && !event.is_alt()) {
            return ProcessResult::TOGGLE_SHAPE;
        }
        // Ctrl+. toggles Chinese/English punctuation.
        if (event.keycode == 0xBE && event.is_ctrl() &&
            !event.is_shift() && !event.is_alt()) {
            return ProcessResult::TOGGLE_PUNCT;
        }
    }

    // Application and system shortcuts own modified key combinations unless
    // CxxIME matched an explicit shortcut above.
    if (event.is_ctrl() || event.is_alt()) {
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::REJECTED;
    }

    // The numeric keypad is a literal number/formula input area. Its digits and operators stay
    // ASCII regardless of Chinese punctuation or full-shape mode.
    if (!event.is_key_up && !context_.is_composing() && is_numpad_text_key(event.keycode)) {
        const std::optional<char> numpad_character = normalize_ascii_key(event);
        if (numpad_character) {
            context_.committed_text.assign(1, *numpad_character);
            context_.set_commit_source(CommitSource::kRawCodePretransformed);
            record_total_us(trace_, total_start, trace_enabled_);
            return ProcessResult::COMMITTED;
        }
    }

    // Check if AsciiComposer committed text (e.g. Shift toggle with code style)
    if (!context_.committed_text.empty()) {
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::COMMITTED;
    }

    // Keep modifier key-up available to the host while allowing the server to return the
    // composition snapshot after a binding converts IME/Symbol input to inline ASCII.
    if (event.is_key_up && modifier_binding_applied && context_.is_composing()) {
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::ACCEPTED;
    }

    // Enter always submits the exact active preedit without changing the persistent mode.
    if (!event.is_key_up && event.keycode == VK_RETURN && context_.is_composing()) {
        context_.committed_text = context_.pinyin_buffer;
        context_.set_commit_source(CommitSource::kRawCodePreserveCase);
        context_.clear_preedit();
        context_.candidates = {};
        context_.reset_pagination();
        ascii_composer_.finish_temporary_ascii();
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::COMMITTED;
    }

    const InlineAsciiResult inline_result =
        ascii_composer_.process_inline_ascii_composition(event, context_, opts.chinese_mode);
    if (inline_result != InlineAsciiResult::kNotHandled) {
        if (inline_result == InlineAsciiResult::kResumeOrigin && context_.is_composing()) {
            context_.update_candidates(translate_current_composition(per_query_deadline));
        }
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::ACCEPTED;
    }

    // Propagate CapsLock style to Context for PinyinProcessor
    context_.caps_lock_style = ascii_composer_.get_binding(VK_CAPITAL);

    // CapsLock plus a letter commits directly with case inversion.
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

    // Intercept digit keys in English full-width mode.
    if (!context_.is_composing() &&
        OutputComposer::intercept_key(event, opts, context_.committed_text)) {
        context_.set_commit_source(CommitSource::kRawCode);
        record_total_us(trace_, total_start, trace_enabled_);
        return ProcessResult::COMMITTED;
    }

    // If in ASCII mode, handle letters/space directly
    if (ascii_composer_.is_ascii_mode() && !context_.is_composing() && !event.is_key_up) {
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
    const bool symbol_trigger_enabled = symbol_input_enabled(opts);
    const bool symbol_trigger = symbol_trigger_enabled && !context_.is_composing() &&
                                SymbolProcessor::is_trigger(event);
    const std::optional<char> normalized = normalize_ascii_key(event);
    const bool plain_main_zero = event.keycode == '0' && !event.is_shift();
    std::optional<ProcessResult> routed_result;

    if (context_.composition_kind() == CompositionKind::kIme && context_.is_composing() &&
        normalized && is_ascii_digit_or_symbol(*normalized) &&
        !is_main_candidate_command(event, context_)) {
        const bool has_candidates = !context_.candidates.candidates.empty();
        const bool start_inline = is_inline_ascii_character(*normalized) ||
                                  plain_main_zero || is_numpad_text_key(event.keycode) ||
                                  !has_candidates;
        if (opts.full_shape && !is_numpad_text_key(event.keycode)) {
            if (handle_full_shape(event, context_, opts)) {
                routed_result = ProcessResult::COMMITTED;
            }
        } else if (start_inline) {
            if (context_.pinyin_buffer.size() < kMaxInputCodeLength) {
                context_.enter_inline_ascii(true);
                context_.insert_preedit(*normalized);
                context_.candidates = {};
                context_.reset_pagination();
            }
            routed_result = ProcessResult::ACCEPTED;
        } else {
            if (!handle_punctuation(event, context_, opts)) {
                commit_with_punctuation(context_, std::string(1, *normalized));
            }
            routed_result = ProcessResult::COMMITTED;
        }
    }

    const bool plain_letter_key = !event.is_key_up && !event.is_shift() && !event.is_ctrl() &&
                                  !event.is_alt() && !event.is_caps_lock() &&
                                  event.keycode >= 'A' && event.keycode <= 'Z';
    const bool has_multiple_wubi_candidates =
        !context_.candidates.candidates.empty() &&
        (context_.candidates.total_count > 1 || context_.candidates.candidates.size() > 1) &&
        context_.candidates.candidates.front().source == CandidateSource::kWubi;
    const bool commit_wubi_before_next_code =
        config_->wubi_commit_first_on_fifth_key &&
        (mode_ == InputMode::WUBI || mode_ == InputMode::MIXED) &&
        plain_letter_key && context_.pinyin_buffer.size() == 4 &&
        context_.preedit_cursor() == context_.pinyin_buffer.size() &&
        has_multiple_wubi_candidates;
    const bool restart_wubi_on_fifth_after_miss =
        config_->wubi_restart_on_fifth_after_miss && mode_ == InputMode::WUBI &&
        context_.composition_kind() == CompositionKind::kIme && plain_letter_key &&
        context_.pinyin_buffer.size() == 4 &&
        context_.preedit_cursor() == context_.pinyin_buffer.size() &&
        context_.candidates.total_count == 0 && context_.candidates.candidates.empty();
    if (!routed_result && commit_wubi_before_next_code) {
        committed_code_override = context_.pinyin_buffer;
        committed_candidate_override = context_.candidates.candidates.front();
        has_committed_candidate_override = true;
        context_.clear_preedit();
        context_.candidates = {};
        context_.reset_pagination();
    } else if (!routed_result && restart_wubi_on_fifth_after_miss) {
        const char restarted_code = static_cast<char>(event.keycode - 'A' + 'a');
        context_.start_composition(
            CompositionKind::kIme, std::string(1, restarted_code), 1);
        context_.candidates = {};
        context_.reset_pagination();
        routed_result = ProcessResult::ACCEPTED;
    }

    // Process pinyin and Wubi input.
    std::chrono::steady_clock::time_point t0, t1, t2;
    if (trace_enabled_) {
        t0 = std::chrono::steady_clock::now();
    }
    ProcessResult result;
    if (routed_result) {
        result = *routed_result;
    } else if (SymbolProcessor::is_active(context_) || symbol_trigger) {
        result = symbol_processor_.process_key(event, context_, symbol_trigger_enabled);
    } else {
        result = processor_->process_key(event, context_);
    }

    if (result == ProcessResult::REJECTED &&
        context_.composition_kind() == CompositionKind::kSymbol && normalized &&
        is_ascii_digit_or_symbol(*normalized)) {
        const bool has_candidates = !context_.candidates.candidates.empty();
        const bool start_inline = is_inline_ascii_character(*normalized) ||
                                  plain_main_zero || is_numpad_text_key(event.keycode);
        if (opts.full_shape && !is_numpad_text_key(event.keycode)) {
            if (handle_full_shape(event, context_, opts)) {
                result = ProcessResult::COMMITTED;
            }
        } else if (has_candidates && !start_inline) {
            if (!handle_punctuation(event, context_, opts)) {
                commit_with_punctuation(context_, std::string(1, *normalized));
            }
            result = ProcessResult::COMMITTED;
        } else {
            if (context_.pinyin_buffer.size() < kMaxInputCodeLength) {
                context_.enter_inline_ascii(true);
                context_.insert_preedit(*normalized);
                context_.candidates = {};
                context_.reset_pagination();
            }
            result = ProcessResult::ACCEPTED;
        }
    }
    const bool input_changed = context_.preedit_revision() != input_revision_before;
    if (input_changed) {
        context_.reset_pagination();
    }
    const bool pagination_changed = context_.page_index != page_index_before ||
                                    context_.page_offset != page_offset_before;
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
        context_.commit_source() == CommitSource::kRawCode) {
        if (context_.pinyin_buffer.empty()) {
            context_.set_commit_source(CommitSource::kRawCode);
        } else {
            context_.set_commit_source(CommitSource::kCandidate);
        }
    }

    // Auto-restore from temporary inline_ascii when composition ends
    if (result == ProcessResult::COMMITTED && ascii_composer_.is_temporary_ascii()) {
        ascii_composer_.finish_temporary_ascii();
    }

    // Handle punctuation and full-width input only when the input processor rejects the key.
    if (result == ProcessResult::REJECTED && !SymbolProcessor::is_active(context_)) {
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

    // Update candidates after processing while the composition remains active.
    if (result == ProcessResult::ACCEPTED && context_.is_composing()) {
        if (trace_enabled_) {
            trace_.page_index = context_.page_index;
            trace_.page_size = config_->page_size;
        }
        bool append_raw = context_.composition_kind() == CompositionKind::kInlineAscii ||
                          context_.commit_source() == CommitSource::kRawCodePreserveCase;
        const bool refresh_candidates = input_changed || pagination_changed;
        if (refresh_candidates) {
            // Skip translate if deadline already expired (e.g. slow ascii_composer/processor)
            if (per_query_deadline.enabled && per_query_deadline.expired()) {
                if (trace_enabled_) {
                    trace_.deadline_exceeded = true;
                    trace_.truncated = true;
                }
            } else if (append_raw) {
                context_.candidates = {};
                context_.reset_pagination();
            } else {
                context_.update_candidates(
                    translate_current_composition(per_query_deadline));
            }
        }
        if (trace_enabled_) {
            trace_.candidate_count = (int)context_.candidates.candidates.size();
        }

        // Auto-commit a unique 4-code Wubi candidate in Wubi or mixed mode.
        if (refresh_candidates && !append_raw && config_->wubi_auto_commit &&
            (mode_ == InputMode::WUBI || mode_ == InputMode::MIXED) &&
            result == ProcessResult::ACCEPTED) {
            if (context_.pinyin_buffer.size() == 4 &&
                context_.candidates.candidates.size() == 1 &&
                context_.candidates.candidates[0].source == CandidateSource::kWubi) {
                committed_code_override = context_.pinyin_buffer;
                committed_candidate_override = context_.candidates.candidates[0];
                has_committed_candidate_override = true;
                context_.commit_candidate(0);
                context_.clear_preedit();
                context_.candidates = {};
                result = ProcessResult::COMMITTED;
            }
        }
    }
    if (commit_wubi_before_next_code && result == ProcessResult::ACCEPTED &&
        context_.is_composing()) {
        context_.committed_text = committed_candidate_override.text;
        context_.set_commit_source(CommitSource::kCandidate);
        commit_continues_composition_ = true;
        result = ProcessResult::COMMITTED;
    }
    if (trace_enabled_) {
        t2 = std::chrono::steady_clock::now();
        trace_.translate_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    }

    CXXIME_LOG(L"Engine::process_key: result=%d, buf='%S'",
               (int)result, context_.pinyin_buffer.c_str());

    // Record an explicit candidate selection; raw text and composed candidates are excluded.
    if (config_->candidate_learning &&
        result == ProcessResult::COMMITTED && !context_.committed_text.empty() &&
        context_.commit_source() == CommitSource::kCandidate) {
        const std::string typed_code = has_committed_candidate_override
            ? committed_code_override
            : context_.committed_candidate_code();
        const Candidate* committed_candidate = has_committed_candidate_override
            ? &committed_candidate_override
            : context_.committed_candidate();
        if (committed_candidate) {
            const bool is_wubi = committed_candidate->source == CandidateSource::kWubi;
            record_candidate_preference(is_wubi ? wubi_dict_ : pinyin_dict_,
                                        committed_candidate, typed_code);
        }
    }

    // Finalize the query trace.
    if (trace_enabled_) {
        auto total_end = std::chrono::steady_clock::now();
        trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
    }

    return result;
}

void Engine::commit_with_punctuation(Context& context, const std::string& output) {
    const int selectable_count = context.selectable_candidate_count();
    if (context.is_composing() && selectable_count > 0) {
        int index = context.candidates.highlighted;
        if (index < 0 || index >= selectable_count) {
            index = 0;
        }
        context.commit_candidate(index);
        context.committed_text += output;
        context.set_commit_source(CommitSource::kCandidate);
    } else if (context.is_composing()) {
        context.committed_text = context.pinyin_buffer + output;
        context.set_commit_source(CommitSource::kRawCodePreserveCase);
    } else {
        context.committed_text = output;
        context.set_commit_source(CommitSource::kRawCodePretransformed);
    }
    context.clear_preedit();
    context.candidates = {};
    context.reset_pagination();
    context.last_committed_char = output.back();
}

bool Engine::handle_punctuation(const KeyEvent& event, Context& context,
                                const OutputOptions& opts) {
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
    const std::optional<char> normalized = normalize_ascii_key(event);
    if (!normalized)
        return false;
    const char ch = *normalized;

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
    commit_with_punctuation(context, output);
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
    const std::optional<char> normalized = normalize_ascii_key(event);
    if (!normalized)
        return false;
    const char ch = *normalized;

    // Step 2: range check
    if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7e)
        return false;

    // Step 3: full-width conversion
    std::string output = OutputComposer::to_full_width(ch);

    // Step 4: commit
    commit_with_punctuation(context, output);
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

    context_.commit_candidate(index);

    if (config_->candidate_learning &&
        context_.candidates.candidates[index].source != CandidateSource::kSymbol) {
        auto& cand = context_.candidates.candidates[index];
        const std::string code = context_.committed_candidate_code();
        bool is_wubi = mode_ == InputMode::WUBI ||
                       (mode_ == InputMode::MIXED && cand.source == CandidateSource::kWubi);
        Dict* active_dict = is_wubi ? wubi_dict_ : pinyin_dict_;
        record_candidate_preference(active_dict, &cand, code);
    }

    return true;
}

std::string Engine::get_commit_text() {
    std::string text = context_.committed_text;
    reset_composition_state();
    return text;
}

std::pair<std::string, CommitSource> Engine::take_commit_text_with_source() {
    auto result = std::make_pair(std::move(context_.committed_text), context_.commit_source());
    if (commit_continues_composition_) {
        context_.committed_text.clear();
        context_.set_commit_source(CommitSource::kRawCode);
        commit_continues_composition_ = false;
    } else {
        reset_composition_state();
    }
    return result;
}

std::pair<std::string, CommitSource> Engine::commit_composition_with_source() {
    auto result = context_.commit_with_source();
    ascii_composer_.finish_temporary_ascii();
    commit_continues_composition_ = false;
    return result;
}

std::string Engine::commit_raw_composition() {
    std::string raw = std::move(context_.pinyin_buffer);
    reset_composition_state();
    return raw;
}

void Engine::clear() {
    reset_composition_state();
    handled_shortcut_key_ = 0;
}

void Engine::clear_composition() {
    reset_composition_state();
    handled_shortcut_key_ = 0;
}

void Engine::reset_composition_state() {
    context_.reset();
    ascii_composer_.finish_temporary_ascii();
    commit_continues_composition_ = false;
}

CandidatePage Engine::translate_current_composition(const QueryDeadline& deadline) {
    if (SymbolProcessor::is_active(context_)) {
        return translate_symbol_page();
    }

    QueryBudget effective_budget =
        make_budget(static_cast<int>(context_.pinyin_buffer.size()), config_->page_size);
    effective_budget.deadline = deadline;
    CandidatePage page = translator_->translate(
        context_.pinyin_buffer, context_.page_index, config_->page_size,
        trace_enabled_ ? &trace_ : nullptr, &effective_budget, &scratch_, context_.page_offset);
    if (config_->wubi_code_hint) {
        add_wubi_code_hints(context_.pinyin_buffer, page);
    }
    return page;
}

void Engine::set_sentence_composition_enabled(bool enabled) {
    sentence_composition_enabled_ = enabled;
    if (translator_) {
        translator_->set_sentence_composition_enabled(enabled);
    }
}

void Engine::clear_query_cache() {
    if (translator_) {
        translator_->clear_query_cache();
    }
}

void Engine::set_wubi_dict(Dict* dict) {
    wubi_dict_ = dict;
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

    reset_composition_state();

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
        mixed_trans->set_candidate_preference(config_->mixed_candidate_preference);
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
    translator_->set_sentence_composition_enabled(sentence_composition_enabled_);
    translator_->set_candidate_learning_enabled(config_->candidate_learning);
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
