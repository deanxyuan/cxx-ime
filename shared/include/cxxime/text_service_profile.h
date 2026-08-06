// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEXT_SERVICE_PROFILE_H_
#define CXXIME_TEXT_SERVICE_PROFILE_H_

#include <windows.h>

namespace cxxime {

inline constexpr CLSID kTextServiceClsid = {
    0xb7e1e5a2,
    0x8f3d,
    0x4a9c,
    {0xb6, 0xe7, 0x2c, 0x4d, 0x8f, 0x1a, 0x3b, 0x5e},
};

inline constexpr GUID kTextServiceProfileGuid = {
    0xd4f2c7a1,
    0x9e6b,
    0x4d8a,
    {0xa3, 0xf5, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60},
};

inline constexpr LANGID kTextServiceLanguageId =
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

} // namespace cxxime

#endif // CXXIME_TEXT_SERVICE_PROFILE_H_
