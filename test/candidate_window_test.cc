// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>
#include <utility>

#include <windows.h>

#include <cxxime/candidate_window.h>
#include <cxxime/config.h>

#include "util/testutil.h"

TEST(CandidateWindow, page_buttons_use_page_callback) {
    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text = "candidate";
    page.candidates.push_back(candidate);

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));

    int callback_count = 0;
    int selection_count = 0;
    std::size_t selected_index = 1;
    cxxime::CandidatePageDirection last_direction = cxxime::CandidatePageDirection::Previous;
    window.set_candidate_selection_callback([&](std::size_t index) {
        ++selection_count;
        selected_index = index;
    });
    window.set_page_callback([&](cxxime::CandidatePageDirection direction) {
        ++callback_count;
        last_direction = direction;
    });

    window.set_page_info(1, 2);
    window.update(page);
    RECT previous = window.page_button_rect_for_test(cxxime::CandidatePageDirection::Previous);
    ASSERT_TRUE(previous.right > previous.left);
    SendMessageW(
        window.hwnd_for_test(), WM_LBUTTONDOWN, 0,
        MAKELPARAM((previous.left + previous.right) / 2,
                   (previous.top + previous.bottom) / 2));
    ASSERT_EQ(callback_count, 0);
    ASSERT_EQ(selection_count, 0);

    RECT next = window.page_button_rect_for_test(cxxime::CandidatePageDirection::Next);
    ASSERT_TRUE(next.right > next.left);
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0,
                 MAKELPARAM((next.left + next.right) / 2, (next.top + next.bottom) / 2));
    ASSERT_EQ(callback_count, 1);
    ASSERT_EQ(selection_count, 0);
    ASSERT_EQ(last_direction, cxxime::CandidatePageDirection::Next);

    window.set_page_info(2, 2);
    window.update(page);
    previous = window.page_button_rect_for_test(cxxime::CandidatePageDirection::Previous);
    ASSERT_TRUE(previous.right > previous.left);
    SendMessageW(
        window.hwnd_for_test(), WM_LBUTTONDOWN, 0,
        MAKELPARAM((previous.left + previous.right) / 2, (previous.top + previous.bottom) / 2));
    ASSERT_EQ(callback_count, 2);
    ASSERT_EQ(selection_count, 0);
    ASSERT_EQ(last_direction, cxxime::CandidatePageDirection::Previous);

    next = window.page_button_rect_for_test(cxxime::CandidatePageDirection::Next);
    SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0,
                 MAKELPARAM((next.left + next.right) / 2, (next.top + next.bottom) / 2));
    ASSERT_EQ(callback_count, 2);
    ASSERT_EQ(selection_count, 0);

    RECT candidate_rect = window.candidate_rect_for_test(0);
    ASSERT_TRUE(candidate_rect.right > candidate_rect.left);
    SendMessageW(
        window.hwnd_for_test(), WM_LBUTTONDOWN, 0,
        MAKELPARAM((candidate_rect.left + candidate_rect.right) / 2,
                   (candidate_rect.top + candidate_rect.bottom) / 2));
    ASSERT_EQ(callback_count, 2);
    ASSERT_EQ(selection_count, 1);
    ASSERT_EQ(selected_index, 0);

    window.destroy();
}

TEST(CandidateWindow, recreate_resets_native_window_size_cache) {
    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text = "candidate";
    page.candidates.push_back(candidate);

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page);

    HWND first_hwnd = window.hwnd_for_test();
    ASSERT_TRUE(first_hwnd != nullptr);
    RECT first_rect = {};
    ASSERT_TRUE(GetWindowRect(first_hwnd, &first_rect) != FALSE);
    window.destroy();

    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page);

    HWND second_hwnd = window.hwnd_for_test();
    ASSERT_TRUE(second_hwnd != nullptr);
    RECT second_rect = {};
    ASSERT_TRUE(GetWindowRect(second_hwnd, &second_rect) != FALSE);

    ASSERT_EQ(second_rect.right - second_rect.left, first_rect.right - first_rect.left);
    ASSERT_EQ(second_rect.bottom - second_rect.top, first_rect.bottom - first_rect.top);
    window.destroy();
}

TEST(CandidateWindow, dpi_relayout_notifies_controller) {
    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text = "candidate";
    page.candidates.push_back(candidate);

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page);
    window.show();

    int callback_count = 0;
    window.set_layout_changed_callback([&callback_count]() { ++callback_count; });
    const UINT old_dpi = window.dpi();
    const UINT next_dpi = old_dpi == 96 ? 120 : 96;
    RECT suggested = {};
    ASSERT_TRUE(window.get_window_rect(&suggested));
    SendMessageW(window.hwnd_for_test(), WM_DPICHANGED, MAKELPARAM(next_dpi, next_dpi),
                 reinterpret_cast<LPARAM>(&suggested));

    ASSERT_EQ(callback_count, 1);
    ASSERT_TRUE(window.is_visible());
    ASSERT_TRUE(window.visible_candidate_count() > 0);
    window.destroy();
}

TEST(CandidateWindow, show_restores_topmost_z_order) {
    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND hwnd = window.hwnd_for_test();
    ASSERT_TRUE(hwnd != nullptr);
    ASSERT_TRUE(SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE);
    ASSERT_TRUE((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);

    window.show();

    ASSERT_TRUE((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    window.destroy();
}

TEST(CandidateWindow, owner_can_follow_active_context_window) {
    HWND first_owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                       nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND context_window = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, first_owner,
                                          nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND second_owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                        nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(first_owner != nullptr);
    ASSERT_TRUE(context_window != nullptr);
    ASSERT_TRUE(second_owner != nullptr);

    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND candidate = window.hwnd_for_test();
    ASSERT_TRUE(candidate != nullptr);

    window.set_owner(context_window);
    ASSERT_TRUE(window.owner_matches(context_window));
    candidate = window.hwnd_for_test();
    ASSERT_TRUE(candidate != nullptr);
    ASSERT_EQ(GetWindow(candidate, GW_OWNER), context_window);

    window.set_owner(second_owner);
    candidate = window.hwnd_for_test();
    ASSERT_TRUE(candidate != nullptr);
    ASSERT_EQ(GetWindow(candidate, GW_OWNER), second_owner);

    window.hide();
    ASSERT_TRUE(GetWindow(candidate, GW_OWNER) == nullptr);

    window.destroy();
    DestroyWindow(second_owner);
    DestroyWindow(first_owner);
}

TEST(CandidateWindow, ensure_created_recovers_destroyed_window_and_owner) {
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(owner != nullptr);

    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND stale_window = window.hwnd_for_test();
    ASSERT_TRUE(stale_window != nullptr);
    ASSERT_TRUE(DestroyWindow(stale_window) != FALSE);
    ASSERT_TRUE(!window.is_created());

    ASSERT_TRUE(window.ensure_created(owner));
    ASSERT_TRUE(window.is_created());
    ASSERT_TRUE(window.owner_matches(owner));

    window.destroy();
    DestroyWindow(owner);
}

TEST(CandidateWindow, width_is_clamped_to_monitor_work_area) {
    cxxime::Config config;
    config.render_backend = "gdi";
    config.layout = "horizontal";
    config.layout_config.min_width = 100000;
    config.layout_config.max_width = 100000;

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text.assign(4096, 'w');
    page.candidates.push_back(std::move(candidate));
    page.total_count = 2;

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND hwnd = window.hwnd_for_test();
    ASSERT_TRUE(hwnd != nullptr);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {sizeof(monitor_info)};
    ASSERT_TRUE(GetMonitorInfoW(monitor, &monitor_info) != FALSE);

    RECT caret_rect = {monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 16,
                       monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 36};
    window.move_to_caret(caret_rect);
    window.update(page);

    RECT window_rect = {};
    ASSERT_TRUE(GetWindowRect(hwnd, &window_rect) != FALSE);
    ASSERT_LE(window_rect.right - window_rect.left,
              monitor_info.rcWork.right - monitor_info.rcWork.left);

    window.show();
    ASSERT_EQ(window.visible_candidate_count(), 1);
    window.hide();
    ASSERT_EQ(window.visible_candidate_count(), 0);
    window.destroy();
}

TEST(CandidateWindow, automatic_width_uses_comfortable_work_area_limit) {
    cxxime::Config config;
    config.render_backend = "gdi";
    config.layout = "horizontal";
    config.layout_config.max_width = 0;

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text.assign(4096, 'w');
    page.candidates.push_back(std::move(candidate));

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND hwnd = window.hwnd_for_test();
    ASSERT_TRUE(hwnd != nullptr);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {sizeof(monitor_info)};
    ASSERT_TRUE(GetMonitorInfoW(monitor, &monitor_info) != FALSE);

    RECT caret_rect = {monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 16,
                       monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 36};
    window.move_to_caret(caret_rect);
    window.update(page);

    HDC dc = GetDC(hwnd);
    const float dpi_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(hwnd, dc);
    const int work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
    const int expected_max_width =
        cxxime::calculate_auto_candidate_window_max_width(work_width, dpi_scale);
    RECT window_rect = {};
    ASSERT_TRUE(GetWindowRect(hwnd, &window_rect) != FALSE);
    ASSERT_LE(window_rect.right - window_rect.left, expected_max_width);

    window.destroy();
}

RUN_ALL_TESTS()
