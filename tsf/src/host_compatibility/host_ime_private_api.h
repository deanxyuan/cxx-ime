// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_HOST_COMPATIBILITY_HOST_IME_PRIVATE_API_H_
#define CXXIME_TSF_HOST_COMPATIBILITY_HOST_IME_PRIVATE_API_H_

#include <cstdint>

namespace cxxime_tsf {

struct HostImePrivateApiRequest {
    uintptr_t imemanager_base = 0;
    uintptr_t manager = 0;
    uintptr_t names_manager = 0;
    bool module_verified = false;
    bool manager_initialized_field_read = false;
    bool manager_initialized_field = false;
    bool manager_enabled_field_read = false;
    bool manager_enabled_field = false;
    bool classification_field_read = false;
    uint32_t classification_field = 0;
};

struct HostImePrivateApiSnapshot {
    bool architecture_supported = false;
    bool manager_vtable_read = false;
    bool manager_initialized_method_read = false;
    bool manager_enabled_method_read = false;
    bool names_manager_vtable_read = false;
    bool classification_method_read = false;
    uintptr_t manager_vtable_rva = 0;
    uintptr_t manager_initialized_method_rva = 0;
    uintptr_t manager_enabled_method_rva = 0;
    uintptr_t names_manager_vtable_rva = 0;
    uintptr_t classification_method_rva = 0;
    bool manager_verified = false;
    bool classification_verified = false;
    bool verified = false;
    bool manager_called = false;
    bool classification_called = false;
    bool manager_initialized = false;
    bool manager_enabled = false;
    uint32_t classification = 0;
    const char* result = "architecture_unsupported";
};

HostImePrivateApiSnapshot inspect_host_ime_private_api(
    const HostImePrivateApiRequest& request);

} // namespace cxxime_tsf

#endif // CXXIME_TSF_HOST_COMPATIBILITY_HOST_IME_PRIVATE_API_H_
