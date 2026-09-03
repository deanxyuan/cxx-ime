// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_SUPPORT_IPC_BASELINE_FIXTURE_H_
#define CXXIME_TEST_SUPPORT_IPC_BASELINE_FIXTURE_H_

#include <array>
#include <cstdint>
#include <cstring>

#include <cxxime/ipc_protocol.h>

namespace cxxime::test {

// The 0.4.0 wire prefixes are the permanent append-only compatibility baseline.
using MainRequestBaseline = std::array<std::uint8_t, 576>;
using MainResponseBaseline = std::array<std::uint8_t, 3176>;

inline MainRequestBaseline make_main_request_baseline(const IPCRequest& request) {
    MainRequestBaseline payload = {};
    std::memcpy(payload.data(), &request, payload.size());
    return payload;
}

inline MainResponseBaseline make_main_response_baseline(const IPCResponse& response) {
    MainResponseBaseline payload = {};
    std::memcpy(payload.data(), &response, payload.size());
    return payload;
}

} // namespace cxxime::test

#endif // CXXIME_TEST_SUPPORT_IPC_BASELINE_FIXTURE_H_
