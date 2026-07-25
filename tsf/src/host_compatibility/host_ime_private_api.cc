// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "host_compatibility/host_ime_private_api.h"

#include <windows.h>

namespace cxxime_tsf {
namespace {

constexpr uintptr_t kManagerVtableRva = 0x2f678;
constexpr uintptr_t kManagerInitializedMethodRva = 0xc7d0;
constexpr uintptr_t kManagerEnabledMethodRva = 0xc7e0;
constexpr uintptr_t kNamesManagerVtableRva = 0x2f0c0;
constexpr uintptr_t kClassificationMethodRva = 0x42b0;

constexpr uintptr_t kManagerInitializedMethodOffset = 0x60;
constexpr uintptr_t kManagerEnabledMethodOffset = 0x70;
constexpr uintptr_t kClassificationMethodOffset = 0x10;

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

} // namespace

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
        request.module_verified && snapshot.manager_vtable_read &&
        snapshot.manager_initialized_method_read &&
        snapshot.manager_enabled_method_read &&
        snapshot.manager_vtable_rva == kManagerVtableRva &&
        snapshot.manager_initialized_method_rva ==
            kManagerInitializedMethodRva &&
        snapshot.manager_enabled_method_rva == kManagerEnabledMethodRva;
    snapshot.classification_verified =
        request.module_verified && snapshot.names_manager_vtable_read &&
        snapshot.classification_method_read &&
        snapshot.names_manager_vtable_rva == kNamesManagerVtableRva &&
        snapshot.classification_method_rva == kClassificationMethodRva;
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
