// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_CONTEXT_H_
#define CXXIME_CONTEXT_H_

#include <string>
#include <cxxime/candidate.h>

namespace cxxime {

class Context {
public:
    std::string pinyin_buffer;
    CandidatePage candidates;
    std::string committed_text;

    bool is_composing() const;
    void reset();
    std::string commit();
    void update_candidates(CandidatePage&& page);
};

} // namespace cxxime

#endif // CXXIME_CONTEXT_H_
