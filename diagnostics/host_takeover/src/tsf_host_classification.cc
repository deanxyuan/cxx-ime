// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_classification.h"

#include <cxxime/stage_trace.h>

namespace cxxime_tsf {

void trace_stage_host_classification_compatibility(
    const HostClassificationCompatibilitySnapshot& snapshot) {
    cxxime::write_stage_trace("tsf", "host.classification_compatibility", {
        {"action", snapshot.action},
        {"result", snapshot.result},
        {"active", snapshot.active},
        {"process_name_read", snapshot.process_name_read},
        {"process_name", snapshot.process_name},
        {"process_matches", snapshot.process_matches},
        {"architecture_supported", snapshot.architecture_supported},
        {"inputsystem_loaded", snapshot.inputsystem_loaded},
        {"imemanager_loaded", snapshot.imemanager_loaded},
        {"inputsystem_timestamp", snapshot.inputsystem_timestamp},
        {"inputsystem_image_size", snapshot.inputsystem_image_size},
        {"imemanager_timestamp", snapshot.imemanager_timestamp},
        {"imemanager_image_size", snapshot.imemanager_image_size},
        {"inputsystem_identity_readable", snapshot.inputsystem_identity_readable},
        {"imemanager_identity_readable", snapshot.imemanager_identity_readable},
        {"manager_interface_ready", snapshot.manager_interface_ready},
        {"manager_gate_ready", snapshot.manager_gate_ready},
        {"window_gate_ready", snapshot.window_gate_ready},
        {"private_api_verified", snapshot.private_api_verified},
        {"candidate_methods_match", snapshot.candidate_methods_match},
        {"runtime_verified", snapshot.runtime_verified},
        {"manager", snapshot.manager},
        {"active_hwnd", snapshot.active_hwnd},
        {"auxiliary_input_handler", snapshot.auxiliary_input_handler},
        {"input_source_code", snapshot.input_source_code},
        {"profile_code", snapshot.profile_code},
        {"effective_classification_code",
         snapshot.effective_classification_code},
        {"classification_available", snapshot.classification_available},
        {"attempted", snapshot.attempted},
        {"write_succeeded", snapshot.write_succeeded},
        {"readback_succeeded", snapshot.readback_succeeded},
        {"requested_profile_code", snapshot.requested_profile_code},
        {"readback_profile_code", snapshot.readback_profile_code},
        {"win32_error", snapshot.win32_error},
        {"classification_ready", snapshot.classification_ready},
        {"restore_attempted", snapshot.restore_attempted},
        {"restore_succeeded", snapshot.restore_succeeded},
        {"restore_readback_succeeded",
         snapshot.restore_readback_succeeded},
        {"restore_verified", snapshot.restore_verified},
        {"original_profile_code", snapshot.original_profile_code},
        {"current_profile_code", snapshot.current_profile_code},
        {"restored_profile_code", snapshot.restored_profile_code},
    });
}

} // namespace cxxime_tsf
