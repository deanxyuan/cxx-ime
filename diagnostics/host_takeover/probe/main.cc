// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"
#include "window_owner_probe.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int) {
    if (command_line && wcsstr(command_line, L"--window-owner")) {
        cxxime_probe::WindowOwnerProbe probe;
        if (!probe.initialize(instance)) {
            MessageBoxW(nullptr, probe.initialization_error().c_str(), L"CxxIME Probe",
                        MB_OK | MB_ICONERROR);
            probe.shutdown();
            return 1;
        }
        const int result = probe.run();
        probe.shutdown();
        return result;
    }

    cxxime_probe::ProbeApp app;
    if (!app.initialize(instance)) {
        MessageBoxW(nullptr, app.initialization_error().c_str(), L"CxxIME Probe",
                    MB_OK | MB_ICONERROR);
        app.shutdown();
        return 1;
    }
    const int result = app.run();
    app.shutdown();
    return result;
}
