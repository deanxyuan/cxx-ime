// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_SCHEME_H_
#define CXXIME_PINYIN_SCHEME_H_

#include <string>
#include <string_view>
#include <vector>

namespace cxxime {

enum class PinyinSchemeKind {
    kFullPinyin,
    kShuangpin,
};

struct PinyinSchemeDescriptor {
    const char* id;
    const wchar_t* display_name;
    PinyinSchemeKind kind;
    const char* manifest_role;
    const char* spelling_filename;
    const wchar_t* nihao_preedit_example;
    const char* wuzong_preedit_example;
    const char* jishu_preedit_example;
};

const std::vector<PinyinSchemeDescriptor>& built_in_pinyin_schemes();
const PinyinSchemeDescriptor* find_pinyin_scheme(std::string_view id);
const PinyinSchemeDescriptor& default_pinyin_scheme();
const PinyinSchemeDescriptor& resolve_pinyin_scheme(std::string_view id);
std::string normalize_pinyin_scheme_id(std::string_view id);

} // namespace cxxime

#endif // CXXIME_PINYIN_SCHEME_H_
