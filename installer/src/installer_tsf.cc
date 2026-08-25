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
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_ALL,
        IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&profile_manager));
    if (SUCCEEDED(result)) {
        // Deactivate the profile for every thread on this desktop before asking TSF to release
        // its TIP instance. This covers hosts that switched away from CxxIME but still retain an
        // active profile object in their thread manager.
        profile_manager->DeactivateProfile(
            TF_PROFILETYPE_INPUTPROCESSOR, cxxime::kTextServiceLanguageId,
            cxxime::kTextServiceClsid, cxxime::kTextServiceProfileGuid, nullptr,
            TF_IPPMF_FORSESSION);
        result = profile_manager->ReleaseInputProcessor(
            cxxime::kTextServiceClsid, TF_RIP_FLAG_FREEUNUSEDLIBRARIES);
        profile_manager->Release();
    }
    CoUninitialize();
    return SUCCEEDED(result) ? 0 : 1;
}

} // namespace installer
} // namespace cxxime
