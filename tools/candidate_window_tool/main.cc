// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// candidate_window_tool — interactive visual test for CandidateWindow

#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <cxxime/candidate_window.h>
#include <cxxime/candidate.h>
#include <cxxime/render_context.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>

static cxxime::CandidateWindow g_window;
static cxxime::CandidatePage g_page;
static bool g_d2d = false;  // will be set from config after load
static cxxime::Config g_config;
static int g_page_idx = 0;
static int g_total_pages = 3;
static HWND g_parent = nullptr;
static int g_font_size_idx = 1;  // index into {12, 14, 16, 18, 20}, start at 14
static const int g_font_sizes[] = {12, 14, 16, 18, 20};

static void update_display();

// Sample candidate data
static std::vector<std::vector<std::string>> g_pages = {
    {"你好", "您好", "昵称", "尼采", "拟态", "腻烦", "匿藏"},
    {"世界", "师姐", "时间", "实践", "事件", "世间", "时节"},
    {"中国", "种过", "忠果", "终过", "重过", "中锅", "种锅"},
};

static void build_page() {
    g_page.candidates.clear();
    auto& words = g_pages[g_page_idx];
    for (size_t i = 0; i < words.size(); ++i) {
        cxxime::Candidate c;
        c.text = words[i];
        g_page.candidates.push_back(c);
    }
    g_page.highlighted = 0;
    g_page.page_size = 7;
}

static void handle_click(int index) {
    if (index == -2) {
        if (g_page_idx > 0) { g_page_idx--; build_page(); update_display(); }
    } else if (index == -3) {
        if (g_page_idx + 1 < g_total_pages) { g_page_idx++; build_page(); update_display(); }
    } else if (index >= 0 && index < (int)g_page.candidates.size()) {
        printf("Clicked: %s\n", g_page.candidates[index].text.c_str());
        g_window.hide();
    }
}

static void update_display() {
    g_window.set_page_info(g_page_idx + 1, g_total_pages);
    g_window.update(g_page);
    RECT fakeCaret = {400, 300, 400, 320};
    g_window.move_to_caret(fakeCaret);
    g_window.show();
    wchar_t title[256];
    swprintf(title, 256, L"Candidate Window Tool [%s] 主题:%S 布局:%S 字体:%dpx",
             g_d2d ? L"D2D" : L"GDI",
             g_config.theme.c_str(),
             g_config.layout.c_str(),
             g_font_sizes[g_font_size_idx]);
    if (g_parent) SetWindowTextW(g_parent, title);
}

static LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN: {
        int vk = (int)wp;
        if (vk >= '1' && vk <= '9') {
            int idx = vk - '1';
            if (idx < (int)g_page.candidates.size()) {
                printf("Selected: %s\n", g_page.candidates[idx].text.c_str());
                g_window.hide();
            }
        } else if (vk == VK_ESCAPE) {
            printf("Escape — hiding window\n");
            g_window.hide();
        } else if (vk == VK_NEXT) {
            if (g_page_idx + 1 < g_total_pages) {
                g_page_idx++;
                build_page();
                update_display();
            }
        } else if (vk == VK_PRIOR) {
            if (g_page_idx > 0) {
                g_page_idx--;
                build_page();
                update_display();
            }
        } else if (vk == VK_SPACE) {
            printf("Committed: %s\n", g_page.candidates[g_page.highlighted].text.c_str());
            g_window.hide();
        } else if (vk == 'T') {
            static int ti = -1;  // first press skips default azure
            static const char* schemes[] = {
                "aqua", "dark_temple", "luna", "ps4",
                "google", "lost_temple", "starcraft", "azure",
                "solarized_rock", "dota_2", "modern_warfare", "steam", "metroblue"
            };
            ti = (ti + 1) % 13;
            g_config.theme = schemes[ti];
            auto tm = cxxime::build_theme_from_config(g_config);
            printf("T=%s bg=(%d,%d,%d) hl=(%d,%d,%d)\n", schemes[ti],
                   tm.background.r, tm.background.g, tm.background.b,
                   tm.hilited_back.r, tm.hilited_back.g, tm.hilited_back.b);
            g_window.hide();
            g_window.set_theme(tm);
            update_display();
        } else if (vk == 'L') {
            // Toggle layout
            static bool vert = false;
            vert = !vert;
            g_window.set_layout(vert ? "vertical" : "horizontal");
            printf("Layout: %s\n", vert ? "vertical" : "horizontal");
            update_display();
        } else if (vk == 'D') {
            g_d2d = !g_d2d;
            g_window.set_render_backend(g_d2d ? cxxime::RenderBackend::D2D : cxxime::RenderBackend::GDI);
            printf("渲染器: %s  主题: %s  布局: %s  字体: %dpx\n",
                   g_d2d ? "D2D" : "GDI",
                   g_config.theme.c_str(),
                   g_config.layout.c_str(),
                   g_font_sizes[g_font_size_idx]);
            update_display();
        } else if (vk == 'F') {
            g_font_size_idx = (g_font_size_idx + 1) % 5;
            int fs = g_font_sizes[g_font_size_idx];
            g_config.font_size = fs;
            printf("字体大小: %dpx\n", fs);
            g_window.destroy();
            g_window.create(nullptr, g_config);
            g_window.set_click_callback(handle_click);
            g_window.set_layout(g_config.layout);
            build_page();
            if (g_d2d) g_window.set_render_backend(cxxime::RenderBackend::D2D);
            update_display();
        } else if (vk == 'P') {
            static bool show_preedit = true;
            show_preedit = !show_preedit;
            g_window.set_preedit(show_preedit ? "ni'hao" : "");
            printf("Preedit: %s\n", show_preedit ? "on" : "off");
            update_display();
        } else {
            // Letters simulate pinyin input
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

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("=== Candidate Window Test Tool ===\n");
    printf("Keys: 1-9=select  Space=commit  Esc=hide\n");
    printf("      PageUp/PageDown=flip  T=theme  L=layout  D=D2D/GDI  F=font  P=preedit\n");
    printf("      (click on window to select candidates)\n\n");

    // Create hidden parent window
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ParentWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CxxIMEToolParent";
    RegisterClassExW(&wc);

    HWND parent = CreateWindowExW(0, L"CxxIMEToolParent", L"Candidate Window Tool",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  400, 100, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    // Create candidate window, load themes
    g_config.load(cxxime::data_path("default.json"));
    g_config.load_themes(cxxime::data_path("themes.json"));
    g_config.page_size = 7;
    g_d2d = (g_config.render_backend != "gdi");
    g_window.create(nullptr, g_config);

    g_window.set_click_callback(handle_click);

    build_page();
    g_window.set_preedit("ni'hao");
    update_display();

    g_parent = parent;
    ShowWindow(parent, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_window.destroy();
    return 0;
}
