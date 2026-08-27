// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/installer_tsf.h>

#include <string>

#include <windows.h>
#include <msctf.h>
#include <objbase.h>

#include <cxxime/text_service_profile.h>

namespace cxxime {
namespace installer {

int release_input_processor_with_timeout() {
    wchar_t executable_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable_path, ARRAYSIZE(executable_path));
    if (length == 0 || length >= ARRAYSIZE(executable_path)) {
        return 1;
    }

    std::wstring command_line =
        L"\"" + std::wstring(executable_path, length) + L"\" release-worker";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(executable_path, command_line.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return 1;
    }

    // Keep the installer responsive if TSF blocks on an unresponsive host. Normal release
    // completes synchronously; this timeout only bounds the dedicated worker process.
    constexpr DWORD kReleaseTimeoutMs = 3000;
    const DWORD wait_result = WaitForSingleObject(process.hProcess, kReleaseTimeoutMs);
    DWORD exit_code = 1;
    if (wait_result == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exit_code);
    } else {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return wait_result == WAIT_OBJECT_0 && exit_code == 0 ? 0 : 1;
}

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
