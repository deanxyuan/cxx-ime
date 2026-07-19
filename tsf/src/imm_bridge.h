// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_IMM_BRIDGE_H_
#define CXXIME_TSF_IMM_BRIDGE_H_

#include "pch.h"

#include <string>

namespace cxxime_tsf {

class ImmBridge {
public:
    bool update_preedit(const std::wstring& preedit);
    bool commit_text(const std::wstring& text);
    void clear();

    const char* last_error() const { return _lastError; }

private:
    void set_error(const char* error) { _lastError = error; }

    bool _composing = false;
    HWND _hwnd = nullptr;
    const char* _lastError = nullptr;
};

} // namespace cxxime_tsf

#endif // CXXIME_TSF_IMM_BRIDGE_H_
