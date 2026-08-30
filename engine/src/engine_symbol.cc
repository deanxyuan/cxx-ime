// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/engine.h>

#include <cxxime/symbol_table.h>

namespace cxxime {

bool Engine::symbol_input_enabled(const OutputOptions& opts) const {
    return symbol_table_ &&
           !symbol_table_->empty() &&
           opts.chinese_mode &&
           opts.chinese_punct &&
           !opts.full_shape;
}

CandidatePage Engine::translate_symbol_page() const {
    return symbol_table_->translate(context_.pinyin_buffer.substr(1), context_.page_index,
                                    config_->page_size, context_.page_offset);
}

} // namespace cxxime
