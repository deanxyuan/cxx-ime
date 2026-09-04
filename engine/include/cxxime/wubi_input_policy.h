// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WUBI_INPUT_POLICY_H_
#define CXXIME_WUBI_INPUT_POLICY_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include <cxxime/candidate_selection.h>
#include <cxxime/config.h>
#include <cxxime/translation_result.h>

namespace cxxime {

enum class WubiFifthKeyAction {
    kNone,
    kCommitFirstAndRestart,
    kRestartAfterMiss,
};

class WubiInputPolicy {
public:
    static WubiFifthKeyAction fifth_key_action(CompositionScheme scheme,
                                               const std::string& active_input,
                                               std::size_t cursor,
                                               const TranslationResult& result,
                                               const Config& config,
                                               uint32_t keycode);

    static bool should_auto_commit(CompositionScheme scheme,
                                   const std::string& active_input,
                                   const TranslationResult& result,
                                   const Config& config);
};

} // namespace cxxime

#endif // CXXIME_WUBI_INPUT_POLICY_H_
