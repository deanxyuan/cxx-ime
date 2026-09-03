// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_UTIL_TOPN_TEST_DATA_H_
#define CXXIME_TEST_UTIL_TOPN_TEST_DATA_H_

#include <string>
#include <utility>
#include <vector>

#include <cxxime/candidate.h>

namespace cxxime::test {

bool create_test_topn(
    const std::string& path,
    const std::vector<std::pair<std::string, std::vector<Candidate>>>& entries,
    bool prefix_complete = true);

} // namespace cxxime::test

#endif // CXXIME_TEST_UTIL_TOPN_TEST_DATA_H_
