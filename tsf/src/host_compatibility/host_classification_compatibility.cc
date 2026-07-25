// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "host_compatibility/host_classification_compatibility.h"

#include "host_compatibility/host_classification_runtime.h"

namespace cxxime_tsf {
namespace {

constexpr uintptr_t kProfileCodeOffset = 0xd4;
constexpr uint32_t kSimplifiedChineseInputSourceCode = 0x00080000;
constexpr uint32_t kHostCandidateProtocolCode = 0x00810000;
constexpr uint32_t kUnsupportedProfileCode = 0x01000000;

thread_local bool g_active = false;
thread_local uintptr_t g_patched_handler = 0;
thread_local uint32_t g_original_profile_code = 0;
thread_local HostClassificationCompatibilitySnapshot g_snapshot;

template <typename Value>
bool read_process_value(uintptr_t address, Value* value) {
    SIZE_T bytes_read = 0;
    return address != 0 && value != nullptr &&
        ReadProcessMemory(
            GetCurrentProcess(), reinterpret_cast<const void*>(address), value,
            sizeof(*value), &bytes_read) != FALSE &&
        bytes_read == sizeof(*value);
}

template <typename Value>
bool write_process_value(uintptr_t address, const Value& value, DWORD* error) {
    SIZE_T bytes_written = 0;
    SetLastError(ERROR_SUCCESS);
    const bool succeeded =
        address != 0 &&
        WriteProcessMemory(
            GetCurrentProcess(), reinterpret_cast<void*>(address), &value,
            sizeof(value), &bytes_written) != FALSE &&
        bytes_written == sizeof(value);
    if (error) {
        const DWORD write_error = GetLastError();
        *error = succeeded
            ? ERROR_SUCCESS
            : (write_error != ERROR_SUCCESS ? write_error : ERROR_WRITE_FAULT);
    }
    return succeeded;
}

void reset_patch_state() {
    g_patched_handler = 0;
    g_original_profile_code = 0;
}

HostClassificationCompatibilitySnapshot finish(
    HostClassificationCompatibilitySnapshot snapshot, const char* result) {
    snapshot.result = result;
    g_snapshot = snapshot;
    return snapshot;
}

} // namespace

void activate_host_classification_compatibility() {
    reset_patch_state();
    g_active = true;
    g_snapshot = {};
    g_snapshot.action = "activate";
    g_snapshot.result = "armed";
    g_snapshot.active = true;
    g_snapshot.requested_profile_code = kHostCandidateProtocolCode;
}

HostClassificationCompatibilitySnapshot prepare_host_classification_compatibility() {
    HostClassificationCompatibilitySnapshot snapshot =
        inspect_host_classification_runtime();
    snapshot.action = "prepare";
    snapshot.active = g_active;
    snapshot.requested_profile_code = kHostCandidateProtocolCode;
    snapshot.original_profile_code = g_original_profile_code;
    if (!g_active) {
        return finish(snapshot, "inactive");
    }
    if (!snapshot.runtime_verified) {
        return finish(snapshot, snapshot.result);
    }
    if (snapshot.input_source_code != kSimplifiedChineseInputSourceCode) {
        return finish(snapshot, "input_source_mismatch");
    }
    if (snapshot.profile_code == kHostCandidateProtocolCode) {
        snapshot.readback_succeeded = true;
        snapshot.readback_profile_code = snapshot.profile_code;
        snapshot.classification_ready = true;
        return finish(snapshot, "already_applied");
    }
    if (snapshot.profile_code != kUnsupportedProfileCode) {
        return finish(snapshot, "supported_profile_preserved");
    }
    if (g_patched_handler != 0 &&
        g_patched_handler != snapshot.auxiliary_input_handler) {
        return finish(snapshot, "handler_changed");
    }

    snapshot.attempted = true;
    snapshot.write_succeeded = write_process_value(
        snapshot.auxiliary_input_handler + kProfileCodeOffset,
        kHostCandidateProtocolCode, &snapshot.win32_error);
    if (!snapshot.write_succeeded) {
        return finish(snapshot, "write_failed");
    }
    g_patched_handler = snapshot.auxiliary_input_handler;
    g_original_profile_code = snapshot.profile_code;
    snapshot.original_profile_code = g_original_profile_code;
    snapshot.readback_succeeded = read_process_value(
        snapshot.auxiliary_input_handler + kProfileCodeOffset,
        &snapshot.readback_profile_code);
    snapshot.classification_ready =
        snapshot.readback_succeeded &&
        snapshot.readback_profile_code == kHostCandidateProtocolCode;
    return finish(
        snapshot, snapshot.classification_ready ? "applied" : "readback_mismatch");
}

HostClassificationCompatibilitySnapshot deactivate_host_classification_compatibility() {
    HostClassificationCompatibilitySnapshot snapshot = g_snapshot;
    snapshot.action = "deactivate";
    snapshot.active = false;
    snapshot.original_profile_code = g_original_profile_code;
    g_active = false;

    const uintptr_t profile_address = g_patched_handler + kProfileCodeOffset;
    const bool current_read = g_patched_handler != 0 &&
        read_process_value(
            profile_address, &snapshot.current_profile_code);
    snapshot.restore_attempted =
        current_read &&
        snapshot.current_profile_code == kHostCandidateProtocolCode;
    snapshot.restore_succeeded =
        snapshot.restore_attempted &&
        write_process_value(
            profile_address, g_original_profile_code, &snapshot.win32_error);
    snapshot.restore_readback_succeeded =
        snapshot.restore_succeeded &&
        read_process_value(profile_address, &snapshot.restored_profile_code);
    snapshot.restore_verified =
        snapshot.restore_readback_succeeded &&
        snapshot.restored_profile_code == g_original_profile_code;
    if (g_patched_handler != 0 && !current_read) {
        snapshot.result = "restore_read_failed";
    } else if (current_read &&
               snapshot.current_profile_code != kHostCandidateProtocolCode) {
        snapshot.result = "host_state_changed";
    } else if (snapshot.restore_attempted) {
        snapshot.result = snapshot.restore_verified ? "restored" : "restore_failed";
    } else {
        snapshot.result = "no_restore_needed";
    }
    reset_patch_state();
    g_snapshot = snapshot;
    return snapshot;
}

const HostClassificationCompatibilitySnapshot&
host_classification_compatibility_snapshot() {
    return g_snapshot;
}

} // namespace cxxime_tsf
