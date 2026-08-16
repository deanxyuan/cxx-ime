// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_DICT_VALIDATION_H_
#define CXXIME_USER_DICT_VALIDATION_H_

#include <cstdint>
#include <string>

namespace cxxime {

inline constexpr std::uint64_t kMaxUserDictImportBytes = 64ULL * 1024ULL * 1024ULL;

bool is_valid_user_dict_text(const std::string& text);
bool is_valid_user_dict_code(const std::string& code);
bool is_valid_user_dict_syllables(const std::string& syllables);
bool is_valid_user_dict_entry(const std::string& text, const std::string& code,
                              const std::string& syllables = {});

} // namespace cxxime

#endif // CXXIME_USER_DICT_VALIDATION_H_
