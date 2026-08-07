// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <string>
#include <thread>

#include <shellapi.h>

#include <cxxime/diagnostic_log_path.h>

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {
namespace {

constexpr int kDiagnosticsLoggingId = 6001;
constexpr int kExportDiagnosticsId = 6002;
constexpr int kOpenDiagnosticsDirectoryId = 6003;

std::wstring module_directory() {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        return {};
    }
    std::wstring directory(path);
    size_t separator = directory.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        directory.resize(separator);
    }
    return directory;
}

std::wstring find_collect_diagnostics_script() {
    std::wstring directory = module_directory();
    if (directory.empty()) {
        return {};
    }
    std::wstring script = directory + L"\\collect_diagnostics.ps1";
    DWORD attributes = GetFileAttributesW(script.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return script;
    }
    return {};
}

} // namespace

void EditorApp::create_diagnostics_panel(HWND panel) {
    const int top = kPanelPadTop;
    SetWindowSubclass(panel, PanelForwardProc, 6000, reinterpret_cast<DWORD_PTR>(hwnd_));

    hDiagnosticsLogging_ =
        make_check(kDiagnosticsLoggingId, L"启用诊断日志", kPanelPadLeft, top, S(180), panel);
    const int content_x = kPanelPadLeft + S(22);

    HWND privacy_notice = CreateWindowExW(
        0, L"STATIC", L"诊断日志可能包含输入编码。", WS_CHILD | WS_VISIBLE | SS_LEFT, content_x,
        top + kRowH, S(260), kCtrlH, panel, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(privacy_notice, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    const int directory_y = top + kRowH * 3;
    HWND open_directory_button = CreateWindowExW(
        0, L"BUTTON", L"打开常规日志目录", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kPanelPadLeft, directory_y, S(150), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenDiagnosticsDirectoryId)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(open_directory_button, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    const int packaged_app_y = directory_y + kRowH;
    HWND packaged_app_path =
        CreateWindowExW(0, L"STATIC", L"PackagedApp 日志目录: LocalState\\cxxime\\logs",
                        WS_CHILD | WS_VISIBLE | SS_LEFT, content_x, packaged_app_y, S(320), kCtrlH,
                        panel, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(packaged_app_path, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    HWND export_button = CreateWindowExW(
        0, L"BUTTON", L"导出诊断包", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kPanelPadLeft, top + kRowH * 6, S(150), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExportDiagnosticsId)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(export_button, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
}

bool EditorApp::handle_diagnostics_command(int control_id, int notification) {
    if (control_id == kExportDiagnosticsId && notification == BN_CLICKED) {
        export_diagnostics();
        return true;
    }
    if (control_id == kOpenDiagnosticsDirectoryId && notification == BN_CLICKED) {
        open_diagnostics_log_directory();
        return true;
    }
    return false;
}

void EditorApp::load_diagnostics_controls() {
    set_check(hDiagnosticsLogging_,
              config_.diagnostics.trace_mode != cxxime::DiagnosticTraceMode::kOff);
}

void EditorApp::read_diagnostics_controls() {
    if (!get_check(hDiagnosticsLogging_)) {
        config_.diagnostics.trace_mode = cxxime::DiagnosticTraceMode::kOff;
    } else if (config_.diagnostics.trace_mode == cxxime::DiagnosticTraceMode::kOff) {
        config_.diagnostics.trace_mode = cxxime::DiagnosticTraceMode::kNormal;
    }
}

void EditorApp::open_diagnostics_log_directory() {
    const std::wstring directory = cxxime::diagnostic_log_directory();
    if (directory.empty()) {
        MessageBoxW(hwnd_, L"无法确定诊断日志目录。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }

    HINSTANCE result =
        ShellExecuteW(hwnd_, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hwnd_, L"无法打开诊断日志目录。", L"CxxIME", MB_OK | MB_ICONERROR);
    }
}

void EditorApp::export_diagnostics() {
    std::wstring script = find_collect_diagnostics_script();
    if (script.empty()) {
        MessageBoxW(hwnd_, L"未找到 collect_diagnostics.ps1。请确认当前版本已完整安装。", L"CxxIME",
                    MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring directory = script;
    size_t separator = directory.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        directory.resize(separator);
    }

    std::wstring parameters = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script + L"\"";
    SHELLEXECUTEINFOW execute_info = {};
    execute_info.cbSize = sizeof(execute_info);
    execute_info.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute_info.hwnd = hwnd_;
    execute_info.lpVerb = L"open";
    execute_info.lpFile = L"powershell.exe";
    execute_info.lpParameters = parameters.c_str();
    execute_info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    execute_info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&execute_info)) {
        MessageBoxW(hwnd_, L"启动诊断导出失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(hwnd_, L"已开始导出诊断包，完成后会再次提示结果。", L"CxxIME",
                MB_OK | MB_ICONINFORMATION);

    if (execute_info.hProcess) {
        HWND window = hwnd_;
        HANDLE process = execute_info.hProcess;
        std::thread([window, process]() {
            WaitForSingleObject(process, INFINITE);
            DWORD exit_code = 1;
            GetExitCodeProcess(process, &exit_code);
            CloseHandle(process);
            if (IsWindow(window)) {
                PostMessageW(window, kDiagnosticsCompleteMessage, static_cast<WPARAM>(exit_code),
                             0);
            }
        }).detach();
    }
}

} // namespace settings
} // namespace cxxime
