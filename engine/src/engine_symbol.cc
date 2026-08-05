// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/engine.h>

#include <cxxime/symbol_table.h>

namespace cxxime {

bool Engine::symbol_input_enabled(const OutputOptions& opts) const {
    return symbol_table_ &&
           !symbol_table_->empty() &&
           opts.chinese_mode &&
           opts.chinese_punct;
}

bool Engine::start_symbol_input_after_commit(std::string& committed_code,
                                             Candidate& committed_candidate,
                                             bool& has_committed_candidate) {
    if (!context_.is_composing() || SymbolProcessor::is_active(context_)) {
        return false;
    }

    committed_code = context_.pinyin_buffer;
    const int highlighted = context_.candidates.highlighted;
    if (highlighted >= 0 &&
        highlighted < static_cast<int>(context_.candidates.candidates.size())) {
        committed_candidate = context_.candidates.candidates[highlighted];
        has_committed_candidate = true;
        context_.committed_text = committed_candidate.text;
        context_.set_commit_source(CommitSource::kCandidate);
    } else {
        context_.committed_text = context_.pinyin_buffer;
        context_.set_commit_source(CommitSource::kRawCodePreserveCase);
    }

    context_.clear_preedit();
    context_.candidates = {};
    context_.reset_pagination();
    context_.set_preedit("/");
    return true;
}

CandidatePage Engine::translate_symbol_page() const {
    return symbol_table_->translate(context_.pinyin_buffer.substr(1), context_.page_index,
                                    config_->page_size, context_.page_offset);
}

} // namespace cxxime
