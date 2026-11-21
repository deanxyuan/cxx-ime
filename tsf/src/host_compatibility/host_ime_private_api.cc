// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "host_compatibility/host_ime_private_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <windows.h>

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
constexpr uint32_t kCandidateProtocol = 0x00810000;
constexpr uint8_t kCandidateCodeLoad[] = {0x8b, 0x41, 0x68}; // mov eax, [this+0x68]
constexpr uint8_t kCandidateDispatch[] = {
    0x48, 0x8b, 0x01, // mov rax, [this]
    0x33, 0xd2,       // xor edx, edx
    0xff, 0x90,       // call [rax+slot]
};
constexpr size_t kMaxNotifyBytes = 2048;

struct MethodCode {
    std::array<uint8_t, kMaxNotifyBytes> bytes{};
    size_t size = 0;
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

bool executable_range(uintptr_t module, uint32_t image_size, uintptr_t address, size_t size) {
    if (!module_contains(module, image_size, address, size)) {
        return false;
    }
    MEMORY_BASIC_INFORMATION region = {};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
        region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) != 0) {
        return false;
    }
    const DWORD access = region.Protect & 0xff;
    const bool executable = access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ ||
                            access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
    const uintptr_t offset = address - reinterpret_cast<uintptr_t>(region.BaseAddress);
    return executable && offset <= region.RegionSize && size <= region.RegionSize - offset;
}

bool read_method_code(uintptr_t module, uint32_t image_size, uintptr_t method, MethodCode* code) {
#if defined(_M_X64)
    DWORD64 image_base = 0;
    const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(method, &image_base, nullptr);
    RUNTIME_FUNCTION function = {};
    if (!entry || image_base != module ||
        !read_process_value(reinterpret_cast<uintptr_t>(entry), &function) ||
        function.BeginAddress >= function.EndAddress || method != module + function.BeginAddress) {
        return false;
    }
    const size_t size = function.EndAddress - function.BeginAddress;
    if (size > code->bytes.size() || !executable_range(module, image_size, method, size)) {
        return false;
    }
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(method),
                           code->bytes.data(), size, &read) ||
        read != size) {
        return false;
    }
    code->size = size;
    return true;
#else
    (void)module;
    (void)image_size;
    (void)method;
    (void)code;
    return false;
#endif
}

template <size_t Size>
bool code_matches(const MethodCode& code, size_t offset, const uint8_t (&bytes)[Size]) {
    return offset <= code.size && Size <= code.size - offset &&
           std::memcmp(code.bytes.data() + offset, bytes, Size) == 0;
}

bool is_instruction_prefix(uint8_t byte) {
    if (byte >= 0x40 && byte <= 0x4f) {
        return true; // REX changes the register or operand width.
    }
    switch (byte) {
    case 0x26:
    case 0x2e:
    case 0x36:
    case 0x3e:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0xf0:
    case 0xf2:
    case 0xf3:
        return true;
    default:
        return false;
    }
}

bool dispatches_candidate_protocol(const MethodCode& code, size_t start, uint32_t slot) {
    if (!code_matches(code, start, kCandidateCodeLoad)) {
        return false;
    }
    // A suffix of a prefixed instruction does not establish the required eax/rcx load.
    // Conservatively reject an ambiguous preceding byte instead of guessing its boundary.
    if (start != 0 && is_instruction_prefix(code.bytes[start - 1])) {
        return false;
    }
    size_t cursor = start + sizeof(kCandidateCodeLoad);
    // Only a contiguous cmp-eax / je chain is accepted: no intervening register writes,
    // calls or arbitrary constant searches. The equality target must dispatch this object.
    while (cursor + 7 <= code.size && code.bytes[cursor] == 0x3d) {
        uint32_t classification = 0;
        std::memcpy(&classification, code.bytes.data() + cursor + 1, sizeof(classification));
        cursor += 5;
        int64_t displacement = 0;
        if (code.bytes[cursor] == 0x74) {
            displacement = static_cast<int8_t>(code.bytes[cursor + 1]);
            cursor += 2;
        } else if (cursor + 6 <= code.size && code.bytes[cursor] == 0x0f &&
                   code.bytes[cursor + 1] == 0x84) {
            int32_t relative = 0;
            std::memcpy(&relative, code.bytes.data() + cursor + 2, sizeof(relative));
            displacement = relative;
            cursor += 6;
        } else {
            return false;
        }
        if (classification == kCandidateProtocol) {
            const int64_t target = static_cast<int64_t>(cursor) + displacement;
            if (target < 0 || target >= static_cast<int64_t>(code.size)) {
                return false;
            }
            const size_t offset = static_cast<size_t>(target);
            if (!code_matches(code, offset, kCandidateDispatch) ||
                sizeof(kCandidateDispatch) + sizeof(slot) > code.size - offset) {
                return false;
            }
            uint32_t actual_slot = 0;
            std::memcpy(&actual_slot, code.bytes.data() + offset + sizeof(kCandidateDispatch),
                        sizeof(actual_slot));
            return actual_slot == slot;
        }
    }
    return false;
}

template <size_t Size>
bool method_matches(uintptr_t module, uint32_t image_size, uintptr_t method,
    const uint8_t (&signature)[Size]) {
    uint8_t bytes[Size] = {};
    return executable_range(module, image_size, method, Size) &&
        read_process_value(method, &bytes) && std::memcmp(bytes, signature, Size) == 0;
}

} // namespace

bool host_ime_method_is_executable(uintptr_t module, uint32_t image_size, uintptr_t method) {
    return executable_range(module, image_size, method, 1);
}

bool host_ime_classification_method_matches(uintptr_t module, uint32_t image_size,
    uintptr_t method) {
    return method_matches(module, image_size, method, kClassificationSignature);
}

bool host_ime_candidate_notify_method_matches(uintptr_t module, uint32_t image_size,
    uintptr_t method) {
    MethodCode code;
    if (!read_method_code(module, image_size, method, &code)) {
        return false;
    }
    bool opens = false;
    bool changes = false;
    for (size_t offset = 0; offset < code.size && (!opens || !changes); ++offset) {
        opens = opens || dispatches_candidate_protocol(code, offset, 0xe0);
        changes = changes || dispatches_candidate_protocol(code, offset, 0xd8);
    }
    return opens && changes;
}

bool host_ime_candidate_methods_match(uintptr_t module, uint32_t image_size,
    uintptr_t notify_method, uintptr_t change_method,
    uintptr_t open_method) {
    return host_ime_candidate_notify_method_matches(module, image_size, notify_method) &&
        host_ime_method_is_executable(module, image_size, change_method) &&
        host_ime_method_is_executable(module, image_size, open_method);
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
