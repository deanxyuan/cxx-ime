// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TRAY_ICON_H_
#define CXXIME_TRAY_ICON_H_

#include <windows.h>
#include <shellapi.h>

class TrayIcon {
public:
    bool create(HWND hwnd);
    void destroy();
    void show_balloon(const wchar_t* title, const wchar_t* text);

private:
    NOTIFYICONDATAW nid_ = {};
    HWND hwnd_ = nullptr;
};

#endif // CXXIME_TRAY_ICON_H_
