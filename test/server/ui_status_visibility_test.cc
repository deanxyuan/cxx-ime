// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/ui_channel.h>
#include <cxxime/window_position.h>

#include "support/dpi_testutil.h"
#include "support/testutil.h"
#include "support/window_transition_probe.h"
#include "ui_presentation_controller.h"
#include "ui_presentation_router.h"

namespace {

bool wait_for(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    return true;
}

class HostWindow {
public:
    explicit HostWindow(bool fullscreen = false, HWND owner = nullptr, HMONITOR monitor = nullptr) {
        if (!monitor) {
            monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        }
        MONITORINFO info = {sizeof(info)};
        ASSERT_TRUE(GetMonitorInfoW(monitor, &info));
        const RECT area = fullscreen ? info.rcMonitor : info.rcWork;
        const int width = (area.right - area.left) / (fullscreen ? 1 : 2);
        const int height = (area.bottom - area.top) / (fullscreen ? 1 : 2);
        window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Visibility test host",
                                 WS_POPUP | WS_VISIBLE, area.left, area.top, width, height, owner,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(window != nullptr);
        ASSERT_EQ(cxxime::is_fullscreen_window(window), fullscreen);
    }
    ~HostWindow() { ASSERT_TRUE(DestroyWindow(window)); }

    HWND window = nullptr;
};

HWND find_ui_window(const wchar_t* class_name) {
    HWND result = nullptr;
    struct Search {
        const wchar_t* class_name;
        HWND* result;
    } search{class_name, &result};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(parameter);
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            wchar_t name[64] = {};
            if (process == GetCurrentProcessId() && GetClassNameW(window, name, 64) &&
                lstrcmpW(name, search.class_name) == 0) {
                *search.result = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return result;
}

class StatusController {
public:
    StatusController() {
        auto config = std::make_shared<cxxime::Config>();
        config->render_backend = "gdi";
        config->status_window.enable = true;
        ASSERT_TRUE(controller.start(config, {}, {}));
        status = find_ui_window(L"CxxIMEStatusWindow");
        ASSERT_TRUE(status != nullptr);
        test::foreground_observer_thread.store(GetWindowThreadProcessId(status, nullptr));
        snapshot.session_id = 1;
        snapshot.session_generation = 1;
        snapshot.target_generation = 1;
        snapshot.composition_generation = 1;
        snapshot.presentation_generation = 1;
        snapshot.flags = cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible);
        // Start with status only; tests may add an active candidate presentation.
    }

    ~StatusController() {
        controller.stop();
        test::foreground_observer_thread.store(0);
    }

    void apply(std::uintptr_t foreground, test::WindowTransitionProbe& probe) {
        const auto reads = test::foreground_reads.load();
        controller.present(1, &snapshot, false, 0, ++revision, foreground);
        ASSERT_TRUE(wait_for([&]() { return test::foreground_reads.load() > reads; }));
        // Keep the host responsive to synchronous messages from owned candidate UI.
        auto drained = std::async(std::launch::async, [&]() { probe.drain(); });
        ASSERT_TRUE(wait_for([&]() {
            return drained.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        }));
        drained.get();
    }

    UiPresentationController controller;
    cxxime::UiPresentationSnapshot snapshot;
    std::uint64_t revision = 0;
    HWND status = nullptr;
};

TEST(UiStatusVisibility, windowless_fullscreen_updates_never_show_and_normal_focus_recovers) {
    test::ScopedDpiAwarenessContext dpi;
    HostWindow fullscreen(true);
    HostWindow normal;
    test::foreground_sample.store(fullscreen.window);
    StatusController fixture;
    test::WindowTransitionProbe probe(fixture.status);
    for (int update = 0; update < 6; ++update) {
        fixture.snapshot.ime_status.set_caps_lock(update % 2 != 0);
        fixture.apply(reinterpret_cast<std::uintptr_t>(fullscreen.window), probe);
        ASSERT_TRUE(!IsWindowVisible(fixture.status));
        probe.expect(0, 0);
    }
    test::foreground_sample.store(normal.window);
    fixture.apply(reinterpret_cast<std::uintptr_t>(normal.window), probe);
    ASSERT_TRUE(IsWindowVisible(fixture.status));
    probe.expect(1, 0);
}

TEST(UiStatusVisibility, queued_snapshots_cannot_show_over_a_different_foreground) {
    test::ScopedDpiAwarenessContext dpi;
    HostWindow original;
    HostWindow replacement;
    const auto captured = reinterpret_cast<std::uintptr_t>(original.window);
    test::foreground_sample.store(replacement.window);
    StatusController fixture;
    test::WindowTransitionProbe probe(fixture.status);
    for (bool windowless : {false, true}) {
        fixture.snapshot.target_window =
            windowless ? 0 : reinterpret_cast<std::uint64_t>(original.window);
        fixture.apply(captured, probe);
        ASSERT_TRUE(!IsWindowVisible(fixture.status));
        probe.expect(0, 0);
    }
    // A still-current foreground does not make a destroyed target valid.
    HWND destroyed = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(destroyed != nullptr);
    ASSERT_TRUE(DestroyWindow(destroyed));
    fixture.snapshot.target_window = reinterpret_cast<std::uint64_t>(destroyed);
    fixture.apply(reinterpret_cast<std::uintptr_t>(replacement.window), probe);
    probe.expect(0, 0);
}

TEST(UiStatusVisibility, fullscreen_edit_then_owned_popups_never_show_status) {
    test::ScopedDpiAwarenessContext dpi;
    // A normal root owner must not hide the fullscreen player in the middle of the chain.
    HostWindow application;
    HostWindow player(true, application.window);
    HostWindow popup(false, player.window);
    HostWindow nested_popup(false, popup.window);
    test::foreground_sample.store(player.window);
    StatusController fixture;
    test::WindowTransitionProbe probe(fixture.status);
    auto& snapshot = fixture.snapshot;
    snapshot.target_window = reinterpret_cast<std::uint64_t>(player.window);
    snapshot.ownership = cxxime::UiOwnership::kExternal;
    snapshot.flags |= cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kCandidateVisible) |
                      cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kHasCaret) |
                      cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kHasCandidates);
    snapshot.candidate_page.count = 1;
    snapshot.candidate_page.total = 1;
    snapshot.candidate_known_count = 1;
    snapshot.candidate_page.candidates[0].text_length = 3;
    std::memcpy(snapshot.candidate_page.candidates[0].text, u8"在", 3);
    RECT player_rect = {};
    ASSERT_TRUE(GetWindowRect(player.window, &player_rect));
    snapshot.caret = {player_rect.left + 20, player_rect.top + 20, player_rect.left + 21,
                      player_rect.top + 40};
    fixture.apply(reinterpret_cast<std::uintptr_t>(player.window), probe);
    HWND candidate = find_ui_window(L"CxxIMECandidateWindow");
    ASSERT_TRUE(candidate && IsWindowVisible(candidate));
    probe.expect(0, 0);

    // The commit ends composition. A later focus change may retain the old caret,
    // but neither it nor a fresh status request permits status UI over the player.
    ++snapshot.composition_generation;
    snapshot.flags = cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible);
    snapshot.candidate_page = {};
    snapshot.candidate_known_count = 0;
    fixture.apply(reinterpret_cast<std::uintptr_t>(player.window), probe);
    ASSERT_TRUE(!IsWindowVisible(candidate));
    probe.expect(0, 0);
    const HWND targets[] = {popup.window, nested_popup.window, popup.window, player.window};
    for (bool windowless : {false, true}) {
        for (HWND target : targets) {
            snapshot.flags = 0;
            fixture.apply(reinterpret_cast<std::uintptr_t>(GetForegroundWindow()), probe);
            test::foreground_sample.store(target);
            ++snapshot.target_generation;
            snapshot.target_window = windowless ? 0 : reinterpret_cast<std::uint64_t>(target);
            snapshot.flags = cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible);
            fixture.apply(reinterpret_cast<std::uintptr_t>(target), probe);
            ASSERT_TRUE(!IsWindowVisible(fixture.status));
            probe.expect(0, 0);
        }
    }

    // Leaving fullscreen must allow the same popup to display status again.
    RECT normal_rect = {};
    ASSERT_TRUE(GetWindowRect(application.window, &normal_rect));
    ASSERT_TRUE(SetWindowPos(player.window, nullptr, normal_rect.left, normal_rect.top,
                             normal_rect.right - normal_rect.left,
                             normal_rect.bottom - normal_rect.top, SWP_NOZORDER | SWP_NOACTIVATE));
    test::foreground_sample.store(popup.window);
    snapshot.target_window = reinterpret_cast<std::uint64_t>(popup.window);
    fixture.apply(reinterpret_cast<std::uintptr_t>(popup.window), probe);
    ASSERT_TRUE(IsWindowVisible(fixture.status));
    probe.expect(1, 0);
}

TEST(UiStatusVisibility, hidden_minimized_and_unrelated_windows_do_not_suppress_status) {
    test::ScopedDpiAwarenessContext dpi;
    HostWindow player(true);
    HostWindow popup(false, player.window);
    HostWindow unrelated;
    ASSERT_TRUE(cxxime::is_fullscreen_window_or_owner(popup.window));
    ASSERT_TRUE(!cxxime::is_fullscreen_window_or_owner(unrelated.window));
    ASSERT_TRUE(!cxxime::is_fullscreen_window_or_owner(nullptr));

    const HWND child =
        CreateWindowExW(0, L"STATIC", L"Input view", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1,
                        popup.window, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(child != nullptr);
    ASSERT_TRUE(cxxime::is_fullscreen_window_or_owner(child));
    ASSERT_TRUE(DestroyWindow(child));
    ASSERT_TRUE(!cxxime::is_fullscreen_window_or_owner(child));

    ShowWindow(player.window, SW_HIDE);
    ASSERT_TRUE(!IsWindowVisible(player.window));
    ASSERT_TRUE(!cxxime::is_fullscreen_window_or_owner(popup.window));
    ShowWindow(player.window, SW_SHOWNOACTIVATE);
    ASSERT_TRUE(cxxime::is_fullscreen_window_or_owner(popup.window));
    ShowWindow(player.window, SW_SHOWMINNOACTIVE);
    ASSERT_TRUE(IsIconic(player.window));
    ASSERT_TRUE(!cxxime::is_fullscreen_window_or_owner(popup.window));
}

TEST(UiStatusVisibility, fullscreen_owner_does_not_suppress_another_monitor) {
    test::ScopedDpiAwarenessContext dpi;
    HostWindow player(true);
    const HMONITOR player_monitor = MonitorFromWindow(player.window, MONITOR_DEFAULTTONULL);
    std::vector<HMONITOR> monitors;
    ASSERT_TRUE(EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors)));
    for (HMONITOR monitor : monitors) {
        HostWindow popup(false, player.window, monitor);
        ASSERT_EQ(cxxime::is_fullscreen_window_or_owner(popup.window), monitor == player_monitor);
    }
}

TEST(UiStatusVisibility, handoff_into_fullscreen_hides_without_waiting_for_the_handoff_timer) {
    test::ScopedDpiAwarenessContext dpi;
    HostWindow normal;
    HostWindow fullscreen(true);
    HostWindow popup(false, fullscreen.window);
    for (HWND target : {fullscreen.window, popup.window}) {
        test::foreground_sample.store(normal.window);
        StatusController fixture;
        test::WindowTransitionProbe probe(fixture.status);
        fixture.apply(reinterpret_cast<std::uintptr_t>(normal.window), probe);
        ASSERT_TRUE(IsWindowVisible(fixture.status));
        test::foreground_sample.store(target);
        const auto reads = test::foreground_reads.load();
        fixture.controller.present(0, nullptr, true, 0, ++fixture.revision, 0);
        ASSERT_TRUE(wait_for([&]() { return test::foreground_reads.load() > reads; }));
        probe.drain();
        ASSERT_TRUE(!IsWindowVisible(fixture.status));
        probe.expect(1, 1);
    }
}

TEST(UiStatusVisibility, superseded_windowless_session_does_not_return_when_the_new_session_hides) {
    test::ScopedDpiAwarenessContext dpi;
    for (bool same_foreground : {false, true}) {
        HostWindow first;
        HostWindow second;
        test::foreground_sample.store(first.window);
        StatusController fixture;
        test::WindowTransitionProbe probe(fixture.status);
        const std::wstring pipe =
            L"\\\\.\\pipe\\CxxIME-Visibility-" + std::to_wstring(GetCurrentProcessId());
        std::atomic<unsigned int> callbacks{0};
        UiPresentationRouter router;
        ASSERT_TRUE(router.start(
            [&](cxxime::UiEndpointId endpoint, const cxxime::UiPresentationSnapshot* snapshot,
                bool handoff, std::uint64_t cycle, std::uint64_t revision,
                std::uintptr_t foreground) {
                fixture.controller.present(endpoint, snapshot, handoff, cycle, revision,
                                           foreground);
                ++callbacks;
            },
            pipe));
        cxxime::UiChannelClient client;
        ASSERT_TRUE(client.start({}, pipe));
        cxxime::UiChannelClient second_client;
        ASSERT_TRUE(second_client.start({}, pipe));
        ASSERT_TRUE(client.publish_latest(fixture.snapshot));
        ASSERT_TRUE(wait_for([&]() { return IsWindowVisible(fixture.status) != FALSE; }));
        probe.drain();
        const auto first_callbacks = callbacks.load();
        const HWND next_foreground = same_foreground ? first.window : second.window;
        test::foreground_sample.store(next_foreground);
        fixture.snapshot.session_id = 2;
        fixture.snapshot.target_window = reinterpret_cast<std::uint64_t>(next_foreground);
        const auto reads = test::foreground_reads.load();
        ASSERT_TRUE(second_client.publish_latest(fixture.snapshot));
        ASSERT_TRUE(wait_for([&]() {
            return callbacks.load() > first_callbacks && test::foreground_reads.load() > reads;
        }));
        probe.drain();
        ASSERT_TRUE(IsWindowVisible(fixture.status));
        probe.expect(1, 0);
        // Explicit hide in the same foreground must not be treated as a window handoff.
        fixture.snapshot.flags = 0;
        ++fixture.snapshot.presentation_generation;
        ASSERT_TRUE(second_client.publish_latest(fixture.snapshot));
        ASSERT_TRUE(wait_for([&]() { return !IsWindowVisible(fixture.status); }));
        probe.drain();
        probe.expect(1, 1);
        client.stop();
        second_client.stop();
        router.stop();
    }
}

} // namespace
