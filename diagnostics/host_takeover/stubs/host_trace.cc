// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/host_trace.h>

namespace cxxime {

const char* host_trace_build_id() {
    return "";
}

const char* host_trace_arch() {
#ifdef _WIN64
    return "x64";
#else
    return "x86";
#endif
}

uint64_t host_trace_next_id() {
    return 0;
}

uint64_t host_trace_input_id(uint32_t, intptr_t) {
    return 0;
}

std::string host_trace_guid(REFGUID) {
    return {};
}

std::string host_trace_digest_utf16(const wchar_t*, size_t) {
    return {};
}

std::string host_trace_digest_utf16(const std::wstring&) {
    return {};
}

void write_host_trace(const char*, const char*, nlohmann::json) {}

} // namespace cxxime
