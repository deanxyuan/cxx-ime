// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "globals.h"
#include "class_factory.h"
#include "register.h"
#include "text_service.h"
#include <cxxime/status_window.h>
#include <cxxime/config_monitor.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>
#include <cxxime/logging.h>
#include <cxxime/diagnostics_config.h>
#include <atomic>

// Forward declarations for DllRegisterServer/DllUnregisterServer
STDAPI DllUnregisterServer();

static cxxime::ConfigMonitor* g_config_monitor = nullptr;

static cxxime::Config g_config;

cxxime::ConfigMonitor* get_config_monitor() { return g_config_monitor; }

cxxime::Config get_config() {
    return g_config;
}

void reload_global_config() {
    g_config = cxxime::Config();
    g_config.load(cxxime::data_path("default.json"));
    g_config.load(cxxime::user_data_path("default.json"));
    cxxime::set_diagnostics_config(g_config.diagnostics);
}

void init_config_monitor() {
    if (!g_config_monitor) return;
    reload_global_config();
    g_config_monitor->start(reload_global_config);
}

void add_config_monitor_ref() {
    if (g_config_monitor)
        g_config_monitor->add_ref();
}

void release_config_monitor_ref() {
    if (g_config_monitor) g_config_monitor->dec_ref();
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        g_hInst = hInst;
        cxxime::set_module_handle(hInst);
        DisableThreadLibraryCalls(hInst);
        InitializeCriticalSection(&g_cs);
        g_config_monitor = new cxxime::ConfigMonitor();
        g_config_monitor->initialize();
        g_config_monitor->add_ref();
        break;
    case DLL_PROCESS_DETACH:
        // Destroy all lingering status windows BEFORE other cleanup.
        cxxime::StatusWindow::cleanup_all();

        // Refcount manages lifecycle — dec_ref, don't delete.
        g_config_monitor->dec_ref();
        TextService::shutdown_trace(); // Flush and close trace writer thread
        DeleteCriticalSection(&g_cs);
        break;
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;

    if (!IsEqualCLSID(rclsid, c_clsidTextService)) return CLASS_E_CLASSNOTAVAILABLE;

    EnterCriticalSection(&g_cs);

    static ClassFactory s_classFactory;
    HRESULT hr = s_classFactory.QueryInterface(riid, ppv);

    LeaveCriticalSection(&g_cs);
    return hr;
}

STDAPI DllCanUnloadNow() { return g_cRefDll >= 0 ? S_FALSE : S_OK; }

STDAPI DllRegisterServer() {
    HRESULT hr;

    hr = register_server();
    if (FAILED(hr)) goto cleanup;

    hr = register_profiles();
    if (FAILED(hr)) goto cleanup;

    hr = register_categories();
    if (FAILED(hr)) goto cleanup;

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
