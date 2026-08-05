// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SYMBOL_PROCESSOR_H_
#define CXXIME_SYMBOL_PROCESSOR_H_

#include <cxxime/context.h>
#include <cxxime/key_event.h>
#include <cxxime/processor.h>

namespace cxxime {

class SymbolProcessor {
public:
    static bool is_active(const Context& context);
    static bool is_trigger(const KeyEvent& event);

    ProcessResult process_key(const KeyEvent& event, Context& context,
                              bool allow_trigger) const;

private:
    static bool edit_preedit(const KeyEvent& event, Context& context);
    static ProcessResult select_candidate(Context& context, int index);
};

} // namespace cxxime

#endif // CXXIME_SYMBOL_PROCESSOR_H_
