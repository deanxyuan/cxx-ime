// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tray_icon.h"

bool TrayIcon::create(HWND hwnd) {
    hwnd_ = hwnd;
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_TIP;
    nid_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid_.szTip, L"CxxIME Server");
    return Shell_NotifyIconW(NIM_ADD, &nid_);
}

void TrayIcon::destroy() {
    Shell_NotifyIconW(NIM_DELETE, &nid_);
}

void TrayIcon::show_balloon(const wchar_t* title, const wchar_t* text) {
    nid_.uFlags = NIF_INFO;
    wcscpy_s(nid_.szInfoTitle, title);
    wcscpy_s(nid_.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.uFlags = NIF_ICON | NIF_TIP;
}
