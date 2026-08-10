// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_HOST_IME_PRIVATE_API_H_
#define CXXIME_HOST_TAKEOVER_TSF_HOST_IME_PRIVATE_API_H_

#include "host_compatibility/host_ime_private_api.h"

#include <cxxime/host_trace.h>

namespace cxxime_tsf {

void add_host_ime_private_api_fields(
    nlohmann::json& fields,
    const HostImePrivateApiRequest& request,
    const HostImePrivateApiSnapshot& snapshot);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_HOST_IME_PRIVATE_API_H_
