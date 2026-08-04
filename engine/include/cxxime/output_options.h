// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_OUTPUT_OPTIONS_H_
#define CXXIME_OUTPUT_OPTIONS_H_

#include <cxxime/ipc_protocol.h>
#include <cxxime/punct_types.h>

namespace cxxime {

// Commit source: determines whether transform() applies conversions.
enum class CommitSource {
    kRawCode,             // Raw pinyin, ASCII passthrough, intercept_key — apply Caps Lock + full-width
    kCandidate,           // Candidate text — no conversion, preserve original text
    kRawCodePreserveCase, // Raw code with correct case, skip Caps Lock inversion
    kRawCodePretransformed, // Engine already applied shape/case conversion
};

// Effective output state derived from ImeStatus.
// Constructed on the stack, used once, then discarded.
// Distinguishes "raw preference" (ImeStatus) from "effective state" (OutputOptions).
struct OutputOptions {
    bool chinese_mode = true;
    bool caps_lock = false;
    bool full_shape = false;
    bool chinese_punct = true;  // Effective Chinese punctuation (not raw preference)
    const PunctMapping* punct_mapping = nullptr;  // Punctuation mapping table (read-only)

    static OutputOptions from(const ImeStatus& status) {
        OutputOptions opts;
        opts.chinese_mode = status.chinese_mode();
        opts.caps_lock = status.caps_lock();
        opts.full_shape = status.full_shape();
        // Effective Chinese punct = chinese_mode && !caps_lock && raw preference
        opts.chinese_punct = status.chinese_mode()
                          && !status.caps_lock()
                          && status.chinese_punct();
        return opts;
    }
};

}  // namespace cxxime

#endif  // CXXIME_OUTPUT_OPTIONS_H_
