// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include "server_app.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    ServerApp app;
    if (!app.initialize())
        return 1;
    app.run();
    app.finalize();
    return 0;
}
