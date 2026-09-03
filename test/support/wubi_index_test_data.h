// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_UTIL_WUBI_INDEX_TEST_DATA_H_
#define CXXIME_TEST_UTIL_WUBI_INDEX_TEST_DATA_H_

#include <string>
#include <tuple>
#include <vector>

namespace cxxime::test {

bool create_test_wubi_index(const std::string& path,
                            const std::vector<std::tuple<std::string, std::string, int>>& entries);

} // namespace cxxime::test

#endif // CXXIME_TEST_UTIL_WUBI_INDEX_TEST_DATA_H_
