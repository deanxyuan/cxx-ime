// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_sdl_message_hook.h"

#include <cxxime/stage_trace.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace cxxime_tsf {
namespace {

std::string utf8_module_name(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = module
        ? GetModuleFileNameW(module, path, ARRAYSIZE(path))
        : 0;
    if (length == 0 || length >= ARRAYSIZE(path)) {
        return {};
    }

    DWORD name_offset = 0;
    for (DWORD index = 0; index < length; ++index) {
        if (path[index] == L'\\' || path[index] == L'/') {
            name_offset = index + 1;
        }
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, path + name_offset, static_cast<int>(length - name_offset),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string name(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, path + name_offset, static_cast<int>(length - name_offset),
        &name[0], required, nullptr, nullptr);
    return name;
}

HMODULE module_from_address(const void* address) {
    MEMORY_BASIC_INFORMATION memory = {};
    const SIZE_T queried = address
        ? VirtualQuery(address, &memory, sizeof(memory))
        : 0;
    return queried == sizeof(memory) && memory.Type == MEM_IMAGE
        ? reinterpret_cast<HMODULE>(memory.AllocationBase)
        : nullptr;
}

void add_address_identity(nlohmann::json& fields,
                          const char* prefix,
                          const void* address) {
    const HMODULE module = module_from_address(address);
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    fields[std::string(prefix) + "_address"] = value;
    fields[std::string(prefix) + "_module_name"] = utf8_module_name(module);
    fields[std::string(prefix) + "_module_rva"] = module && address
        ? value - base
        : 0;
}

template <typename Value>
bool read_process_value(const void* address, Value* value) {
    SIZE_T bytes_read = 0;
    return address && value &&
        ReadProcessMemory(
            GetCurrentProcess(), address, value, sizeof(*value), &bytes_read) != FALSE &&
        bytes_read == sizeof(*value);
}

#if defined(_M_X64)
bool decode_sdl_dynapi_slot(const void* export_address, const void* const** slot) {
    if (!slot) {
        return false;
    }
    *slot = nullptr;
    uint8_t instruction[7] = {};
    if (!read_process_value(export_address, &instruction) ||
        instruction[0] != 0x48 || instruction[1] != 0xff || instruction[2] != 0x25) {
        return false;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, instruction + 3, sizeof(displacement));
    const auto next = reinterpret_cast<const uint8_t*>(export_address) + sizeof(instruction);
    *slot = reinterpret_cast<const void* const*>(next + displacement);
    return true;
}

bool decode_rip_store(const uint8_t* code,
                      size_t offset,
                      uint8_t source_register,
                      uintptr_t instruction_address,
                      const void* const** storage) {
    if (!storage || code[offset] != 0x48 || code[offset + 1] != 0x89 ||
        code[offset + 2] != source_register) {
        return false;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, code + offset + 3, sizeof(displacement));
    *storage = reinterpret_cast<const void* const*>(
        instruction_address + offset + 7 + displacement);
    return true;
}
#endif

} // namespace

void trace_stage_sdl_windows_message_hook(HMODULE module) {
    const void* export_address = reinterpret_cast<const void*>(
        module ? GetProcAddress(module, "SDL_SetWindowsMessageHook") : nullptr);
    nlohmann::json fields = {
        {"export_present", export_address != nullptr},
        {"decoder", "sdl3_dynapi_x64_rip_stores"},
    };
    add_address_identity(fields, "export", export_address);
    if (!export_address) {
        fields["result"] = "export_unavailable";
        cxxime::write_stage_trace(
            "tsf", "sdl.windows_message_hook", std::move(fields));
        return;
    }

#if defined(_M_X64)
    const void* const* dispatch_slot = nullptr;
    const void* implementation = nullptr;
    const bool slot_decoded = decode_sdl_dynapi_slot(export_address, &dispatch_slot);
    const bool implementation_read =
        slot_decoded && read_process_value(dispatch_slot, &implementation);
    fields["dispatch_slot_decoded"] = slot_decoded;
    fields["dispatch_slot"] = reinterpret_cast<uintptr_t>(dispatch_slot);
    fields["implementation_read"] = implementation_read;
    add_address_identity(fields, "implementation", implementation);
    if (!implementation_read || !implementation) {
        fields["result"] = slot_decoded
            ? "implementation_unavailable"
            : "dispatch_decode_failed";
        cxxime::write_stage_trace(
            "tsf", "sdl.windows_message_hook", std::move(fields));
        return;
    }

    uint8_t code[48] = {};
    if (!read_process_value(implementation, &code)) {
        fields["result"] = "implementation_read_failed";
        cxxime::write_stage_trace(
            "tsf", "sdl.windows_message_hook", std::move(fields));
        return;
    }

    const void* const* callback_storage = nullptr;
    const void* const* userdata_storage = nullptr;
    const uintptr_t implementation_address =
        reinterpret_cast<uintptr_t>(implementation);
    for (size_t offset = 0; offset + 7 <= sizeof(code); ++offset) {
        if (!callback_storage) {
            decode_rip_store(
                code, offset, 0x0d, implementation_address, &callback_storage);
        }
        if (!userdata_storage) {
            decode_rip_store(
                code, offset, 0x15, implementation_address, &userdata_storage);
        }
    }

    const void* callback = nullptr;
    const void* userdata = nullptr;
    const bool callback_read =
        callback_storage && read_process_value(callback_storage, &callback);
    const bool userdata_read =
        userdata_storage && read_process_value(userdata_storage, &userdata);
    fields["callback_storage_decoded"] = callback_storage != nullptr;
    fields["callback_storage"] = reinterpret_cast<uintptr_t>(callback_storage);
    fields["callback_read"] = callback_read;
    fields["userdata_storage_decoded"] = userdata_storage != nullptr;
    fields["userdata_storage"] = reinterpret_cast<uintptr_t>(userdata_storage);
    fields["userdata_read"] = userdata_read;
    fields["userdata"] = reinterpret_cast<uintptr_t>(userdata);
    fields["callback_registered"] = callback_read && callback != nullptr;
    add_address_identity(fields, "callback", callback);
    if (!callback_storage || !userdata_storage) {
        fields["result"] = "storage_decode_failed";
    } else if (!callback_read || !userdata_read) {
        fields["result"] = "storage_read_failed";
    } else {
        fields["result"] = callback ? "registered" : "not_registered";
    }
#else
    fields["result"] = "architecture_unsupported";
#endif
    cxxime::write_stage_trace(
        "tsf", "sdl.windows_message_hook", std::move(fields));
}

} // namespace cxxime_tsf
