// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_TSF_PCH_H_
#define CXXIME_TSF_PCH_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <combaseapi.h>
#include <wrl.h>

#include <string>
#include <memory>

template <typename T>
using com_ptr = Microsoft::WRL::ComPtr<T>;

#endif // CXXIME_TSF_PCH_H_
