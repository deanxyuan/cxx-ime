// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_IMM_BRIDGE_H_
#define CXXIME_TSF_IMM_BRIDGE_H_

#include "pch.h"

#include <string>

namespace cxxime_tsf {

class ImmBridge {
public:
    bool update_preedit(const std::wstring& preedit,
                        uint64_t input_id = 0,
                        uint64_t composition_id = 0);
    bool commit_text(const std::wstring& text,
                     uint64_t input_id = 0,
                     uint64_t composition_id = 0);
    void clear(uint64_t input_id = 0, uint64_t composition_id = 0);

    const char* last_error() const { return _lastError; }

private:
    void set_error(const char* error) { _lastError = error; }

    bool _composing = false;
    HWND _hwnd = nullptr;
    const char* _lastError = nullptr;
};

} // namespace cxxime_tsf

#endif // CXXIME_TSF_IMM_BRIDGE_H_
