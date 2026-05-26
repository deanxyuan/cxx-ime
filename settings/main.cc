// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"
#include <shellscalingapi.h>
#include <shellapi.h>
#include <cxxime/data_path.h>

#pragma comment(lib, "shcore.lib")

static float get_dpi_scale() {
    HDC dc = GetDC(nullptr);
    float s = GetDeviceCaps(dc, LOGPIXELSY) / 96.0f;
    ReleaseDC(nullptr, dc);
    return s;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    float dpiScale = get_dpi_scale();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc - 1; ++i) {
            if (wcscmp(argv[i], L"--data") == 0 && i + 1 < argc) {
                std::string dir;
                int len = WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1,
                                              nullptr, 0, nullptr, nullptr);
                if (len > 1) {
                    dir.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1,
                                       &dir[0], len, nullptr, nullptr);
                }
                cxxime::set_data_dir(dir);
                break;
            }
        }
        LocalFree(argv);
    }

    return cxxime::settings::EditorApp::run(hInst, dpiScale);
}
