// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// status_window_tool — interactive visual test for StatusWindow redesign

#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <cxxime/status_window.h>
#include <cxxime/render_context.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>

static cxxime::StatusWindow g_window;
static cxxime::ButtonState g_state;      // current button states
static cxxime::Theme g_theme;            // current theme
static bool g_enabled = true;
static HWND g_parent = nullptr;
static bool g_dark_theme = true;

// ── Theme presets ────────────────────────────────────────────
static cxxime::Theme make_dark_status_theme() {
    cxxime::Theme t;
    t.background         = {30,  30,  30,  255};
    t.text               = {204, 204, 204, 255};
    t.border             = {50,  50,  50,  255};
    t.hilited_back       = {0,   120, 212, 255};
    t.hilited_text       = {255, 255, 255, 255};
    // status window colors
    t.status_active_back   = {0,   120, 212, 255};
    t.status_active_text   = {255, 255, 255, 255};
    t.status_inactive_back = {61,  61,  61,  200};
    t.status_inactive_text = {204, 204, 204, 255};
    t.status_separator     = {64,  64,  64,  255};
    t.status_logo_back     = {61,  61,  61,  120};
    t.status_logo_border   = {64,  64,  64,  150};
    return t;
}

static cxxime::Theme make_light_status_theme() {
    cxxime::Theme t;
    t.background         = {243, 243, 243, 255};
    t.text               = {51,  51,  51,  255};
    t.border             = {200, 200, 200, 255};
    t.hilited_back       = {0,   120, 212, 255};
    t.hilited_text       = {255, 255, 255, 255};
    // status window colors
    t.status_active_back   = {0,   120, 212, 255};
    t.status_active_text   = {255, 255, 255, 255};
    t.status_inactive_back = {232, 232, 232, 200};
    t.status_inactive_text = {51,  51,  51,  255};
    t.status_separator     = {212, 212, 212, 255};
    t.status_logo_back     = {232, 232, 232, 120};
    t.status_logo_border   = {212, 212, 212, 150};
    return t;
}

// ── Helpers ──────────────────────────────────────────────────
static void update_display() {
    g_window.update_state(g_state);
    g_window.show();

    wchar_t title[256];
    swprintf(title, 256,
        L"Status Window Tool [%s]  中:%s  全:%s  。:%s  %s",
        g_dark_theme ? L"Dark" : L"Light",
        g_state.chinese_mode  ? L"中" : L"EN",
        g_state.full_shape    ? L"全" : L"半",
        g_state.chinese_punct ? L"。": L".",
        g_enabled ? L"" : L"(DISABLED)");
    if (g_parent) SetWindowTextW(g_parent, title);
}

static void print_state() {
    printf("\xe7\x8a\xb6\xe6\x80\x81: \xe4\xb8\xad=%s  \xe5\x85\xa8=%s  \xe3\x80\x82=%s  %s\n",
           g_state.chinese_mode  ? "\xe4\xb8\xad" : "EN",
           g_state.full_shape    ? "\xe5\x85\xa8" : "\xe5\x8d\x8a",
           g_state.chinese_punct ? "\xe3\x80\x82" : ".",
           g_enabled ? "" : "[IPC\xe6\x96\xad\xe5\xbc\x80]");
}

// ── Click callback ───────────────────────────────────────────
static void on_button_click(cxxime::StatusButton btn) {
    switch (btn) {
    case cxxime::StatusButton::CHINESE_MODE:
        g_state.chinese_mode = !g_state.chinese_mode;
        printf("\xe2\x86\x92 \xe5\x88\x87\xe6\x8d\xa2: %s\n", g_state.chinese_mode ? "\xe4\xb8\xad\xe6\x96\x87\xe6\xa8\xa1\xe5\xbc\x8f" : "\xe8\x8b\xb1\xe6\x96\x87\xe6\xa8\xa1\xe5\xbc\x8f");
        break;
    case cxxime::StatusButton::FULL_SHAPE:
        g_state.full_shape = !g_state.full_shape;
        printf("\xe2\x86\x92 \xe5\x88\x87\xe6\x8d\xa2: %s\n", g_state.full_shape ? "\xe5\x85\xa8\xe8\xa7\x92" : "\xe5\x8d\x8a\xe8\xa7\x92");
        break;
    case cxxime::StatusButton::CHINESE_PUNCT:
        g_state.chinese_punct = !g_state.chinese_punct;
        printf("\xe2\x86\x92 \xe5\x88\x87\xe6\x8d\xa2: %s\n", g_state.chinese_punct ? "\xe4\xb8\xad\xe6\x96\x87\xe6\xa0\x87\xe7\x82\xb9" : "\xe8\x8b\xb1\xe6\x96\x87\xe6\xa0\x87\xe7\x82\xb9");
        break;
    case cxxime::StatusButton::SETTINGS:
        printf("\xe2\x86\x92 \xe8\xae\xbe\xe7\xbd\xae\xe6\x8c\x89\xe9\x92\xae\xe8\xa2\xab\xe7\x82\xb9\xe5\x87\xbb (\xe5\x9c\xa8\xe6\xad\xa3\xe5\xbc\x8f\xe7\x89\x88\xe6\x9c\xac\xe4\xb8\xad\xe4\xbc\x9a\xe5\x90\xaf\xe5\x8a\xa8\xe8\xae\xbe\xe7\xbd\xae\xe7\xa8\x8b\xe5\xba\x8f)\n");
        break;
    }
    update_display();
}

// ── Window procedure ─────────────────────────────────────────
static LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN: {
        int vk = (int)wp;
        switch (vk) {
        case '1':
            g_state.chinese_mode = !g_state.chinese_mode;
            print_state(); update_display(); break;
        case '2':
            g_state.full_shape = !g_state.full_shape;
            print_state(); update_display(); break;
        case '3':
            g_state.chinese_punct = !g_state.chinese_punct;
            print_state(); update_display(); break;
        case 'E':
            g_enabled = !g_enabled;
            g_window.set_enabled(g_enabled);
            printf("\xe2\x86\x92 IPC\xe7\x8a\xb6\xe6\x80\x81: %s\n", g_enabled ? "\xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5" : "\xe5\xb7\xb2\xe6\x96\xad\xe5\xbc\x80");
            print_state(); update_display(); break;
        case 'T':
            g_dark_theme = !g_dark_theme;
            g_theme = g_dark_theme ? make_dark_status_theme() : make_light_status_theme();
            printf("\xe2\x86\x92 \xe4\xb8\xbb\xe9\xa2\x98: %s\n", g_dark_theme ? "\xe6\x9a\x97\xe8\x89\xb2" : "\xe4\xba\xae\xe8\x89\xb2");
            // Re-create window to apply theme
            g_window.destroy();
            g_window.create(nullptr, g_theme);
            g_window.set_click_callback(on_button_click);
            update_display(); break;
        case VK_LEFT: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x - 10, y);
            printf("\xe2\x86\x92 \xe4\xbd\x8d\xe7\xbd\xae: (%d, %d)\n", x - 10, y); break;
        }
        case VK_RIGHT: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x + 10, y);
            printf("\xe2\x86\x92 \xe4\xbd\x8d\xe7\xbd\xae: (%d, %d)\n", x + 10, y); break;
        }
        case VK_UP: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x, y - 10);
            printf("\xe2\x86\x92 \xe4\xbd\x8d\xe7\xbd\xae: (%d, %d)\n", x, y - 10); break;
        }
        case VK_DOWN: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x, y + 10);
            printf("\xe2\x86\x92 \xe4\xbd\x8d\xe7\xbd\xae: (%d, %d)\n", x, y + 10); break;
        }
        case VK_ESCAPE:
            printf("\xe9\x80\x80\xe5\x87\xba\n");
            PostQuitMessage(0); break;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Entry point ──────────────────────────────────────────────
int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("=== CxxIME Status Window Tool ===\n");
    printf("Keys:  1     = Toggle \xe4\xb8\xad/EN\n");
    printf("       2     = Toggle \xe5\x85\xa8/\xe5\x8d\x8a\n");
    printf("       3     = Toggle \xe3\x80\x82/.\n");
    printf("       E     = Toggle IPC connected/disconnected\n");
    printf("       T     = Toggle dark/light theme\n");
    printf("       Arrow = Move window\n");
    printf("       Esc   = Exit\n\n");
    printf("Click on buttons to toggle state.\n\n");

    // Create parent window (keyboard focus)
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ParentWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CxxIMEStatusToolParent";
    RegisterClassExW(&wc);

    g_parent = CreateWindowExW(0, L"CxxIMEStatusToolParent",
        L"Status Window Tool",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 120,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    // Initialize theme (dark by default)
    g_theme = make_dark_status_theme();

    // Create status window
    g_window.create(nullptr, g_theme);
    g_window.set_click_callback(on_button_click);

    // Initial display
    update_display();
    print_state();
    ShowWindow(g_parent, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_window.destroy();
    return 0;
}
