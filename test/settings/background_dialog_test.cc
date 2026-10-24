// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>

#include "background_dialog.h"
#include "support/background_test_image.h"
#include "support/dpi_testutil.h"

namespace {

struct CanvasSnapshot {
    int width = 0;
    int height = 0;
    std::vector<BYTE> pixels;

    explicit CanvasSnapshot(HWND canvas) {
        RECT area{};
        ASSERT_TRUE(GetClientRect(canvas, &area));
        width = area.right;
        height = area.bottom;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        HDC dc = CreateCompatibleDC(nullptr);
        void* data = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        ASSERT_TRUE(dc && bitmap && data);
        auto old = SelectObject(dc, bitmap);
        SendMessageW(canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        GdiFlush();
        pixels.assign(static_cast<BYTE*>(data), static_cast<BYTE*>(data) + width * height * 4);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    COLORREF at(int x, int y) const {
        const auto offset = (y * width + x) * 4;
        return RGB(pixels[offset + 2], pixels[offset + 1], pixels[offset]);
    }

    RECT frame() const {
        RECT result{width, height, 0, 0};
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (at(x, y) == GetSysColor(COLOR_HIGHLIGHT)) {
                    result.left = (std::min)(result.left, LONG(x));
                    result.top = (std::min)(result.top, LONG(y));
                    result.right = (std::max)(result.right, LONG(x + 1));
                    result.bottom = (std::max)(result.bottom, LONG(y + 1));
                }
            }
        }
        ASSERT_LT(result.left, result.right);
        ASSERT_LT(result.top, result.bottom);
        return result;
    }
};

void check_plain_preview(HWND dialog, HWND canvas) {
    HWND scene = GetDlgItem(dialog, 3103);
    ASSERT_TRUE(IsWindowEnabled(scene));
    const auto original = SendMessageW(scene, CB_GETCURSEL, 0, 0);
    for (int layout : {0, 1}) {
        SendMessageW(scene, CB_SETCURSEL, layout, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(3103, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(scene));
        const CanvasSnapshot snapshot(canvas);
        const auto frame = snapshot.frame();
        ASSERT_EQ(frame.right - frame.left > frame.bottom - frame.top, layout == 0);
        bool has_content = false;
        const auto background = snapshot.at(frame.left + 4, frame.top + 4);
        for (int y = frame.top + 4; y < frame.bottom - 4; ++y) {
            for (int x = frame.left + 4; x < frame.right - 4; ++x) {
                has_content |= snapshot.at(x, y) != background;
            }
        }
        ASSERT_TRUE(has_content);
    }
    SendMessageW(scene, CB_SETCURSEL, original, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(3103, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(scene));
}

void check_canvas(HWND dialog, HWND canvas) {
    HWND scene = GetDlgItem(dialog, 3103);
    HWND zoom = GetDlgItem(dialog, 3104);
    HWND strength = GetDlgItem(dialog, 3123);
    ASSERT_EQ(SendMessageW(scene, CB_GETCOUNT, 0, 0), 2);
    for (int layout : {0, 1}) {
        SendMessageW(scene, CB_SETCURSEL, layout, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(3103, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(scene));
        SendMessageW(dialog, WM_COMMAND, 3101, 0);
        SendMessageW(strength, TBM_SETPOS, TRUE, 100);
        SendMessageW(dialog, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(strength));
        const CanvasSnapshot full(canvas);
        const auto frame = full.frame();
        const POINT outside = layout == 0 ? POINT{full.width / 2, frame.top - 4}
                                          : POINT{frame.left - 4, full.height / 2};
        ASSERT_TRUE(!PtInRect(&frame, outside));
        // Both sides use the same source; full strength must not apply a hidden theme cap.
        const auto is_source = [](COLORREF color) {
            return color == RGB(200, 20, 20) || color == RGB(20, 20, 200);
        };
        ASSERT_TRUE(is_source(full.at(outside.x, outside.y)));
        SendMessageW(strength, TBM_SETPOS, TRUE, 0);
        SendMessageW(dialog, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(strength));
        const CanvasSnapshot plain(canvas);
        ASSERT_EQ(plain.at(outside.x, outside.y), full.at(outside.x, outside.y));
        bool inside_changed = false;
        for (int y = frame.top + 3; y < frame.bottom - 3; ++y) {
            for (int x = frame.left + 3; x < frame.right - 3; ++x) {
                inside_changed |= full.at(x, y) != plain.at(x, y);
            }
        }
        ASSERT_TRUE(inside_changed);
        if (layout == 0) {
            int aligned_columns = 0;
            for (int x = frame.left + 4; x < frame.right - 4; ++x) {
                const auto source = full.at(x, outside.y);
                // Ignore interpolated stripe boundaries. Solid columns must have the same
                // red/blue tint inside and outside the frame, with no positional drift.
                if (!is_source(source) || full.at(x - 2, outside.y) != source ||
                    full.at(x + 2, outside.y) != source) {
                    continue;
                }
                for (int y = frame.top + 4; y < frame.bottom - 4; ++y) {
                    const auto actual = full.at(x, y);
                    const auto base = plain.at(x, y);
                    if (actual == base) {
                        continue;
                    }
                    const int red = int(GetRValue(actual)) - int(GetRValue(base));
                    const int blue = int(GetBValue(actual)) - int(GetBValue(base));
                    ASSERT_TRUE(source == RGB(200, 20, 20) ? red > blue : blue > red);
                    ++aligned_columns;
                    break;
                }
            }
            ASSERT_GT(aligned_columns, (frame.right - frame.left) / 4);
        }
        POINT screen = outside;
        ClientToScreen(canvas, &screen);
        SendMessageW(canvas, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                     MAKELPARAM(screen.x, screen.y));
        ASSERT_EQ(SendMessageW(zoom, TBM_GETPOS, 0, 0), 110);
        const CanvasSnapshot before_drag(canvas);
        SendMessageW(canvas, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(outside.x, outside.y));
        ASSERT_EQ(GetCapture(), canvas);
        SendMessageW(canvas, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(outside.x + 8, outside.y + 8));
        SendMessageW(canvas, WM_LBUTTONUP, 0, MAKELPARAM(outside.x + 8, outside.y + 8));
        ASSERT_TRUE(GetCapture() != canvas);
        const CanvasSnapshot moved(canvas);
        bool source_moved = false;
        for (int y = 0; y < moved.height; ++y) {
            for (int x = 0; x < moved.width; ++x) {
                if (!PtInRect(&frame, {x, y}) && is_source(before_drag.at(x, y)) &&
                    is_source(moved.at(x, y))) {
                    source_moved |= before_drag.at(x, y) != moved.at(x, y);
                }
            }
        }
        ASSERT_TRUE(source_moved);
        auto after = moved.frame();
        ASSERT_TRUE(EqualRect(&frame, &after));
        // Reapply the already selected fit using the actual button notification.
        SendMessageW(GetDlgItem(dialog, 3101), BM_CLICK, 0, 0);
        ASSERT_EQ(SendMessageW(zoom, TBM_GETPOS, 0, 0), 100);
        const CanvasSnapshot restored(canvas);
        after = restored.frame();
        ASSERT_TRUE(EqualRect(&frame, &after));
        ASSERT_EQ(SendMessageW(strength, TBM_GETPOS, 0, 0), 0);
        for (int y = 0; y < restored.height; ++y) {
            for (int x = 0; x < restored.width; ++x) {
                ASSERT_EQ(restored.at(x, y), plain.at(x, y));
            }
        }
        // Disabled pictures keep their framing but do not respond to gestures.
        SendMessageW(GetDlgItem(dialog, 3120), BM_CLICK, 0, 0);
        SendMessageW(canvas, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                     MAKELPARAM(screen.x, screen.y));
        SendMessageW(canvas, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(outside.x, outside.y));
        ASSERT_TRUE(GetCapture() != canvas);
        ASSERT_EQ(SendMessageW(zoom, TBM_GETPOS, 0, 0), 100);
        SendMessageW(GetDlgItem(dialog, 3120), BM_CLICK, 0, 0);
    }
}

struct DialogDriver {
    HWND owner = nullptr;
    UINT_PTR timer = 0;
    ULONGLONG deadline = GetTickCount64() + 5000;
    bool cancel = false;
    bool gestures = false;
    bool visited = false;
    bool exercise_controls = false;
    bool disable = false;
    bool inspect_canvas = false;
    std::vector<std::wstring> choices;
    std::size_t next_choice = 0;
    HWND picker = nullptr;
    background_test::Files* capture_files = nullptr;
    std::wstring capture_path;
    int capture_scene = 0;
    static DialogDriver* active;

    explicit DialogDriver(bool cancel, bool gestures)
        : cancel(cancel)
        , gestures(gestures) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        ASSERT_TRUE(InitCommonControlsEx(&controls));
        owner = CreateWindowExW(0, L"STATIC", L"Image test owner", WS_POPUP, 100, 100, 640, 480,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(owner != nullptr);
        active = this;
        timer = SetTimer(nullptr, 0, 20, tick);
        ASSERT_TRUE(timer != 0);
    }
    ~DialogDriver() {
        KillTimer(nullptr, timer);
        DestroyWindow(owner);
        active = nullptr;
    }
    static BOOL CALLBACK find(HWND window, LPARAM data) {
        wchar_t name[80]{};
        GetClassNameW(window, name, 80);
        if (wcscmp(name, L"CxxIME.BackgroundDialog") == 0 &&
            GetWindow(window, GW_OWNER) == active->owner) {
            *reinterpret_cast<HWND*>(data) = window;
            return TRUE;
        }
        if (wcscmp(name, L"#32770") == 0) {
            HWND owner = GetWindow(window, GW_OWNER);
            GetClassNameW(owner, name, 80);
            if (wcscmp(name, L"CxxIME.BackgroundDialog") == 0 &&
                GetWindow(owner, GW_OWNER) == active->owner) {
                active->picker = window;
            }
        }
        return TRUE;
    }
    static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
        auto& self = *active;
        HWND dialog = nullptr;
        self.picker = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), find, reinterpret_cast<LPARAM>(&dialog));
        ASSERT_LT(GetTickCount64(), self.deadline);
        if (self.picker) {
            ASSERT_GT(self.next_choice, 0);
            HWND filename = nullptr;
            EnumChildWindows(
                self.picker,
                [](HWND child, LPARAM data) -> BOOL {
                    wchar_t name[32]{};
                    GetClassNameW(child, name, 32);
                    const int id = GetDlgCtrlID(child);
                    if (wcscmp(name, L"Edit") == 0 && (id == cmb13 || id == edt1)) {
                        *reinterpret_cast<HWND*>(data) = child;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&filename));
            ASSERT_TRUE(filename != nullptr);
            ASSERT_TRUE(SetWindowTextW(filename, self.choices[self.next_choice - 1].c_str()));
            PostMessageW(self.picker, WM_COMMAND, IDOK, 0);
            return;
        }
        if (!dialog || !IsWindowEnabled(GetDlgItem(dialog, IDOK))) {
            return;
        }
        self.visited = true;
        if (self.next_choice < self.choices.size()) {
            ++self.next_choice;
            HWND choose = GetDlgItem(dialog, 3121);
            ASSERT_TRUE(IsWindowVisible(choose));
            ASSERT_TRUE(IsWindowEnabled(choose));
            SendMessageW(choose, BM_CLICK, 0, 0);
            return;
        }
        HWND canvas = GetDlgItem(dialog, 3111);
        ASSERT_TRUE(canvas != nullptr);
        ASSERT_TRUE(GetWindowLongPtrW(canvas, GWL_STYLE) & WS_TABSTOP);
        ASSERT_TRUE(GetDlgItem(dialog, 3104) != nullptr);
        if (SendMessageW(GetDlgItem(dialog, 3120), BM_GETCHECK, 0, 0) != BST_CHECKED) {
            check_plain_preview(dialog, canvas);
        }
        if (self.inspect_canvas) {
            check_canvas(dialog, canvas);
        }
        if (self.exercise_controls) {
            const int ids[] = {3120, 3121, 3101, 3102, 3103, 3104, 3111, 3123, IDOK};
            RECT positions[9]{};
            for (int i = 0; i < 9; ++i) {
                GetWindowRect(GetDlgItem(dialog, ids[i]), &positions[i]);
            }
            HWND enabled = GetDlgItem(dialog, 3120);
            HWND strength = GetDlgItem(dialog, 3123);
            ASSERT_EQ(SendMessageW(enabled, BM_GETCHECK, 0, 0), BST_CHECKED);
            SendMessageW(enabled, BM_CLICK, 0, 0);
            ASSERT_TRUE(!IsWindowEnabled(strength));
            ASSERT_TRUE(!IsWindowEnabled(GetDlgItem(dialog, 3104)));
            ASSERT_TRUE(IsWindowEnabled(GetDlgItem(dialog, 3121)));
            check_plain_preview(dialog, canvas);
            SendMessageW(enabled, BM_CLICK, 0, 0);
            ASSERT_TRUE(IsWindowEnabled(strength));
            SendMessageW(strength, TBM_SETPOS, TRUE, 40);
            SendMessageW(dialog, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(strength));
            if (self.disable) {
                SendMessageW(enabled, BM_CLICK, 0, 0);
            }
            for (int i = 0; i < 9; ++i) {
                RECT current{};
                GetWindowRect(GetDlgItem(dialog, ids[i]), &current);
                ASSERT_TRUE(EqualRect(&current, &positions[i]));
            }
        }
        if (self.gestures) {
            SendMessageW(dialog, WM_COMMAND, 3102, 0); // contain resets to center / 100%.
            HWND zoom = GetDlgItem(dialog, 3104);
            SendMessageW(zoom, TBM_SETPOS, TRUE, 175);
            SendMessageW(dialog, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(zoom));
            SendMessageW(GetDlgItem(dialog, 3102), BM_CLICK, 0, 0);
            ASSERT_EQ(SendMessageW(zoom, TBM_GETPOS, 0, 0), 100);
            SendMessageW(zoom, TBM_SETPOS, TRUE, 175);
            SendMessageW(dialog, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(zoom));
            SendMessageW(canvas, WM_KEYDOWN, VK_RIGHT, 0);
            HWND scene = GetDlgItem(dialog, 3103);
            SetFocus(scene);
            MSG tab{};
            tab.hwnd = scene;
            tab.message = WM_KEYDOWN;
            tab.wParam = VK_TAB;
            ASSERT_TRUE(IsDialogMessageW(dialog, &tab));
            ASSERT_EQ(GetFocus(), canvas);
            SendMessageW(scene, CB_SETCURSEL, 1, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(3103, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(scene));
            // Start outside the candidate frame, then leave the canvas while captured.
            const POINT outside{4, 4};
            SendMessageW(canvas, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(outside.x, outside.y));
            ASSERT_EQ(GetCapture(), canvas);
            SendMessageW(canvas, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-8, -8));
            SendMessageW(canvas, WM_LBUTTONUP, 0, MAKELPARAM(-8, -8));
            POINT screen = outside;
            ClientToScreen(canvas, &screen);
            SendMessageW(canvas, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                         MAKELPARAM(screen.x, screen.y));
            ASSERT_EQ(SendMessageW(zoom, TBM_GETPOS, 0, 0), 193);
            SendMessageW(canvas, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA),
                         MAKELPARAM(screen.x, screen.y));
            ASSERT_EQ(SendMessageW(zoom, TBM_GETPOS, 0, 0), 175);
            for (int index : {0, 1, 0}) {
                SendMessageW(scene, CB_SETCURSEL, index, 0);
                SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(3103, CBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(scene));
                UpdateWindow(canvas);
            }
            // A no-op slider release must not alter the image position.
            SendMessageW(dialog, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(zoom));
        }
        if (self.capture_files) {
            HWND scene = GetDlgItem(dialog, 3103);
            SendMessageW(scene, CB_SETCURSEL, self.capture_scene, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(3103, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(scene));
            self.capture_files->snapshot(dialog, self.capture_path);
        }
        SendMessageW(dialog, WM_COMMAND, self.cancel ? IDCANCEL : IDOK, 0);
    }
};
DialogDriver* DialogDriver::active = nullptr;

} // namespace

TEST(BackgroundDialog, confirm_keeps_layout_and_draft_until_main_apply) {
    test::ScopedDpiAwarenessContext dpi;
    background_test::Files files;
    const auto source =
        files.write(L"preview.png", 320, 240, background_test::solid(320, 240, 20, 80, 160));
    cxxime::Config config;
    config.font_size = 28;
    config.layout = "vertical";
    config.background.strength = 0.7;
    ASSERT_TRUE(config.load_themes(std::string(CXXIME_DATA_DIR) + "themes.json"));
    const auto original = config.to_user_json();
    std::optional<cxxime::settings::BackgroundEdit> result;
    {
        DialogDriver driver(false, true);
        result = cxxime::settings::edit_background(driver.owner, config, source);
        ASSERT_TRUE(driver.visited);
        ASSERT_TRUE(IsWindowEnabled(driver.owner));
    }
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(config.to_user_json(), original);
    ASSERT_EQ(result->config.fit, "contain");
    ASSERT_LT(std::abs(result->config.zoom - 1.75), 0.000001);
    ASSERT_EQ(result->config.strength, 0.7);
    ASSERT_TRUE(result->config.x != 0.5);
    ASSERT_GT(std::abs(result->config.y - 0.5), 0.001);
    ASSERT_TRUE(result->imported.image != nullptr);
    const auto saved = result->config;
    config.background = saved;
    {
        DialogDriver reopen(false, false);
        auto untouched = cxxime::settings::edit_background(reopen.owner, config);
        ASSERT_TRUE(untouched.has_value());
        ASSERT_EQ(untouched->config.x, saved.x);
        ASSERT_EQ(untouched->config.y, saved.y);
        ASSERT_EQ(untouched->config.zoom, saved.zoom);
    }
}

TEST(BackgroundDialog, cancel_discards_new_copy_but_not_preexisting_resource) {
    background_test::Files files;
    const auto source =
        files.write(L"cancel.png", 64, 32, background_test::solid(64, 32, 50, 100, 150));
    cxxime::Config config;
    {
        DialogDriver driver(true, true);
        ASSERT_TRUE(!cxxime::settings::edit_background(driver.owner, config, source));
        ASSERT_TRUE(driver.visited);
    }
    const auto resources = files.directory / "backgrounds";
    ASSERT_TRUE(std::filesystem::is_empty(resources));
    auto existing = cxxime::import_background_image(source);
    ASSERT_TRUE(existing.image != nullptr);
    config.background.resource = existing.resource;
    config.background.enabled = true;
    {
        DialogDriver driver(true, true);
        ASSERT_TRUE(!cxxime::settings::edit_background(driver.owner, config));
    }
    ASSERT_TRUE(std::filesystem::exists(cxxime::background_resource_path(existing.resource)));
}

TEST(BackgroundDialog, empty_background_can_be_confirmed_without_selecting_a_file) {
    background_test::Files files;
    cxxime::Config config;
    DialogDriver driver(false, false);
    auto result = cxxime::settings::edit_background(driver.owner, config);
    ASSERT_TRUE(driver.visited);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(!result->config.enabled);
    ASSERT_TRUE(result->config.resource.empty());
    ASSERT_TRUE(!result->imported.image);
}

TEST(BackgroundDialog, canvas_shows_source_and_actual_effect_with_a_stationary_frame) {
    test::ScopedDpiAwarenessContext dpi;
    background_test::Files files;
    auto pixels = background_test::solid(320, 240, 20, 20, 200);
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            if ((x / 20) % 2) {
                const int offset = (y * 320 + x) * 4;
                pixels[offset] = 200;
                pixels[offset + 2] = 20;
            }
        }
    }
    const auto source = files.write(L"canvas.png", 320, 240, pixels);
    cxxime::Config config;
    ASSERT_TRUE(config.load_themes(std::string(CXXIME_DATA_DIR) + "themes.json"));
    DialogDriver driver(false, false);
    driver.inspect_canvas = true;
    auto result = cxxime::settings::edit_background(driver.owner, config, source);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->config.zoom, 1);
    ASSERT_EQ(result->config.x, 0.5);
    ASSERT_EQ(result->config.y, 0.5);
}

TEST(BackgroundDialog, disable_preserves_picture_and_strength_without_moving_controls) {
    background_test::Files files;
    const auto source =
        files.write(L"toggle.png", 64, 32, background_test::solid(64, 32, 30, 80, 120));
    cxxime::Config config;
    DialogDriver driver(false, false);
    driver.exercise_controls = true;
    driver.disable = true;
    auto result = cxxime::settings::edit_background(driver.owner, config, source);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(!result->config.enabled);
    ASSERT_TRUE(!result->config.resource.empty());
    ASSERT_EQ(result->config.strength, 0.4);
    ASSERT_TRUE(result->imported.image != nullptr);
    ASSERT_TRUE(result->imported.created);
    config.background = result->config;
    DialogDriver reopen(false, false);
    auto reopened = cxxime::settings::edit_background(reopen.owner, config);
    ASSERT_TRUE(reopened.has_value());
    ASSERT_TRUE(!reopened->config.enabled);
    ASSERT_EQ(reopened->config.resource, result->config.resource);
    ASSERT_EQ(reopened->config.strength, 0.4);
}

TEST(BackgroundDialog, choosing_and_rechoosing_inside_dialog_transfers_only_the_final_draft) {
    background_test::Files files;
    const auto first =
        files.write(L"first.png", 64, 32, background_test::solid(64, 32, 20, 30, 40));
    const auto second =
        files.write(L"second.png", 64, 32, background_test::solid(64, 32, 90, 100, 110));
    cxxime::Config config;
    DialogDriver driver(false, false);
    driver.deadline = GetTickCount64() + 15000;
    driver.choices = {first, second, first};
    auto result = cxxime::settings::edit_background(driver.owner, config);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(driver.next_choice, driver.choices.size());
    ASSERT_TRUE(result->config.enabled);
    ASSERT_EQ(result->config.name, "first.png");
    ASSERT_TRUE(result->imported.created);
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(files.directory / "backgrounds")) {
        ASSERT_EQ(entry.path().stem().u8string(), result->config.resource);
        ++count;
    }
    ASSERT_EQ(count, 1);
    const auto resource = result->config.resource;
    // Main-window cancellation must still own and remove the transferred draft.
    {
        cxxime::BackgroundDraftResources drafts;
        drafts.add(result->imported);
        result.reset();
    }
    ASSERT_TRUE(!std::filesystem::exists(cxxime::background_resource_path(resource)));
}

TEST(BackgroundDialog, failed_replacement_preserves_the_existing_picture_and_framing) {
    background_test::Files files;
    auto original = cxxime::import_background_image(
        files.write(L"valid.png", 64, 32, background_test::solid(64, 32, 30, 40, 50)));
    ASSERT_TRUE(original.image != nullptr);
    const auto damaged = files.directory / "damaged.png";
    std::ofstream(damaged, std::ios::binary) << "not an image";
    cxxime::Config config;
    config.background.enabled = true;
    config.background.resource = original.resource;
    config.background.name = "valid.png";
    config.background.fit = "contain";
    config.background.zoom = 1.75;
    config.background.x = 0.25;
    config.background.strength = 0.4;
    DialogDriver driver(false, false);
    driver.deadline = GetTickCount64() + 15000;
    driver.choices = {damaged.wstring()};
    auto result = cxxime::settings::edit_background(driver.owner, config);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->config.resource, original.resource);
    ASSERT_EQ(result->config.name, config.background.name);
    ASSERT_EQ(result->config.fit, config.background.fit);
    ASSERT_EQ(result->config.zoom, config.background.zoom);
    ASSERT_EQ(result->config.x, config.background.x);
    ASSERT_EQ(result->config.strength, config.background.strength);
    ASSERT_TRUE(result->config.enabled);
    ASSERT_TRUE(result->imported.image != nullptr);
    ASSERT_TRUE(!result->imported.created);
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--capture") {
        test::ScopedDpiAwarenessContext dpi;
        background_test::Files files;
        cxxime::Config config;
        ASSERT_TRUE(config.load_themes(std::string(CXXIME_DATA_DIR) + "themes.json"));
        const auto source = std::filesystem::u8path(argv[2]).wstring();
        const auto output = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        std::filesystem::create_directories(output);
        for (const auto& theme : config.preset_color_schemes) {
            config.theme = theme.first;
            for (int scene : {0, 1}) {
                DialogDriver driver(false, false);
                driver.capture_files = &files;
                driver.capture_scene = scene;
                driver.capture_path =
                    (output / (theme.first + (scene == 0 ? "-horizontal.png" : "-vertical.png")))
                        .wstring();
                ASSERT_TRUE(
                    cxxime::settings::edit_background(driver.owner, config, source).has_value());
            }
        }
        return 0;
    }
    return test::RunAllTests();
}
