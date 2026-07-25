// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "host_compatibility/host_classification_runtime.h"
#include "host_compatibility/host_ime_private_api.h"

#include <cwchar>

namespace cxxime_tsf {
namespace {

constexpr wchar_t kTargetProcessName[] = L"dota2.exe";
constexpr DWORD kInputSystemTimestamp = 0x6a5e7111;
constexpr DWORD kInputSystemImageSize = 0x52000;
constexpr DWORD kImeManagerTimestamp = 0x6a5e7102;
constexpr DWORD kImeManagerImageSize = 0x3d000;
constexpr uintptr_t kManagerMessageMethodRva = 0xcc70;
constexpr uintptr_t kWindowMessageMethodRva = 0x56f0;
constexpr uintptr_t kAuxiliaryClassificationMethodRva = 0x42b0;
constexpr uintptr_t kCandidateNotifyMethodRva = 0xa410;
constexpr uintptr_t kCandidateChangeMethodRva = 0xa5f0;
constexpr uintptr_t kCandidateOpenMethodRva = 0xa840;
constexpr uint32_t kUnsupportedProfileCode = 0x01000000;
constexpr uint32_t kDefaultInputSourceCode = 0x02000000;
constexpr char kImeManagerInterfaceName[] = "IMEManager001";

using CreateInterfaceFn = void* (*)(const char* name, int* return_code);

struct ModuleIdentity {
    bool readable = false;
    DWORD timestamp = 0;
    DWORD image_size = 0;
};

template <typename Value>
bool read_process_value(uintptr_t address, Value* value) {
    SIZE_T bytes_read = 0;
    return address != 0 && value != nullptr &&
        ReadProcessMemory(
            GetCurrentProcess(), reinterpret_cast<const void*>(address), value,
            sizeof(*value), &bytes_read) != FALSE &&
        bytes_read == sizeof(*value);
}

ModuleIdentity read_module_identity(HMODULE module) {
    ModuleIdentity identity;
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    IMAGE_DOS_HEADER dos = {};
    if (!read_process_value(base, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0) {
        return identity;
    }

    IMAGE_NT_HEADERS headers = {};
    if (!read_process_value(base + static_cast<uintptr_t>(dos.e_lfanew), &headers) ||
        headers.Signature != IMAGE_NT_SIGNATURE) {
        return identity;
    }

    identity.readable = true;
    identity.timestamp = headers.FileHeader.TimeDateStamp;
    identity.image_size = headers.OptionalHeader.SizeOfImage;
    return identity;
}

void capture_process_gate(HostClassificationCompatibilitySnapshot* snapshot) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    snapshot->process_name_read = length > 0 && length < MAX_PATH;
    if (!snapshot->process_name_read) {
        return;
    }

    const wchar_t* slash = std::wcsrchr(path, L'\\');
    const wchar_t* name = slash ? slash + 1 : path;
    WideCharToMultiByte(
        CP_UTF8, 0, name, -1, snapshot->process_name,
        static_cast<int>(sizeof(snapshot->process_name)), nullptr, nullptr);
    snapshot->process_matches = _wcsicmp(name, kTargetProcessName) == 0;
}

bool current_process_owns_window(HWND hwnd) {
    DWORD process_id = 0;
    return hwnd && IsWindow(hwnd) &&
        GetWindowThreadProcessId(hwnd, &process_id) != 0 &&
        process_id == GetCurrentProcessId();
}

HostClassificationCompatibilitySnapshot fail_snapshot(
    HostClassificationCompatibilitySnapshot snapshot, const char* result) {
    snapshot.result = result;
    return snapshot;
}

} // namespace

HostClassificationCompatibilitySnapshot inspect_host_classification_runtime() {
    HostClassificationCompatibilitySnapshot snapshot;
    capture_process_gate(&snapshot);
    if (!snapshot.process_matches) {
        return fail_snapshot(snapshot, "process_mismatch");
    }
#if !defined(_M_X64)
    return fail_snapshot(snapshot, "architecture_unsupported");
#else
    snapshot.architecture_supported = true;
    HMODULE inputsystem = GetModuleHandleW(L"inputsystem.dll");
    HMODULE imemanager = GetModuleHandleW(L"imemanager.dll");
    snapshot.inputsystem_loaded = inputsystem != nullptr;
    snapshot.imemanager_loaded = imemanager != nullptr;
    const ModuleIdentity inputsystem_identity = read_module_identity(inputsystem);
    const ModuleIdentity imemanager_identity = read_module_identity(imemanager);
    snapshot.inputsystem_timestamp = inputsystem_identity.timestamp;
    snapshot.inputsystem_image_size = inputsystem_identity.image_size;
    snapshot.imemanager_timestamp = imemanager_identity.timestamp;
    snapshot.imemanager_image_size = imemanager_identity.image_size;
    snapshot.inputsystem_identity_matches =
        inputsystem_identity.readable &&
        inputsystem_identity.timestamp == kInputSystemTimestamp &&
        inputsystem_identity.image_size == kInputSystemImageSize;
    snapshot.imemanager_identity_matches =
        imemanager_identity.readable &&
        imemanager_identity.timestamp == kImeManagerTimestamp &&
        imemanager_identity.image_size == kImeManagerImageSize;
    if (!snapshot.inputsystem_identity_matches ||
        !snapshot.imemanager_identity_matches) {
        return fail_snapshot(snapshot, "binary_identity_mismatch");
    }

    const uintptr_t imemanager_base = reinterpret_cast<uintptr_t>(imemanager);
    FARPROC create_interface = GetProcAddress(imemanager, "CreateInterface");
    int interface_result = -1;
    void* manager_interface = create_interface
        ? reinterpret_cast<CreateInterfaceFn>(create_interface)(
              kImeManagerInterfaceName, &interface_result)
        : nullptr;
    snapshot.manager = reinterpret_cast<uintptr_t>(manager_interface);
    uintptr_t manager_vtable = 0;
    snapshot.manager_interface_ready =
        create_interface && interface_result == 0 && snapshot.manager != 0 &&
        read_process_value(snapshot.manager, &manager_vtable);
    if (!snapshot.manager_interface_ready) {
        return fail_snapshot(snapshot, "manager_unavailable");
    }

    uintptr_t manager_method = 0;
    uintptr_t window_handler = 0;
    uint8_t manager_initialized = 0;
    uint8_t manager_enabled = 0;
    const bool manager_state_read =
        read_process_value(manager_vtable + 0x80, &manager_method) &&
        read_process_value(snapshot.manager + 0x58, &window_handler) &&
        read_process_value(snapshot.manager + 0x60, &manager_initialized) &&
        read_process_value(snapshot.manager + 0x61, &manager_enabled);
    if (!manager_state_read || window_handler == 0) {
        return fail_snapshot(snapshot, "manager_state_unavailable");
    }

    uintptr_t window_vtable = 0;
    uintptr_t window_message_method = 0;
    uintptr_t candidate_processor = 0;
    uintptr_t auxiliary_input_handler = 0;
    HWND active_hwnd = nullptr;
    uint8_t window_message_enabled = 0;
    const bool window_state_read =
        read_process_value(window_handler, &window_vtable) &&
        read_process_value(window_handler + 0x20, &active_hwnd) &&
        read_process_value(window_handler + 0x28, &candidate_processor) &&
        read_process_value(window_handler + 0x30, &auxiliary_input_handler) &&
        read_process_value(window_handler + 0x51, &window_message_enabled) &&
        read_process_value(window_vtable + 0xc0, &window_message_method);
    snapshot.active_hwnd = reinterpret_cast<uintptr_t>(active_hwnd);
    snapshot.auxiliary_input_handler = auxiliary_input_handler;
    if (!window_state_read || candidate_processor == 0 ||
        auxiliary_input_handler == 0) {
        return fail_snapshot(snapshot, "window_state_unavailable");
    }

    uintptr_t auxiliary_vtable = 0;
    uintptr_t classification_method = 0;
    const bool classification_read =
        read_process_value(auxiliary_input_handler, &auxiliary_vtable) &&
        read_process_value(auxiliary_vtable + 0x10, &classification_method) &&
        read_process_value(
            auxiliary_input_handler + 0xd0, &snapshot.input_source_code) &&
        read_process_value(
            auxiliary_input_handler + 0xd4, &snapshot.profile_code);
    snapshot.classification_available = classification_read;
    if (!classification_read) {
        return fail_snapshot(snapshot, "classification_unavailable");
    }
    const uint32_t raw_classification =
        snapshot.input_source_code == kDefaultInputSourceCode
        ? kUnsupportedProfileCode
        : snapshot.profile_code;

    const HostImePrivateApiSnapshot private_api =
        inspect_host_ime_private_api({
            imemanager_base,
            snapshot.manager,
            auxiliary_input_handler,
            snapshot.imemanager_identity_matches,
            true,
            manager_initialized != 0,
            true,
            manager_enabled != 0,
            true,
            raw_classification,
        });
    snapshot.private_api_verified = private_api.verified;
    snapshot.effective_classification_code = private_api.classification_called
        ? private_api.classification
        : raw_classification;
    snapshot.manager_gate_ready =
        manager_method == imemanager_base + kManagerMessageMethodRva &&
        private_api.manager_called && private_api.manager_initialized &&
        private_api.manager_enabled;
    snapshot.window_gate_ready =
        window_message_method == imemanager_base + kWindowMessageMethodRva &&
        window_message_enabled != 0 && current_process_owns_window(active_hwnd);

    uintptr_t candidate_vtable = 0;
    uintptr_t candidate_notify_method = 0;
    uintptr_t candidate_change_method = 0;
    uintptr_t candidate_open_method = 0;
    const bool candidate_state_read =
        read_process_value(candidate_processor, &candidate_vtable) &&
        read_process_value(candidate_vtable + 0x30, &candidate_notify_method) &&
        read_process_value(candidate_vtable + 0xd8, &candidate_change_method) &&
        read_process_value(candidate_vtable + 0xe0, &candidate_open_method);
    snapshot.candidate_methods_match =
        candidate_state_read &&
        candidate_notify_method == imemanager_base + kCandidateNotifyMethodRva &&
        candidate_change_method == imemanager_base + kCandidateChangeMethodRva &&
        candidate_open_method == imemanager_base + kCandidateOpenMethodRva;
    snapshot.runtime_verified =
        snapshot.manager_gate_ready && snapshot.window_gate_ready &&
        snapshot.private_api_verified &&
        private_api.classification == raw_classification &&
        classification_method ==
            imemanager_base + kAuxiliaryClassificationMethodRva &&
        snapshot.candidate_methods_match;
    snapshot.result = snapshot.runtime_verified ? "verified" : "runtime_unverified";
    return snapshot;
#endif
}

} // namespace cxxime_tsf
