// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TRANSLATION_POLICY_H_
#define CXXIME_TRANSLATION_POLICY_H_

namespace cxxime {

struct TranslationPolicy {
    // False preserves the 0.4.0 full-span candidate path. The server enables this
    // only after a future segmented-selection capability is negotiated.
    bool allow_partial_selection = false;
};

} // namespace cxxime

#endif // CXXIME_TRANSLATION_POLICY_H_
