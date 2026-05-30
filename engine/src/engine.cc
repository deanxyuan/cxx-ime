// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/engine.h>
#include <windows.h>
#include <chrono>
#include <cxxime/logging.h>

namespace cxxime {

// Static member for global query ID generation
uint64_t Engine::next_query_id_ = 0;

// Deterministic sampling for release builds (1% rate)
static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static bool should_sample(uint64_t session_id, uint64_t revision) {
    uint64_t h = mix64((session_id << 32) ^ revision);
    return (h % 100) == 0; // 1%
}

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
    dict_ = &dict;
    spellings_ = &spellings;
    syllabifier_ = syllabifier;
    config_ = &config;

    translator_.set_dict(dict_);
    if (syllabifier_) {
        translator_.set_syllabifier(syllabifier_);
    }
    if (dict_->has_short_cache()) {
        translator_.set_short_cache(&dict_->short_cache());
    }

    init_per_session(config);
    return true;
}

void Engine::init_per_session(const Config& config) {
    ascii_composer_.load_config(config);
}

void Engine::finalize() {
    if (dict_ == &owned_dict_) {
        owned_dict_.close();
    }
    context_.reset();
}

ProcessResult Engine::process_key(const KeyEvent& event) {
    CXXIME_LOG(L"Engine::process_key: vk=%u, is_key_up=%d, composing=%d",
               event.keycode, event.is_key_up, context_.is_composing());

    // Initialize trace for this query (only if tracing enabled)
    // Preserve session_id/revision set by caller (server) before this call.
    std::chrono::steady_clock::time_point total_start;
    if (trace_enabled_) {
        uint32_t saved_session_id = trace_.session_id;
        uint64_t saved_revision = trace_.revision;
        trace_ = QueryTrace{};
        trace_.query_id = next_query_id_++;
        trace_.session_id = saved_session_id;
        trace_.revision = saved_revision;
        total_start = std::chrono::steady_clock::now();
    }

    // Create per-query deadline (Phase 3: QueryDeadline with expires_at)
    QueryDeadline per_query_deadline = QueryDeadline::from_now(query_deadline_ms_);

    // Let AsciiComposer track modifier key state (may toggle ascii_mode)
    ascii_composer_.process_key(event.keycode, event.is_key_up, context_);

    CXXIME_LOG(L"Engine::process_key: after ascii_composer, committed_text='%S'", context_.committed_text.c_str());

    // Check if AsciiComposer committed text (e.g. Shift toggle with commit_text)
    if (!context_.committed_text.empty()) {
        if (trace_enabled_) {
            auto total_end = std::chrono::steady_clock::now();
            trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
        }
        return ProcessResult::COMMITTED;
    }

    // If in ASCII mode, handle letters/space directly
    if (ascii_composer_.is_ascii_mode() && !event.is_key_up) {
        uint32_t vk = event.keycode;

        // Letter keys (A-Z): commit as single ASCII char
        if (vk >= 'A' && vk <= 'Z') {
            context_.committed_text = std::string(1, static_cast<char>(vk - 'A' + 'a'));
            if (ascii_composer_.is_temporary_ascii()) {
                ascii_composer_.set_ascii_mode(false);
            }
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::COMMITTED;
        }

        // Space: commit a space
        if (vk == 0x20) {  // VK_SPACE
            context_.committed_text = " ";
            if (trace_enabled_) {
                auto total_end = std::chrono::steady_clock::now();
                trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            }
            return ProcessResult::COMMITTED;
        }

        // Enter: commit pending text and newline
        if (vk == 0x0D) {  // VK_RETURN
            context_.committed_text = "\r\n";
            if (ascii_composer_.is_temporary_ascii()) {
                ascii_composer_.set_ascii_mode(false);
            }
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

    std::chrono::steady_clock::time_point t0, t1, t2;
    if (trace_enabled_) {
        t0 = std::chrono::steady_clock::now();
    }
    auto result = processor_.process_key(event, context_);
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

    // Auto-restore from temporary inline_ascii when composition ends
    if (result == ProcessResult::COMMITTED && ascii_composer_.is_temporary_ascii()) {
        ascii_composer_.set_ascii_mode(false);
    }

    // After processing, update candidates if still composing
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
            auto page = translator_.translate(context_.pinyin_buffer, context_.page_index, config_->page_size,
                                              trace_enabled_ ? &trace_ : nullptr, &effective_budget);
            context_.update_candidates(std::move(page));
        }
        if (trace_enabled_) {
            trace_.candidate_count = (int)context_.candidates.candidates.size();
        }
    }
    if (trace_enabled_) {
        t2 = std::chrono::steady_clock::now();
        trace_.translate_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    }

    CXXIME_LOG(L"Engine::process_key: result=%d, buf='%S'",
               (int)result, context_.pinyin_buffer.c_str());

    // If committed, update user frequency and recent cache
    if (result == ProcessResult::COMMITTED && !context_.committed_text.empty()) {
        std::string code = context_.pinyin_buffer;
        if (code.empty()) {
            code = dict_->reverse_lookup(context_.committed_text);
        }
        dict_->update_frequency(context_.committed_text, code);
        // Phase 4: update session recent cache for short input fast path
        if (!context_.pinyin_buffer.empty()) {
            Candidate c;
            c.text = context_.committed_text;
            c.frequency = 0;
            translator_.update_recent(context_.pinyin_buffer, c);
        }
    }

    // Finalize trace
    if (trace_enabled_) {
        auto total_end = std::chrono::steady_clock::now();
        trace_.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();

        // Only log slow queries and sampled queries (async, non-blocking)
        if (trace_.should_log()) {
            trace_.log();
        }
    }

    return result;
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

    std::string code = context_.pinyin_buffer;
    if (code.empty())
        code = dict_->reverse_lookup(context_.committed_text);
    if (!code.empty())
        dict_->update_frequency(context_.committed_text, code);

    // Phase 4: update session recent cache for short input fast path
    if (!context_.pinyin_buffer.empty()) {
        translator_.update_recent(context_.pinyin_buffer,
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
    return text;
}

void Engine::clear() {
    context_.reset();
    translator_.clear_recent();
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
