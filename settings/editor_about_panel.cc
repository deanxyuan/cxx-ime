// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <cwchar>

#include <string>
#include <thread>

#include <shellapi.h>
#include <windowsx.h>

#include <cxxime/version.h>

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {
namespace {

constexpr int kGiteeLinkId = 5002;
constexpr int kGitHubLinkId = 5003;
constexpr UINT kCopyLinkCommand = 1;
constexpr wchar_t kGiteeUrl[] = L"https://gitee.com/shadowyuan/cxx-ime";
constexpr wchar_t kGitHubUrl[] = L"https://github.com/deanxyuan/cxx-ime";

const wchar_t* about_link_url(UINT_PTR control_id) {
    switch (control_id) {
    case kGiteeLinkId:
        return kGiteeUrl;
    case kGitHubLinkId:
        return kGitHubUrl;
    default:
        return nullptr;
    }
}

bool copy_text_to_clipboard(HWND owner, const wchar_t* text) {
    const size_t character_count = wcslen(text) + 1;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, character_count * sizeof(wchar_t));
    if (!memory) {
        return false;
    }

    auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    wcscpy_s(destination, character_count, text);
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    bool copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    if (!copied) {
        GlobalFree(memory);
    }
    return copied;
}

LRESULT CALLBACK AboutLinkProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                               UINT_PTR subclass_id, DWORD_PTR) {
    if (message == WM_CONTEXTMENU) {
        const wchar_t* url = about_link_url(subclass_id);
        if (!url) {
            return 0;
        }

        POINT position = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (position.x == -1 && position.y == -1) {
            RECT rect = {};
            GetWindowRect(window, &rect);
            position.x = rect.left;
            position.y = rect.bottom;
        }

        HWND owner = GetAncestor(window, GA_ROOT);
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return 0;
        }
        AppendMenuW(menu, MF_STRING, kCopyLinkCommand, L"复制链接");
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                      position.x, position.y, 0, owner, nullptr);
        DestroyMenu(menu);
        if (command == kCopyLinkCommand && !copy_text_to_clipboard(owner, url)) {
            MessageBoxW(owner, L"复制链接失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        }
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, AboutLinkProc, subclass_id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

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

void EditorApp::create_about_panel(HWND panel, int panel_width) {
    const int top = kPanelPadTop;
    SetWindowSubclass(panel, PanelForwardProc, 5000, reinterpret_cast<DWORD_PTR>(hwnd_));
    hAboutTitleFont_ = CreateFontW(-S(kFontPt + 2), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto make_about_text = [&](const wchar_t* text, int y, int height, HFONT font) {
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       kPanelPadLeft, y, panel_width - kPanelPadLeft - S(8), height,
                                       panel, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    };
    hAboutTitle_ = make_about_text(L"CxxIME 输入法", top, S(28), hAboutTitleFont_);
    make_about_text(L"版本 " CXXIME_VERSION_WSTRING L" — Apache License 2.0", top + kRowH, kCtrlH,
                    get_font());
    make_about_text(L"轻量级 Windows TSF 输入法（拼音 / 五笔 / 混输）", top + kRowH * 2, kCtrlH,
                    get_font());
    HWND gitee_link = CreateWindowExW(
        0, WC_LINK,
        L"<a "
        L"href=\"https://gitee.com/shadowyuan/cxx-ime\">https://gitee.com/shadowyuan/cxx-ime</a>",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, kPanelPadLeft, top + kRowH * 3,
        panel_width - kPanelPadLeft - S(8), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGiteeLinkId)), GetModuleHandle(nullptr),
        nullptr);
    SendMessageW(gitee_link, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    SetWindowSubclass(gitee_link, AboutLinkProc, kGiteeLinkId, 0);
    HWND github_link = CreateWindowExW(
        0, WC_LINK,
        L"<a "
        L"href=\"https://github.com/deanxyuan/cxx-ime\">https://github.com/deanxyuan/cxx-ime</a>",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, kPanelPadLeft, top + kRowH * 4,
        panel_width - kPanelPadLeft - S(8), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGitHubLinkId)), GetModuleHandle(nullptr),
        nullptr);
    SendMessageW(github_link, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    SetWindowSubclass(github_link, AboutLinkProc, kGitHubLinkId, 0);
    HWND diagnostics = CreateWindowExW(
        0, L"BUTTON", L"导出诊断包", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kPanelPadLeft, top + kRowH * 6, S(120), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(5001)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(diagnostics, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
}

bool EditorApp::handle_about_command(int control_id, int notification) {
    if (control_id != 5001) {
        return false;
    }
    if (notification == BN_CLICKED) {
        export_diagnostics();
    }
    return true;
}

bool EditorApp::handle_about_notify(LPARAM notification) {
    auto* header = reinterpret_cast<LPNMHDR>(notification);
    const wchar_t* url = header ? about_link_url(header->idFrom) : nullptr;
    if (!url) {
        return false;
    }

    if (header->code == NM_CLICK || header->code == NM_RETURN) {
        HINSTANCE result = ShellExecuteW(hwnd_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            MessageBoxW(hwnd_, L"无法打开项目主页。", L"CxxIME", MB_OK | MB_ICONERROR);
        }
        return true;
    }
    return false;
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
