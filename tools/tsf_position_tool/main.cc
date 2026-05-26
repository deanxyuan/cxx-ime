// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// tsf_position_tool — validates caret-to-window positioning logic

#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <cxxime/candidate_window.h>
#include <cxxime/candidate.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>

static cxxime::CandidateWindow g_window;
static cxxime::CandidatePage g_page;
static cxxime::Config g_config;
static RECT g_caret = {400, 300, 400, 320};

static void build_page() {
    g_page.candidates.clear();
    for (int i = 0; i < 7; ++i) {
        cxxime::Candidate c;
        char buf[16];
        snprintf(buf, sizeof(buf), "test_%d", i + 1);
        c.text = buf;
        g_page.candidates.push_back(c);
    }
    g_page.highlighted = 0;
}

static void update_display() {
    build_page();
    g_window.update(g_page);
    g_window.move_to_caret(g_caret);
    g_window.show();
    wchar_t title[256];
    swprintf(title, 256, L"TSF Position Tool  caret=(%d,%d,%d,%d)  window@%hs",
             g_caret.left, g_caret.top, g_caret.right, g_caret.bottom,
             g_config.layout.c_str());
    SetConsoleTitleW(title);
    printf("caret=(%d,%d,%d,%d) | layout=%s | theme=%s\n",
           g_caret.left, g_caret.top, g_caret.right, g_caret.bottom,
           g_config.layout.c_str(), g_config.theme.c_str());
}

static LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN: {
        int vk = (int)wp;
        // Eat modifier keys to prevent DefWindowProc from interpreting them
        if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU)
            return 0;
        int step = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 20 : 2;
        bool moved = false;
        switch (vk) {
        case VK_LEFT:  g_caret.left -= step; g_caret.right -= step; moved = true; break;
        case VK_RIGHT: g_caret.left += step; g_caret.right += step; moved = true; break;
        case VK_UP:    g_caret.top -= step; g_caret.bottom -= step; moved = true; break;
        case VK_DOWN:  g_caret.top += step; g_caret.bottom += step; moved = true; break;
        case 'L':
            { static bool vert = false; vert = !vert;
              g_window.set_layout(vert ? "vertical" : "horizontal"); }
            break;
        case 'T':
            { static int ti = 0;
              static const char* schemes[] = {"azure","aqua","luna","dark_temple",
                  "google","starcraft","solarized_rock","metroblue",nullptr};
              if (!schemes[ti]) ti = 0;
              g_config.theme = schemes[ti++];
              auto tm = cxxime::build_theme_from_config(g_config);
              g_window.set_theme(tm); }
            break;
        case 'D':
            { static bool d2d = true; d2d = !d2d;
              printf("Renderer: %s\n", d2d ? "D2D" : "GDI");
              g_window.set_render_backend(d2d
                  ? cxxime::RenderBackend::D2D : cxxime::RenderBackend::GDI); }
            break;
        case VK_ESCAPE: g_window.hide(); return 0;
        case VK_SPACE:  break;
        case '1': case '2': case '3': case '4': case '5': case '6': case '7':
            printf("Selected: %s\n", g_page.candidates[vk - '1'].text.c_str());
            g_window.hide();
            return 0;
        default: return DefWindowProcW(hwnd, msg, wp, lp);
        }
        // Only reposition on caret movement or state changes (not on Space alone)
        update_display();
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("=== TSF Position Tool ===\n");
    printf("Arrow keys: move caret   Shift+Arrow: fast move\n");
    printf("L: toggle layout   T: cycle themes   D: toggle D2D/GDI\n");
    printf("Space: refresh   1-7: select   Esc: hide\n\n");

    g_config.load(cxxime::data_path("default.json"));
    g_config.load_themes(cxxime::data_path("themes.json"));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ParentWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"TsfPosToolParent";
    RegisterClassExW(&wc);

    HWND parent = CreateWindowExW(0, L"TsfPosToolParent", L"TSF Position Tool",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        400, 80, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    ShowWindow(parent, SW_SHOW);

    g_window.create(nullptr, g_config);
    g_window.set_click_callback([](int idx) {
        printf("Clicked: %d\n", idx);
        g_window.hide();
    });

    update_display();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_window.destroy();
    return 0;
}
