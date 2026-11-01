// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cmath>
#include <vector>

#include <windows.h>
#include <commctrl.h>

#include <cxxime/control_server.h>
#include <cxxime/custom_phrases.h>

#include "custom_phrase_panel.h"
#include "editor_app_internal.h"
#include "session_manager_integration_test_support.h"
#include "support/dpi_testutil.h"

TEST(CustomPhrasePanel, native_controls_save_literals_and_reorder_through_isolated_transport) {
    const std::string path = test_user_data_dir + "\\custom_phrases.tsv";
    DeleteFileA(path.c_str());
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    cxxime::ControlServer server;
    const std::wstring pipe =
        L"\\\\.\\pipe\\CxxIME-PhrasePanel-Test-" + std::to_wstring(GetCurrentProcessId());
    ASSERT_TRUE(server.start(
        "{}", {},
        [&](cxxime::ControlMessageType type, const std::string& request,
            cxxime::ControlMessageType* response_type, std::string* response) {
            if (type != cxxime::ControlMessageType::kLexiconRequest) {
                return false;
            }
            *response_type = cxxime::ControlMessageType::kLexiconResult;
            return handle_lexicon_control_request(manager, request, response);
        },
        pipe));
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
    ASSERT_TRUE(InitCommonControlsEx(&controls));
    cxxime::settings::init_layout();
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 520, 340, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(parent != nullptr);
    cxxime::settings::CustomPhrasePanel panel(cxxime::LexiconControlClient(1500, pipe));
    panel.create(parent, 0, 0, 500, 320);
    ASSERT_TRUE(panel.hwnd() != nullptr);
    panel.refresh();
    auto control = [&](int id) { return GetDlgItem(panel.hwnd(), id); };
    auto click = [&](int id) { SendMessageW(control(id), BM_CLICK, 0, 0); };
    SetWindowTextW(control(4), L"yx");
    SetWindowTextW(control(5), L"abc@qq.com");
    ASSERT_TRUE(IsWindowEnabled(control(6)));
    click(6);
    ASSERT_EQ(manager.custom_phrases()->lookup("yx").front(), "abc@qq.com");
    ASSERT_TRUE(!IsWindowEnabled(control(7)));
    ASSERT_TRUE(!IsWindowEnabled(control(9)));
    ASSERT_TRUE(!IsWindowEnabled(control(10)));
    SetWindowTextW(control(5), L"second@example.com");
    ASSERT_TRUE(IsWindowEnabled(control(7)));
    click(6);
    ASSERT_EQ(ListView_GetItemCount(control(3)), 2);
    ASSERT_TRUE(IsWindowEnabled(control(9)));
    click(9);
    ASSERT_EQ(manager.custom_phrases()->lookup("yx").front(), "second@example.com");
    ASSERT_TRUE(!IsWindowEnabled(control(9)));
    ASSERT_TRUE(IsWindowEnabled(control(10)));
    SetWindowTextW(control(5), L"updated@example.com");
    click(7);
    ASSERT_EQ(manager.custom_phrases()->lookup("yx").front(), "updated@example.com");
    SetWindowTextW(control(4), L"Y1");
    ASSERT_TRUE(!IsWindowEnabled(control(6)));
    ASSERT_TRUE(!IsWindowEnabled(control(7)));
    SetWindowTextW(control(1), L"abc@");
    ASSERT_TRUE((SendMessageW(control(1), WM_GETDLGCODE, VK_RETURN, 0) & DLGC_WANTALLKEYS) != 0);
    SendMessageW(control(1), WM_KEYDOWN, VK_RETURN, 0);
    ASSERT_EQ(ListView_GetItemCount(control(3)), 1);
    ListView_SetItemState(control(3), 0, LVIS_SELECTED, LVIS_SELECTED);
    ASSERT_TRUE(IsWindowEnabled(control(9)));
    ASSERT_TRUE(!IsWindowEnabled(control(10)));

    // All records are legal, but a 128-row JSON response would exceed the pipe payload limit.
    const std::string escaped = std::string(126, '"') + std::string(126, '\\');
    std::string imported;
    for (int index = 0; index < 128; ++index) {
        imported += "long\t" + escaped + std::to_string(100 + index) + '\n';
    }
    ASSERT_TRUE(manager.custom_phrases()->merge_contents(imported, nullptr));
    cxxime::LexiconControlRequest request;
    request.operation = cxxime::LexiconOperation::kQuery;
    request.query = "long";
    request.limit = cxxime::LEXICON_CONTROL_MAX_LIMIT;
    cxxime::LexiconControlResult result;
    ASSERT_TRUE(cxxime::LexiconControlClient(1500, pipe).custom_phrase_request(request, &result));
    ASSERT_EQ(result.query.entries.size(), cxxime::LEXICON_CONTROL_DEFAULT_LIMIT);
    ASSERT_TRUE(result.query.has_more);
    ASSERT_EQ(result.query.entries[0].text, escaped + "100");

    SetWindowTextW(control(1), L"long");
    click(2);
    const int page_size = static_cast<int>(cxxime::LEXICON_CONTROL_DEFAULT_LIMIT);
    for (int offset = 0; offset < 128; offset += page_size) {
        ASSERT_EQ(ListView_GetItemCount(control(3)), page_size);
        wchar_t first_text[256] = {};
        ListView_GetItemText(control(3), 0, 2, first_text, 256);
        ASSERT_EQ(std::wstring(first_text),
                  cxxime::settings::utf8_to_wstr(escaped + std::to_string(100 + offset)));
        ASSERT_TRUE(IsWindowEnabled(control(13)));
        ASSERT_EQ(IsWindowEnabled(control(15)) != FALSE, offset + page_size < 128);
        if (offset + page_size < 128) {
            click(15);
        }
    }
    click(14);
    wchar_t previous_text[256] = {};
    ListView_GetItemText(control(3), 0, 2, previous_text, 256);
    ASSERT_EQ(std::wstring(previous_text),
              cxxime::settings::utf8_to_wstr(escaped + std::to_string(100 + 128 - 2 * page_size)));
    DestroyWindow(parent);
    server.stop();
    DeleteFileA(path.c_str());
}

TEST(CustomPhrasePanel, dpi_scaling_updates_visible_and_hidden_nested_controls_and_columns) {
    test::ScopedDpiAwarenessContext dpi;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
    ASSERT_TRUE(InitCommonControlsEx(&controls));
    cxxime::settings::init_layout();
    HWND root = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1100, 700, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(root != nullptr);
    // The existing dictionary pages and the new page are sibling layout containers.
    HWND dictionary = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"", WS_CHILD, 8, 40, 500,
                                      320, root, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND query = CreateWindowExW(0, L"EDIT", L"existing query", WS_CHILD | WS_VISIBLE, 8, 0, 320,
                                 28, dictionary, nullptr, GetModuleHandleW(nullptr), nullptr);
    cxxime::settings::CustomPhrasePanel panel;
    panel.create(root, 8, 40, 500, 320);
    ShowWindow(panel.hwnd(), SW_SHOW);
    ASSERT_TRUE(dictionary != nullptr && query != nullptr && panel.hwnd() != nullptr);

    auto bounds = [](HWND window) {
        RECT rect = {};
        ASSERT_TRUE(GetWindowRect(window, &rect));
        MapWindowPoints(nullptr, GetParent(window), reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    };
    const std::vector<HWND> windows = {dictionary,
                                       query,
                                       panel.hwnd(),
                                       GetDlgItem(panel.hwnd(), 1),
                                       GetDlgItem(panel.hwnd(), 3),
                                       GetDlgItem(panel.hwnd(), 4),
                                       GetDlgItem(panel.hwnd(), 6)};
    std::vector<RECT> original;
    for (HWND window : windows) {
        original.push_back(bounds(window));
    }
    HWND list = GetDlgItem(panel.hwnd(), 3);
    const int column_width = ListView_GetColumnWidth(list, 0);
    LOGFONTW font_description = {};
    ASSERT_TRUE(
        GetObjectW(cxxime::settings::get_font(), sizeof(font_description), &font_description));
    const int initial_font_height = font_description.lfHeight;
    std::vector<HFONT> fonts;
    float previous_scale = 1.0f;
    for (float scale : {1.25f, 1.5f, 1.0f}) {
        font_description.lfHeight = static_cast<LONG>(std::lround(initial_font_height * scale));
        HFONT font = CreateFontIndirectW(&font_description);
        ASSERT_TRUE(font != nullptr);
        fonts.push_back(font);
        cxxime::settings::scale_panel_children(root, scale / previous_scale, font);
        for (std::size_t index = 0; index < windows.size(); ++index) {
            const RECT current = bounds(windows[index]);
            const RECT before = original[index];
            ASSERT_LE(std::abs(current.left - std::lround(before.left * scale)), 2L);
            ASSERT_LE(std::abs(current.top - std::lround(before.top * scale)), 2L);
            ASSERT_LE(std::abs(current.right - current.left -
                               std::lround((before.right - before.left) * scale)),
                      2L);
            ASSERT_LE(std::abs(current.bottom - current.top -
                               std::lround((before.bottom - before.top) * scale)),
                      2L);
        }
        for (HWND control : {query, GetDlgItem(panel.hwnd(), 1), list, GetDlgItem(panel.hwnd(), 4),
                             GetDlgItem(panel.hwnd(), 6)}) {
            ASSERT_EQ(reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)), font);
        }
        ASSERT_LE(std::abs(ListView_GetColumnWidth(list, 0) - std::lround(column_width * scale)),
                  2L);
        ASSERT_TRUE((GetWindowLongPtrW(dictionary, GWL_STYLE) & WS_VISIBLE) == 0);
        ASSERT_TRUE((GetWindowLongPtrW(panel.hwnd(), GWL_STYLE) & WS_VISIBLE) != 0);
        previous_scale = scale;
    }
    DestroyWindow(root);
    for (HFONT font : fonts) {
        DeleteObject(font);
    }
}
