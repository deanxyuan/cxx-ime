// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <windows.h>

#include <json.hpp>

#include <cxxime/ime_menu.h>

#include "config_store.h"
#include "config_write_coordinator.h"
#include "support/dpi_testutil.h"
#include "support/testutil.h"
#include "support/window_transition_probe.h"
#include "ui_presentation_controller.h"

namespace {

bool wait_for(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
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

HWND find_status_window() {
    HWND result = nullptr;
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            wchar_t name[64] = {};
            if (process == GetCurrentProcessId() && GetClassNameW(window, name, 64) &&
                lstrcmpW(name, L"CxxIMEStatusWindow") == 0) {
                *reinterpret_cast<HWND*>(parameter) = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}

// Drive the native popup on its own thread, without moving the mouse, SendInput,
// or changing the real foreground application (which may use an installed IME).
class MenuSelection {
public:
    MenuSelection(HWND window, bool cancel, bool during_handoff = false,
                  std::function<void()> on_open = {})
        : window_(window)
        , cancel_(cancel)
        , on_open_(std::move(on_open))
        , await_handoff_timer_(during_handoff) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        ASSERT_TRUE(active_ == nullptr);
        active_ = this;
        const DWORD thread = GetWindowThreadProcessId(window, nullptr);
        call_hook_ = SetWindowsHookExW(WH_CALLWNDPROC, observe_call, nullptr, thread);
        message_hook_ = SetWindowsHookExW(WH_GETMESSAGE, observe_message, nullptr, thread);
        ASSERT_TRUE(call_hook_ != nullptr && message_hook_ != nullptr);
    }

    ~MenuSelection() {
        ASSERT_TRUE(UnhookWindowsHookEx(message_hook_));
        ASSERT_TRUE(UnhookWindowsHookEx(call_hook_));
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        active_ = nullptr;
    }

    void open() { ASSERT_TRUE(PostMessageW(window_, WM_RBUTTONUP, 0, MAKELPARAM(4, 4))); }

    std::atomic<bool> opened{false};
    std::atomic<bool> closed{false};
    std::atomic<bool> handoff_timer_seen{false};
    std::atomic<UINT> selected{0};

private:
    static LRESULT CALLBACK observe_call(int code, WPARAM wp, LPARAM lp) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (code >= 0 && active_) {
            auto& self = *active_;
            const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lp);
            if (message.hwnd == self.window_ && message.message == WM_INITMENUPOPUP &&
                !self.opened.exchange(true)) {
                if (self.on_open_) {
                    self.on_open_();
                }
                if (self.cancel_) {
                    PostMessageW(self.window_, WM_KEYDOWN, VK_ESCAPE, 0);
                } else {
                    const HMENU menu = reinterpret_cast<HMENU>(message.wParam);
                    const UINT hide =
                        static_cast<UINT>(cxxime::ImeMenuCommand::kToggleStatusWindow);
                    PostMessageW(self.window_, WM_KEYDOWN, VK_DOWN, 0);
                    bool found = false;
                    for (int index = 0; index < GetMenuItemCount(menu); ++index) {
                        const UINT id = GetMenuItemID(menu, index);
                        if (id == hide) {
                            found = true;
                            break;
                        }
                        if ((GetMenuState(menu, index, MF_BYPOSITION) & MF_SEPARATOR) == 0) {
                            PostMessageW(self.window_, WM_KEYDOWN, VK_DOWN, 0);
                        }
                    }
                    ASSERT_TRUE(found);
                    PostMessageW(self.window_, WM_KEYDOWN, VK_RETURN, 0);
                }
            } else if (message.hwnd == self.window_ && message.message == WM_MENUSELECT &&
                       message.lParam != 0) {
                self.selected.store(LOWORD(message.wParam));
            } else if (message.hwnd == self.window_ && message.message == WM_EXITMENULOOP) {
                self.closed.store(true);
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    static LRESULT CALLBACK observe_message(int code, WPARAM wp, LPARAM lp) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (code >= 0 && wp == PM_REMOVE && active_) {
            auto& self = *active_;
            const auto& message = *reinterpret_cast<const MSG*>(lp);
            if (self.await_handoff_timer_ && message.message == WM_TIMER && !message.hwnd) {
                self.await_handoff_timer_ = false;
                self.handoff_timer_seen.store(true);
                // The real handoff timer exists but has not been dispatched yet:
                // execute the menu while handoff is active, without a timing race.
                SendMessageW(self.window_, WM_RBUTTONUP, 0, MAKELPARAM(4, 4));
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    inline static MenuSelection* active_ = nullptr;
    inline static std::recursive_mutex mutex_;
    HWND window_ = nullptr;
    HHOOK call_hook_ = nullptr;
    HHOOK message_hook_ = nullptr;
    bool cancel_ = false;
    std::function<void()> on_open_;
    bool await_handoff_timer_ = false;
};

class MenuFixture {
public:
    MenuFixture() {
        char directory[MAX_PATH] = {};
        char temporary[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, directory));
        ASSERT_TRUE(GetTempFileNameA(directory, "cim", 0, temporary));
        path = temporary;
        ASSERT_TRUE(DeleteFileA(path.c_str()));
        std::shared_ptr<const cxxime::Config> initial;
        ASSERT_TRUE(store.initialize(std::string(CXXIME_DATA_DIR) + "default.json", path,
                                     std::string(CXXIME_DATA_DIR) + "themes.json", &initial));
        ASSERT_TRUE(
            writer.start(&store, [this](const std::shared_ptr<const cxxime::Config>& config) {
                controller.update_config(config);
                ++applied;
            }));
        std::string runtime;
        ASSERT_TRUE(
            writer.submit(cxxime::UserConfigMutationKind::kMergePatch,
                          R"({"style":{"render_backend":"gdi"},"status_window":{"enable":true}})",
                          &runtime, nullptr));
        auto config = std::make_shared<cxxime::Config>();
        ASSERT_TRUE(config->load_runtime_json(runtime));
        host = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Menu test host", WS_POPUP, 0, 0, 200,
                               100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(host != nullptr);
        test::foreground_sample.store(host);
        ASSERT_TRUE(controller.start(
            config, [this](cxxime::UiEndpointId, const cxxime::UiCommand&) { ++host_commands; },
            [this](const std::string& patch) {
                ASSERT_TRUE(writer.enqueue_patch(patch));
                ++patches;
            }));
        status = find_status_window();
        ASSERT_TRUE(status != nullptr);
        test::foreground_observer_thread.store(GetWindowThreadProcessId(status, nullptr));
        snapshot.session_id = 1;
        snapshot.session_generation = 1;
        snapshot.target_generation = 1;
        snapshot.composition_generation = 1;
        snapshot.presentation_generation = 1;
        snapshot.target_window = reinterpret_cast<std::uint64_t>(host);
        snapshot.flags = cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible);
        publish();
        ASSERT_TRUE(wait_for([&]() { return IsWindowVisible(status) != FALSE; }));
    }

    ~MenuFixture() {
        controller.stop();
        writer.stop();
        test::foreground_observer_thread.store(0);
        test::foreground_sample.store(nullptr);
        ASSERT_TRUE(DestroyWindow(host));
        ASSERT_TRUE(DeleteFileA(path.c_str()));
    }

    void publish() {
        controller.present(1, &snapshot, false, 0, ++revision,
                           reinterpret_cast<std::uintptr_t>(host));
    }

    void flush() {
        std::string ignored;
        ASSERT_TRUE(writer.snapshot_user_config(&ignored, nullptr));
    }

    bool saved_enabled() {
        std::ifstream input(path);
        return nlohmann::json::parse(input)["status_window"]["enable"].get<bool>();
    }

    ConfigStore store;
    ConfigWriteCoordinator writer;
    UiPresentationController controller;
    cxxime::UiPresentationSnapshot snapshot;
    std::string path;
    HWND host = nullptr;
    HWND status = nullptr;
    std::uint64_t revision = 0;
    std::atomic<int> patches{0};
    std::atomic<int> applied{0};
    std::atomic<int> host_commands{0};
};

TEST(UiStatusMenu, hide_persists_without_a_host_command_and_stale_updates_cannot_reshow) {
    test::ScopedDpiAwarenessContext dpi;
    for (int scenario = 0; scenario < 3; ++scenario) {
        MenuFixture fixture;
        test::WindowTransitionProbe probe(fixture.status);
        MenuSelection menu(fixture.status, false, scenario == 2, [&]() {
            if (scenario == 1) {
                ++fixture.snapshot.target_generation;
                ++fixture.snapshot.presentation_generation;
                fixture.publish();
            }
        });
        if (scenario == 2) {
            fixture.controller.present(0, nullptr, true, 0, ++fixture.revision, 0);
        } else {
            menu.open();
        }
        ASSERT_TRUE(wait_for([&]() { return menu.closed.load(); }));
        probe.drain();
        ASSERT_TRUE(menu.opened.load());
        ASSERT_EQ(menu.handoff_timer_seen.load(), scenario == 2);
        ASSERT_EQ(fixture.patches.load(), 1)
            << " scenario=" << scenario << " selected=" << menu.selected.load()
            << " host_commands=" << fixture.host_commands.load();
        ASSERT_EQ(fixture.host_commands.load(), 0);
        fixture.flush();
        ASSERT_TRUE(!fixture.saved_enabled());
        ASSERT_TRUE(wait_for([&]() { return !IsWindowVisible(fixture.status); }));
        probe.drain();
        probe.expect(0, 1);

        const auto reads = test::foreground_reads.load();
        ++fixture.snapshot.presentation_generation;
        fixture.publish();
        ASSERT_TRUE(wait_for([&]() { return test::foreground_reads.load() > reads; }));
        probe.drain();
        ASSERT_TRUE(!IsWindowVisible(fixture.status));
        probe.expect(0, 1);

        ASSERT_TRUE(fixture.writer.submit(cxxime::UserConfigMutationKind::kMergePatch,
                                          R"({"status_window":{"enable":true}})", nullptr,
                                          nullptr));
        ASSERT_TRUE(wait_for([&]() { return IsWindowVisible(fixture.status) != FALSE; }));
        probe.drain();
        ASSERT_TRUE(fixture.saved_enabled());
        probe.expect(1, 1);
    }
}

TEST(UiStatusMenu, cancel_does_not_change_configuration_or_visibility) {
    test::ScopedDpiAwarenessContext dpi;
    MenuFixture fixture;
    test::WindowTransitionProbe probe(fixture.status);
    MenuSelection menu(fixture.status, true);
    menu.open();
    ASSERT_TRUE(wait_for([&]() { return menu.closed.load(); }));
    probe.drain();
    fixture.flush();
    ASSERT_EQ(fixture.patches.load(), 0);
    ASSERT_EQ(fixture.host_commands.load(), 0);
    ASSERT_TRUE(fixture.saved_enabled());
    ASSERT_TRUE(IsWindowVisible(fixture.status));
    probe.expect(0, 0);
}

TEST(UiStatusMenu, failed_save_does_not_hide_and_menu_can_retry) {
    test::ScopedDpiAwarenessContext dpi;
    MenuFixture fixture;
    test::WindowTransitionProbe probe(fixture.status);
    const int applied = fixture.applied.load();
    // Deny replacing the existing user file, while still permitting reads.
    HANDLE locked = CreateFileA(fixture.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(locked != INVALID_HANDLE_VALUE);
    {
        MenuSelection menu(fixture.status, false);
        menu.open();
        ASSERT_TRUE(wait_for([&]() { return menu.closed.load(); }));
        probe.drain();
        fixture.flush();
        ASSERT_EQ(fixture.patches.load(), 1);
        ASSERT_EQ(fixture.applied.load(), applied);
        ASSERT_TRUE(fixture.saved_enabled());
        ASSERT_TRUE(IsWindowVisible(fixture.status));
        probe.expect(0, 0);
    }
    ASSERT_TRUE(CloseHandle(locked));
    MenuSelection retry(fixture.status, false);
    retry.open();
    ASSERT_TRUE(wait_for([&]() { return retry.closed.load(); }));
    probe.drain();
    fixture.flush();
    ASSERT_EQ(fixture.patches.load(), 2);
    ASSERT_EQ(fixture.applied.load(), applied + 1);
    ASSERT_TRUE(!fixture.saved_enabled());
    ASSERT_TRUE(wait_for([&]() { return !IsWindowVisible(fixture.status); }));
    probe.drain();
    probe.expect(0, 1);
}

TEST(UiStatusMenu, dragging_saves_position_through_the_same_config_channel) {
    test::ScopedDpiAwarenessContext dpi;
    MenuFixture fixture;
    test::WindowTransitionProbe probe(fixture.status);
    DWORD_PTR result = 0;
    for (const auto message : {WM_LBUTTONDOWN, WM_MOUSEMOVE, WM_LBUTTONUP}) {
        const int offset = message == WM_LBUTTONDOWN ? 4 : -30;
        ASSERT_TRUE(SendMessageTimeoutW(fixture.status, message, 0, MAKELPARAM(offset, offset),
                                        SMTO_ABORTIFHUNG, 3000, &result));
    }
    probe.drain();
    fixture.flush();
    ASSERT_EQ(fixture.patches.load(), 1);
    ASSERT_EQ(fixture.host_commands.load(), 0);
    RECT rect = {};
    ASSERT_TRUE(GetWindowRect(fixture.status, &rect));
    std::ifstream input(fixture.path);
    const auto saved = nlohmann::json::parse(input);
    ASSERT_EQ(saved["status_window"]["x"].get<int>(), rect.left);
    ASSERT_EQ(saved["status_window"]["y"].get<int>(), rect.top);
    ASSERT_TRUE(saved["status_window"]["enable"].get<bool>());
}

} // namespace
