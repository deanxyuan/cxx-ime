// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_COMPOSITION_TERMINATION_H_
#define CXXIME_TSF_COMPOSITION_TERMINATION_H_

#include <string>

namespace cxxime_tsf {

enum class HostTerminationTextResult {
    kReplaced,
    kUnchanged,
    kReadFailed,
    kWriteFailed,
};

template <typename ReadText, typename WriteText>
HostTerminationTextResult
normalize_host_termination_text(const std::wstring& expected, const std::wstring& replacement,
                                ReadText&& read_text, WriteText&& write_text) {
    std::wstring current;
    if (!read_text(&current)) {
        return HostTerminationTextResult::kReadFailed;
    }
    if (current != expected) {
        return HostTerminationTextResult::kUnchanged;
    }
    return write_text(replacement) ? HostTerminationTextResult::kReplaced
                                   : HostTerminationTextResult::kWriteFailed;
}

} // namespace cxxime_tsf

#endif // CXXIME_TSF_COMPOSITION_TERMINATION_H_
