// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_HOST_COMPATIBILITY_HOST_CLASSIFICATION_COMPATIBILITY_H_
#define CXXIME_TSF_HOST_COMPATIBILITY_HOST_CLASSIFICATION_COMPATIBILITY_H_

#include <windows.h>

#include <cstdint>

namespace cxxime_tsf {

struct HostClassificationCompatibilitySnapshot {
    const char* action = "inactive";
    const char* result = "inactive";
    bool active = false;
    bool process_name_read = false;
    char process_name[64] = {};
    bool process_matches = false;
    bool architecture_supported = false;
    bool inputsystem_loaded = false;
    bool imemanager_loaded = false;
    DWORD inputsystem_timestamp = 0;
    DWORD inputsystem_image_size = 0;
    DWORD imemanager_timestamp = 0;
    DWORD imemanager_image_size = 0;
    bool inputsystem_identity_matches = false;
    bool imemanager_identity_matches = false;
    bool manager_interface_ready = false;
    bool manager_gate_ready = false;
    bool window_gate_ready = false;
    bool private_api_verified = false;
    bool candidate_methods_match = false;
    bool runtime_verified = false;
    uintptr_t manager = 0;
    uintptr_t active_hwnd = 0;
    uintptr_t auxiliary_input_handler = 0;
    uint32_t input_source_code = 0;
    uint32_t profile_code = 0;
    uint32_t effective_classification_code = 0;
    bool classification_available = false;
    bool attempted = false;
    bool write_succeeded = false;
    bool readback_succeeded = false;
    uint32_t requested_profile_code = 0;
    uint32_t readback_profile_code = 0;
    DWORD win32_error = ERROR_SUCCESS;
    bool classification_ready = false;

    bool restore_attempted = false;
    bool restore_succeeded = false;
    bool restore_readback_succeeded = false;
    bool restore_verified = false;
    uint32_t original_profile_code = 0;
    uint32_t current_profile_code = 0;
    uint32_t restored_profile_code = 0;
};

void activate_host_classification_compatibility();
HostClassificationCompatibilitySnapshot prepare_host_classification_compatibility();
HostClassificationCompatibilitySnapshot deactivate_host_classification_compatibility();
const HostClassificationCompatibilitySnapshot& host_classification_compatibility_snapshot();

} // namespace cxxime_tsf

#endif // CXXIME_TSF_HOST_COMPATIBILITY_HOST_CLASSIFICATION_COMPATIBILITY_H_
