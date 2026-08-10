// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_ui_element_identity.h"

#include <windows.h>

#include <string>

namespace cxxime_tsf {
namespace {

std::string utf8_from_wide(const wchar_t* text, size_t length) {
    if (!text || length == 0) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text, static_cast<int>(length), &result[0], required, nullptr, nullptr);
    return result;
}

std::string module_name_utf8(const wchar_t* path, size_t length) {
    size_t name_offset = 0;
    for (size_t index = 0; index < length; ++index) {
        if (path[index] == L'\\' || path[index] == L'/') {
            name_offset = index + 1;
        }
    }
    return utf8_from_wide(path + name_offset, length - name_offset);
}

void add_implementation_module(ITfUIElement* element, nlohmann::json& fields) {
    void* vtable = element ? *reinterpret_cast<void**>(element) : nullptr;
    MEMORY_BASIC_INFORMATION memory = {};
    const SIZE_T queried = vtable ? VirtualQuery(vtable, &memory, sizeof(memory)) : 0;
    const HMODULE module = queried == sizeof(memory) && memory.Type == MEM_IMAGE
        ? reinterpret_cast<HMODULE>(memory.AllocationBase)
        : nullptr;

    wchar_t module_path[MAX_PATH] = {};
    const DWORD path_length = module
        ? GetModuleFileNameW(module, module_path, ARRAYSIZE(module_path))
        : 0;
    fields["implementation_vtable"] = reinterpret_cast<uintptr_t>(vtable);
    fields["implementation_module_base"] = reinterpret_cast<uintptr_t>(module);
    fields["implementation_vtable_rva"] = module && vtable
        ? reinterpret_cast<uintptr_t>(vtable) - reinterpret_cast<uintptr_t>(module)
        : 0;
    fields["implementation_module_found"] = module != nullptr && path_length > 0;
    fields["implementation_module_name"] = module_name_utf8(module_path, path_length);
    fields["implementation_module_path_digest"] = cxxime::host_trace_digest_utf16(
        module_path, path_length);
}

} // namespace

void add_ui_element_identity_fields(ITfUIElement* element, nlohmann::json& fields) {
    const HMODULE shared_correction_ui = GetModuleHandleW(L"mscand20.dll");
    fields["shared_correction_ui_loaded"] = shared_correction_ui != nullptr;
    fields["shared_correction_ui_module"] =
        reinterpret_cast<uintptr_t>(shared_correction_ui);
    add_implementation_module(element, fields);
}

} // namespace cxxime_tsf
