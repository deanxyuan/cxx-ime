// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_ENGINE_RESPONSE_H_
#define CXXIME_TSF_ENGINE_RESPONSE_H_

#include <cstddef>
#include <string>

#include <cxxime/candidate_presentation.h>
#include <cxxime/ipc_protocol.h>

namespace cxxime_tsf {

struct DecodedEnginePresentation {
    std::wstring preedit;
    std::size_t preedit_cursor_utf16 = 0;
    std::size_t converted_prefix_utf16 = 0;
    cxxime::CandidatePresentationPage candidates;
};

bool decode_engine_presentation(const cxxime::IPCResponse& response,
                                DecodedEnginePresentation* presentation);
bool decode_engine_commit_text(const cxxime::IPCResponse& response, std::wstring* commit_text);

} // namespace cxxime_tsf

#endif // CXXIME_TSF_ENGINE_RESPONSE_H_
