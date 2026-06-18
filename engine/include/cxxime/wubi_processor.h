// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WUBI_PROCESSOR_H_
#define CXXIME_WUBI_PROCESSOR_H_

#include <cxxime/processor.h>

namespace cxxime {

class WubiProcessor : public IProcessor {
public:
    ProcessResult process_key(const KeyEvent& event, Context& context) override;
};

} // namespace cxxime

#endif // CXXIME_WUBI_PROCESSOR_H_
