// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "server_app.h"
#include <cxxime/data_path.h>
#include <string>
#include <shellapi.h>

static std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

static std::string get_arg(int argc, LPWSTR* argv, const std::wstring& flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (argv[i] == flag)
            return wide_to_utf8(argv[i + 1]);
    }
    return {};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::string dict_path;
    std::string config_path;
    if (argv) {
        // --data sets base data directory (overrides compile-time/default path)
        std::string data_dir = get_arg(argc, argv, L"--data");
        if (!data_dir.empty())
            cxxime::set_data_dir(data_dir);

        dict_path = get_arg(argc, argv, L"--dict");
        config_path = get_arg(argc, argv, L"--config");
        LocalFree(argv);
    }

    ServerApp app;
    if (!app.initialize(dict_path, config_path))
        return 1;
    app.run();
    app.finalize();
    return 0;
}
