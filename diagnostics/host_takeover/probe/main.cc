// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    cxxime_probe::ProbeApp app;
    if (!app.initialize(instance)) {
        MessageBoxW(nullptr, L"Failed to initialize the IME host Probe.", L"CxxIME Probe",
                    MB_OK | MB_ICONERROR);
        app.shutdown();
        return 1;
    }
    const int result = app.run();
    app.shutdown();
    return result;
}
