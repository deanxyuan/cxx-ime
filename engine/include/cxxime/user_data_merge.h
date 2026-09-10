// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_DATA_MERGE_H_
#define CXXIME_USER_DATA_MERGE_H_

#include <cstddef>
#include <string>

namespace cxxime {

struct UserDataMergeResult {
    std::string contents;
    std::size_t imported_count = 0;
    std::size_t skipped_count = 0;
};

bool merge_user_data_contents(const std::string& file_name, const std::string& current,
                              const std::string& imported, UserDataMergeResult* result);

} // namespace cxxime

#endif // CXXIME_USER_DATA_MERGE_H_
