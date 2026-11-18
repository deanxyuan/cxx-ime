// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_ORDER_H_
#define CXXIME_CANDIDATE_ORDER_H_

#include <cxxime/translation_result.h>

namespace cxxime {

// Arrange an unsliced source prefix or resumable window after action arbitration.
// Any custom phrases are already a leading, deduplicated block.
// Published entries remain fixed; only the new tail participates in ordering.
TranslationResult finish_candidate_order(const TranslationRequest& request,
                                         TranslationResult result);

} // namespace cxxime

#endif // CXXIME_CANDIDATE_ORDER_H_
