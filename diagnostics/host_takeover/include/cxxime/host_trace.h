// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TRACE_H_
#define CXXIME_HOST_TRACE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include <guiddef.h>
#include <json.hpp>

namespace cxxime {

inline constexpr int kHostTraceSchemaVersion = 3;

const char* host_trace_product_version();
const char* host_trace_arch();

uint64_t host_trace_next_id();
uint64_t host_trace_input_id(uint32_t key_code, intptr_t key_data);

class HostTraceSession {
public:
    void begin_input(uint32_t key_code, intptr_t key_data) {
        input_id_ = host_trace_input_id(key_code, key_data);
    }

    uint64_t input_id() const { return input_id_; }
    uint64_t composition_id() const { return composition_id_; }

    uint64_t ensure_composition() {
        if (composition_id_ == 0) {
            composition_id_ = host_trace_next_id();
        }
        return composition_id_;
    }

    void reset_composition() { composition_id_ = 0; }

private:
    uint64_t input_id_ = 0;
    uint64_t composition_id_ = 0;
};

std::string host_trace_guid(REFGUID guid);
std::string host_trace_digest_utf16(const wchar_t* text, size_t length);
std::string host_trace_digest_utf16(const std::wstring& text);

void write_host_trace(const char* component,
                      const char* event,
                      nlohmann::json fields = nlohmann::json::object());

} // namespace cxxime

#endif // CXXIME_HOST_TRACE_H_
