// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_PROCESSOR_H_
#define CXXIME_PROCESSOR_H_

#include <cxxime/key_event.h>
#include <cxxime/context.h>

namespace cxxime {

enum class ProcessResult {
    ACCEPTED,
    REJECTED,
    COMMITTED,
};

class PinyinProcessor {
public:
    ProcessResult process_key(const KeyEvent& event, Context& context);
};

} // namespace cxxime

#endif // CXXIME_PROCESSOR_H_
