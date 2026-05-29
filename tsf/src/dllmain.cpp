// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "globals.h"
#include "class_factory.h"
#include "register.h"
#include "text_service.h"

// Forward declarations for DllRegisterServer/DllUnregisterServer
STDAPI DllUnregisterServer();

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
        InitializeCriticalSection(&g_cs);
        break;
    case DLL_PROCESS_DETACH:
        TextService::shutdown_trace();  // Flush and close trace writer thread
        DeleteCriticalSection(&g_cs);
        break;
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv)
        return E_INVALIDARG;
    *ppv = nullptr;

    if (!IsEqualCLSID(rclsid, c_clsidTextService))
        return CLASS_E_CLASSNOTAVAILABLE;

    EnterCriticalSection(&g_cs);

    static ClassFactory s_classFactory;
    HRESULT hr = s_classFactory.QueryInterface(riid, ppv);

    LeaveCriticalSection(&g_cs);
    return hr;
}

STDAPI DllCanUnloadNow() {
    return g_cRefDll >= 0 ? S_FALSE : S_OK;
}

STDAPI DllRegisterServer() {
    HRESULT hr;

    hr = register_server();
    if (FAILED(hr))
        goto cleanup;

    hr = register_profiles();
    if (FAILED(hr))
        goto cleanup;

    hr = register_categories();
    if (FAILED(hr))
        goto cleanup;

    return S_OK;

cleanup:
    DllUnregisterServer();
    return hr;
}

STDAPI DllUnregisterServer() {
    unregister_categories();
    unregister_profiles();
    unregister_server();
    return S_OK;
}
