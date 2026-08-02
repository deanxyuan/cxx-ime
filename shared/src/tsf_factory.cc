// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/tsf_factory.h>

#include <string>

#include <windows.h>

namespace cxxime {
namespace {

HMODULE load_tsf_module() {
    wchar_t system_directory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return nullptr;
    }

    std::wstring module_path(system_directory, length);
    module_path += L"\\msctf.dll";
    return LoadLibraryW(module_path.c_str());
}

HMODULE tsf_module() {
    // Factory calls remain valid because the module stays loaded for the process lifetime.
    static HMODULE module = load_tsf_module();
    return module;
}

template <typename Interface>
HRESULT invoke_tsf_factory(const char* name, Interface** output) {
    if (!output) {
        return E_POINTER;
    }
    *output = nullptr;

    HMODULE module = tsf_module();
    if (!module) {
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }

    using Factory = HRESULT(WINAPI*)(Interface**);
    const auto factory = reinterpret_cast<Factory>(GetProcAddress(module, name));
    if (!factory) {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    const HRESULT result = factory(output);
    return SUCCEEDED(result) && !*output ? E_UNEXPECTED : result;
}

} // namespace

HRESULT create_tsf_thread_manager_without_com(ITfThreadMgr** thread_manager) {
    return invoke_tsf_factory("TF_CreateThreadMgr", thread_manager);
}

HRESULT create_tsf_category_manager_without_com(ITfCategoryMgr** category_manager) {
    return invoke_tsf_factory("TF_CreateCategoryMgr", category_manager);
}

HRESULT create_tsf_display_attribute_manager_without_com(
    ITfDisplayAttributeMgr** display_attribute_manager) {
    return invoke_tsf_factory("TF_CreateDisplayAttributeMgr", display_attribute_manager);
}

HRESULT create_tsf_input_processor_profile_manager_without_com(
    ITfInputProcessorProfileMgr** profile_manager) {
    if (!profile_manager) {
        return E_POINTER;
    }
    *profile_manager = nullptr;

    ITfInputProcessorProfiles* profiles = nullptr;
    const HRESULT factory_result = invoke_tsf_factory("TF_CreateInputProcessorProfiles", &profiles);
    if (FAILED(factory_result)) {
        return factory_result;
    }

    const HRESULT query_result = profiles->QueryInterface(
        IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(profile_manager));
    profiles->Release();
    return SUCCEEDED(query_result) && !*profile_manager ? E_NOINTERFACE : query_result;
}

} // namespace cxxime
