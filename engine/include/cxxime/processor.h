// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PROCESSOR_H_
#define CXXIME_PROCESSOR_H_

#include <cxxime/key_event.h>
#include <cxxime/context.h>

namespace cxxime {

enum class ProcessResult {
    ACCEPTED,
    REJECTED,
    COMMITTED,
    TOGGLE_SHAPE,   // full/half shape toggle (Shift+Space)
    TOGGLE_PUNCT,   // Chinese/English punctuation toggle (Ctrl+.)
};

// Abstract processor interface
class IProcessor {
public:
    virtual ~IProcessor() = default;
    virtual ProcessResult process_key(const KeyEvent& event, Context& context) = 0;
};

// Pinyin processor implementation
class PinyinProcessor : public IProcessor {
public:
    ProcessResult process_key(const KeyEvent& event, Context& context) override;
};

} // namespace cxxime

#endif // CXXIME_PROCESSOR_H_
