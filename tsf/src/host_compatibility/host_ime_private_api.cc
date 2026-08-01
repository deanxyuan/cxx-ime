// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "host_compatibility/host_ime_private_api.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cxxime_tsf {
namespace {

constexpr uintptr_t kManagerInitializedMethodOffset = 0x60;
constexpr uintptr_t kManagerEnabledMethodOffset = 0x70;
constexpr uintptr_t kClassificationMethodOffset = 0x10;

constexpr uint8_t kManagerInitializedSignature[] = {
    0x0f, 0xb6, 0x41, 0x60, 0xc3,
};
constexpr uint8_t kManagerEnabledSignature[] = {
    0x0f, 0xb6, 0x41, 0x61, 0xc3,
};
constexpr uint8_t kClassificationSignature[] = {
    0x81, 0xb9, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xb8, 0x00,
    0x00, 0x00, 0x01, 0x74, 0x06, 0x8b, 0x81, 0xd4, 0x00, 0x00, 0x00, 0xc3,
};
constexpr uint8_t kManagerMessageSignature[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x60, 0x80, 0x79, 0x61, 0x00,
};
constexpr uint8_t kWindowMessageSignature[] = {
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x40, 0x80, 0x79, 0x51, 0x00,
};
constexpr uint8_t kCandidateNotifySignature[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x49, 0x8b, 0xc0, 0x49,
    0x8b, 0xf1, 0x49, 0x8b, 0xf8, 0x8b, 0xea, 0x48, 0x8b, 0xd9,
};
constexpr uint8_t kCandidateCodeFieldSignature[] = {0x8b, 0x41, 0x68};
constexpr uint8_t kCandidateProtocolSignature[] = {0x3d, 0x00, 0x00, 0x81, 0x00};
constexpr uint8_t kCandidateChangeSignature[] = {
    0x40, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xec, 0x28, 0x48,
    0x8b, 0xf1, 0x8b, 0xfa, 0x48, 0x8b, 0x49, 0x08, 0xff, 0x15,
};
constexpr uint8_t kCandidateOpenSignature[] = {
    0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x28, 0x48,
    0x8b, 0xf1, 0x44, 0x8b, 0xf2, 0x48, 0x8b, 0x49, 0x08, 0xff, 0x15,
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

uintptr_t method_rva(uintptr_t module, uintptr_t method) {
    return module != 0 && method >= module ? method - module : 0;
}

bool module_contains(uintptr_t module, uint32_t image_size, uintptr_t address, size_t byte_count) {
    if (module == 0 || image_size == 0 || address < module || byte_count > image_size) {
        return false;
    }
    const uintptr_t offset = address - module;
    return offset <= image_size - byte_count;
}

template <size_t Size>
bool method_matches(uintptr_t module, uint32_t image_size, uintptr_t method,
    const uint8_t (&signature)[Size]) {
    uint8_t bytes[Size] = {};
    return module_contains(module, image_size, method, Size) &&
        read_process_value(method, &bytes) && std::memcmp(bytes, signature, Size) == 0;
}

} // namespace

bool host_ime_manager_message_method_matches(uintptr_t module, uint32_t image_size,
    uintptr_t method) {
    return method_matches(module, image_size, method, kManagerMessageSignature);
}

bool host_ime_window_message_method_matches(uintptr_t module, uint32_t image_size,
    uintptr_t method) {
    return method_matches(module, image_size, method, kWindowMessageSignature);
}

bool host_ime_classification_method_matches(uintptr_t module, uint32_t image_size,
    uintptr_t method) {
    return method_matches(module, image_size, method, kClassificationSignature);
}

bool host_ime_candidate_notify_method_matches(uintptr_t module, uint32_t image_size,
    uintptr_t method) {
    return method_matches(module, image_size, method, kCandidateNotifySignature) &&
        method_matches(module, image_size, method + 0x3c, kCandidateCodeFieldSignature) &&
        method_matches(module, image_size, method + 0x69, kCandidateProtocolSignature);
}

bool host_ime_candidate_methods_match(uintptr_t module, uint32_t image_size,
    uintptr_t notify_method, uintptr_t change_method,
    uintptr_t open_method) {
    return host_ime_candidate_notify_method_matches(module, image_size, notify_method) &&
        method_matches(module, image_size, change_method, kCandidateChangeSignature) &&
        method_matches(module, image_size, open_method, kCandidateOpenSignature);
}

HostImePrivateApiSnapshot inspect_host_ime_private_api(
    const HostImePrivateApiRequest& request) {
    HostImePrivateApiSnapshot snapshot;
#if !defined(_M_X64)
    (void)request;
    return snapshot;
#else
    snapshot.architecture_supported = true;
    uintptr_t manager_vtable = 0;
    uintptr_t manager_initialized_method = 0;
    uintptr_t manager_enabled_method = 0;
    uintptr_t names_manager_vtable = 0;
    uintptr_t classification_method = 0;
    snapshot.manager_vtable_read =
        read_process_value(request.manager, &manager_vtable);
    snapshot.manager_initialized_method_read =
        snapshot.manager_vtable_read && read_process_value(
            manager_vtable + kManagerInitializedMethodOffset,
            &manager_initialized_method);
    snapshot.manager_enabled_method_read =
        snapshot.manager_vtable_read && read_process_value(
            manager_vtable + kManagerEnabledMethodOffset,
            &manager_enabled_method);
    snapshot.names_manager_vtable_read =
        read_process_value(request.names_manager, &names_manager_vtable);
    snapshot.classification_method_read =
        snapshot.names_manager_vtable_read && read_process_value(
            names_manager_vtable + kClassificationMethodOffset,
            &classification_method);

    snapshot.manager_vtable_rva =
        method_rva(request.imemanager_base, manager_vtable);
    snapshot.manager_initialized_method_rva =
        method_rva(request.imemanager_base, manager_initialized_method);
    snapshot.manager_enabled_method_rva =
        method_rva(request.imemanager_base, manager_enabled_method);
    snapshot.names_manager_vtable_rva =
        method_rva(request.imemanager_base, names_manager_vtable);
    snapshot.classification_method_rva =
        method_rva(request.imemanager_base, classification_method);
    snapshot.manager_verified =
        request.module_readable && snapshot.manager_vtable_read &&
        snapshot.manager_initialized_method_read && snapshot.manager_enabled_method_read &&
        module_contains(request.imemanager_base, request.imemanager_image_size, manager_vtable,
            sizeof(uintptr_t)) &&
        method_matches(request.imemanager_base, request.imemanager_image_size,
            manager_initialized_method, kManagerInitializedSignature) &&
        method_matches(request.imemanager_base, request.imemanager_image_size,
            manager_enabled_method, kManagerEnabledSignature);
    snapshot.classification_verified =
        request.module_readable && snapshot.names_manager_vtable_read &&
        snapshot.classification_method_read &&
        module_contains(request.imemanager_base, request.imemanager_image_size,
            names_manager_vtable, sizeof(uintptr_t)) &&
        host_ime_classification_method_matches(
            request.imemanager_base, request.imemanager_image_size, classification_method);
    snapshot.verified =
        snapshot.manager_verified && snapshot.classification_verified;

    using BoolGetter = bool (*)(void* instance);
    using ClassificationGetter = uint32_t (*)(void* instance);
    if (snapshot.manager_verified) {
        snapshot.manager_initialized =
            reinterpret_cast<BoolGetter>(manager_initialized_method)(
                reinterpret_cast<void*>(request.manager));
        snapshot.manager_enabled =
            reinterpret_cast<BoolGetter>(manager_enabled_method)(
                reinterpret_cast<void*>(request.manager));
        snapshot.manager_called = true;
    }
    if (snapshot.classification_verified) {
        snapshot.classification =
            reinterpret_cast<ClassificationGetter>(classification_method)(
                reinterpret_cast<void*>(request.names_manager));
        snapshot.classification_called = true;
    }
    snapshot.result = snapshot.verified ? "called" : "method_unverified";
    return snapshot;
#endif
}

} // namespace cxxime_tsf
