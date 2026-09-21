// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/pinyin_scheme.h>

#include <algorithm>

namespace cxxime {
namespace {

const std::vector<PinyinSchemeDescriptor> kPinyinSchemes = {
    {"full_pinyin", L"全拼", PinyinSchemeKind::kFullPinyin, "pinyin_spellings",
     "pinyin.spellings.bin", L"ni'hao", "wu'zong", "ji'shu"},
    {"microsoft_shuangpin", L"微软双拼", PinyinSchemeKind::kShuangpin,
     "pinyin_spellings_microsoft_shuangpin", "pinyin.microsoft-shuangpin.spellings.bin", L"ni'hk",
     "wu'zs", "ji'uu"},
};

} // namespace

const std::vector<PinyinSchemeDescriptor>& built_in_pinyin_schemes() { return kPinyinSchemes; }

const PinyinSchemeDescriptor* find_pinyin_scheme(std::string_view id) {
    const auto found = std::find_if(kPinyinSchemes.begin(), kPinyinSchemes.end(),
                                    [id](const auto& scheme) { return id == scheme.id; });
    return found == kPinyinSchemes.end() ? nullptr : &*found;
}

const PinyinSchemeDescriptor& default_pinyin_scheme() { return kPinyinSchemes.front(); }

const PinyinSchemeDescriptor& resolve_pinyin_scheme(std::string_view id) {
    const PinyinSchemeDescriptor* scheme = find_pinyin_scheme(id);
    return scheme ? *scheme : default_pinyin_scheme();
}

std::string normalize_pinyin_scheme_id(std::string_view id) { return resolve_pinyin_scheme(id).id; }

} // namespace cxxime
