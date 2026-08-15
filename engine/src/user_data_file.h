// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ENGINE_USER_DATA_FILE_H_
#define CXXIME_ENGINE_USER_DATA_FILE_H_

#include <string>

namespace cxxime {

bool read_user_data_file(const std::string& path, std::string* contents);
bool write_user_data_file_atomically(const std::string& path, const std::string& contents);

} // namespace cxxime

#endif // CXXIME_ENGINE_USER_DATA_FILE_H_
