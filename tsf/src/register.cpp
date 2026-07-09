// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// The input profile icon is registered from cxxime-resources.dll. The TSF
// module does not embed icon resources.

#include "register.h"
#include "globals.h"
#include "resource_loader.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

HRESULT register_server() {
    WCHAR dll_path[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, dll_path, MAX_PATH);

    WCHAR key_path[256] = {};
    wsprintfW(key_path, L"CLSID\\{%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              c_clsidTextService.Data1, c_clsidTextService.Data2, c_clsidTextService.Data3,
              c_clsidTextService.Data4[0], c_clsidTextService.Data4[1], c_clsidTextService.Data4[2],
              c_clsidTextService.Data4[3], c_clsidTextService.Data4[4], c_clsidTextService.Data4[5],
              c_clsidTextService.Data4[6], c_clsidTextService.Data4[7]);

    HKEY hKey = nullptr;
    LONG lr = RegCreateKeyExW(HKEY_CLASSES_ROOT, key_path, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    if (lr != ERROR_SUCCESS)
        return E_FAIL;

    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)TEXTSERVICE_DESC,
                   (DWORD)((wcslen(TEXTSERVICE_DESC) + 1) * sizeof(WCHAR)));

    HKEY hSubKey = nullptr;
    lr = RegCreateKeyExW(hKey, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &hSubKey, nullptr);
    if (lr == ERROR_SUCCESS) {
        RegSetValueExW(hSubKey, nullptr, 0, REG_SZ, (const BYTE*)dll_path,
                       (DWORD)((wcslen(dll_path) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hSubKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)TEXTSERVICE_MODEL,
                       (DWORD)((wcslen(TEXTSERVICE_MODEL) + 1) * sizeof(WCHAR)));
        RegCloseKey(hSubKey);
    }

    RegCloseKey(hKey);
    return S_OK;
}

HRESULT unregister_server() {
    WCHAR key_path[256] = {};
    wsprintfW(key_path, L"CLSID\\{%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              c_clsidTextService.Data1, c_clsidTextService.Data2, c_clsidTextService.Data3,
              c_clsidTextService.Data4[0], c_clsidTextService.Data4[1], c_clsidTextService.Data4[2],
              c_clsidTextService.Data4[3], c_clsidTextService.Data4[4], c_clsidTextService.Data4[5],
              c_clsidTextService.Data4[6], c_clsidTextService.Data4[7]);

    SHDeleteKeyW(HKEY_CLASSES_ROOT, key_path);
    return S_OK;
}

// Profile registration (Windows 8+ API).

HRESULT register_profiles() {
    WCHAR achIconFile[MAX_PATH] = {};
    if (!cxxime_tsf::get_resource_dll_path(achIconFile, ARRAYSIZE(achIconFile))) {
        return E_FAIL;
    }
    ULONG cchIconFile = (ULONG)wcslen(achIconFile);

    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_ALL,
                                  IID_ITfInputProcessorProfileMgr, (void**)&pProfileMgr);
    if (FAILED(hr))
        return hr;

    // Register simplified Chinese profile (HKL = NULL for pure TSF IME)
    hr = pProfileMgr->RegisterProfile(
        c_clsidTextService,
        TEXTSERVICE_LANGID_HANS,
        c_guidProfile,
        TEXTSERVICE_DESC,
        (ULONG)wcslen(TEXTSERVICE_DESC),
        achIconFile,
        cchIconFile,
        TEXTSERVICE_ICON_INDEX,
        nullptr,  // hkl is NULL for a pure TSF input method.
        0,        // flags
        TRUE,     // enable
        0);

    pProfileMgr->Release();
    return hr;
}

HRESULT unregister_profiles() {
    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_ALL,
                                  IID_ITfInputProcessorProfileMgr, (void**)&pProfileMgr);
    if (FAILED(hr))
        return hr;

    pProfileMgr->UnregisterProfile(c_clsidTextService, TEXTSERVICE_LANGID_HANS,
                                    c_guidProfile, 0);
    pProfileMgr->Release();
    return S_OK;
}

// Category registration.

static const GUID kSupportCategories[] = {
    GUID_TFCAT_CATEGORY_OF_TIP,
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_TIPCAP_SECUREMODE,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_WOW16,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    GUID_TFCAT_PROP_AUDIODATA,
    GUID_TFCAT_PROP_INKDATA,
    GUID_TFCAT_PROPSTYLE_CUSTOM,
    GUID_TFCAT_PROPSTYLE_STATIC,
    GUID_TFCAT_PROPSTYLE_STATICCOMPACT,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    GUID_TFCAT_DISPLAYATTRIBUTEPROPERTY,
};

HRESULT register_categories() {
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
        return hr;

    for (const auto& guid : kSupportCategories) {
        hr = pCategoryMgr->RegisterCategory(c_clsidTextService, guid, c_clsidTextService);
        if (FAILED(hr)) break;
    }

    pCategoryMgr->Release();
    return hr;
}

HRESULT unregister_categories() {
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
        return hr;

    for (const auto& guid : kSupportCategories) {
        pCategoryMgr->UnregisterCategory(c_clsidTextService, guid, c_clsidTextService);
    }

    pCategoryMgr->Release();
    return S_OK;
}
