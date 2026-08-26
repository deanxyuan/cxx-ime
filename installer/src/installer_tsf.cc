// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/installer_tsf.h>

#include <windows.h>
#include <msctf.h>
#include <objbase.h>

#include <cxxime/text_service_profile.h>

namespace cxxime {
namespace installer {

int release_input_processor() {
    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init_result)) {
        return 1;
    }

    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&profile_manager));
    if (SUCCEEDED(result)) {
        result = profile_manager->ReleaseInputProcessor(
            cxxime::kTextServiceClsid, TF_RIP_FLAG_FREEUNUSEDLIBRARIES);
        profile_manager->Release();
    }
    CoUninitialize();
    return SUCCEEDED(result) ? 0 : 1;
}

} // namespace installer
} // namespace cxxime
