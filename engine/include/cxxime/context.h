// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONTEXT_H_
#define CXXIME_CONTEXT_H_

#include <string>
#include <utility>
#include <cxxime/candidate.h>
#include <cxxime/output_options.h>

namespace cxxime {

class Context {
public:
    std::string pinyin_buffer;
    CandidatePage candidates;
    std::string committed_text;
    int page_index = 0;

    bool is_composing() const;
    void reset();
    std::string commit();
    void update_candidates(CandidatePage&& page);

    void set_commit_source(CommitSource s) { commit_source_ = s; }
    CommitSource commit_source() const { return commit_source_; }

    // 一次性取走 committed_text 和 commit_source，清空内部状态
    std::pair<std::string, CommitSource> commit_with_source();

private:
    CommitSource commit_source_ = CommitSource::kRawCode;
};

} // namespace cxxime

#endif // CXXIME_CONTEXT_H_
