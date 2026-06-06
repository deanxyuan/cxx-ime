// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// status_window_tool — interactive visual test for StatusWindow redesign

#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <cxxime/status_window.h>
#include <cxxime/render_context.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>

static cxxime::StatusWindow g_window;
static cxxime::ButtonState g_state;
static cxxime::StatusTheme g_theme;
static bool g_enabled = true;
static HWND g_parent = nullptr;

// ── Helpers ──────────────────────────────────────────────────
static void update_display() {
    g_window.update_state(g_state);
    g_window.show();

    wchar_t title[256];
    swprintf(title, 256,
        L"中:%s 全:%s 。:%s %s",
        g_state.chinese_mode  ? L"中" : L"英",
        g_state.full_shape    ? L"全" : L"半",
        g_state.chinese_punct ? L"。": L".",
        g_enabled ? L"" : L"(DISABLED)");
    if (g_parent) SetWindowTextW(g_parent, title);
}

static void print_state() {
    printf("状态: 中=%s  全=%s  。=%s  %s\n",
           g_state.chinese_mode  ? "中" : "英",
           g_state.full_shape    ? "全" : "半",
           g_state.chinese_punct ? "。" : ".",
           g_enabled ? "" : "[IPC断开]");
}

// ── Click callback ───────────────────────────────────────────
static void on_button_click(cxxime::StatusButton btn) {
    switch (btn) {
    case cxxime::StatusButton::CHINESE_MODE:
        g_state.chinese_mode = !g_state.chinese_mode;
        printf("→ 切换: %s\n", g_state.chinese_mode ? "中文模式" : "英文模式");
        break;
    case cxxime::StatusButton::FULL_SHAPE:
        g_state.full_shape = !g_state.full_shape;
        printf("→ 切换: %s\n", g_state.full_shape ? "全角" : "半角");
        break;
    case cxxime::StatusButton::CHINESE_PUNCT:
        g_state.chinese_punct = !g_state.chinese_punct;
        printf("→ 切换: %s\n", g_state.chinese_punct ? "中文标点" : "英文标点");
        break;
    case cxxime::StatusButton::SETTINGS:
        printf("→ 设置按键被点击 (在正式版本中会启动设置程序)\n");
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
            printf("→ IPC状态: %s\n", g_enabled ? "已连接" : "已断开");
            print_state(); update_display(); break;
        case VK_LEFT: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x - 10, y);
            printf("→ 位置: (%d, %d)\n", x - 10, y); break;
        }
        case VK_RIGHT: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x + 10, y);
            printf("→ 位置: (%d, %d)\n", x + 10, y); break;
        }
        case VK_UP: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x, y - 10);
            printf("→ 位置: (%d, %d)\n", x, y - 10); break;
        }
        case VK_DOWN: {
            int x, y; g_window.get_position(x, y);
            g_window.set_position(x, y + 10);
            printf("→ 位置: (%d, %d)\n", x, y + 10); break;
        }
        case VK_ESCAPE:
            printf("退出\n");
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
    printf("Keys:  1     = Toggle 中/英\n");
    printf("       2     = Toggle 全/半\n");
    printf("       3     = Toggle 。/.\n");
    printf("       E     = Toggle IPC connected/disconnected\n");
    printf("       Arrow = Move window\n");
    printf("       Esc   = Exit\n\n");
    printf("Click on buttons to toggle state.\n\n");

    // Load logo icon
    HICON logo_icon = (HICON)LoadImageW(nullptr,
        CXXIME_PROJECT_DIR L"resource/freedly.ico",
        IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);

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

    // Create status window with logo icon
    g_window.create(nullptr, g_theme);
    if (logo_icon) g_window.set_logo_icon(logo_icon);
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
    if (logo_icon) DestroyIcon(logo_icon);
    return 0;
}
