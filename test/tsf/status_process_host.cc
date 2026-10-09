// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "status_process_host.h"

#include "globals.h"
#include "text_service_test_support.h"

namespace status_test {
namespace {

// Each child owns a real TextService, independent IPC session and Win32 message loop.
// The TSF host, foreground sample and thread keyboard table are controlled test inputs.
// Both processes share a real hidden HWND; the user's desktop cannot change the scenario.
class Host {
public:
    explicit Host(const std::wstring& pipe) {
        ASSERT_TRUE(GetKeyboardState(original_keyboard_));
        manager_.document.top = &context_;
        manager_.focused = FALSE;
        context_.document = &manager_.document;
        context_.active_view = &view_;
        view_.foreground_window = true;
        TextServiceTestPeer::set_thread_manager(service_, &manager_);
        ASSERT_TRUE(TextServiceTestPeer::connect(service_, pipe));
        TextServiceTestPeer::start_status_dispatch(service_);
        TextServiceTestPeer::start_ui(service_, pipe + L"_ui", true);
    }
    ~Host() {
        TextServiceTestPeer::stop_ui(service_);
        TextServiceTestPeer::stop_status_dispatch(service_);
        SetKeyboardState(original_keyboard_);
    }

    void apply_keyboard_sample() {
        BYTE state[256] = {};
        state[VK_CAPITAL] = caps_ ? 1 : 0;
        ASSERT_TRUE(SetKeyboardState(state));
        // Verify the same sample API used by TextService, without repairing a failed setup.
        BYTE observed[256] = {};
        ASSERT_TRUE(GetKeyboardState(observed));
        ASSERT_EQ((observed[VK_CAPITAL] & 1) != 0, caps_)
            << "GetKeyState=" << GetKeyState(VK_CAPITAL) << " foreground=" << GetForegroundWindow();
    }

    void trace_timer(WPARAM timer, const char* phase) {
        const auto status = TextServiceTestPeer::status(service_);
        fprintf(stderr, "timer %s id=%llu expected=%d sample=%d local=%d foreground=%p\n", phase,
                static_cast<unsigned long long>(timer), caps_ ? 1 : 0,
                (GetKeyState(VK_CAPITAL) & 1) != 0, status.caps_lock() ? 1 : 0,
                GetForegroundWindow());
    }

    LRESULT command(HostCommand command, bool caps) {
        switch (command) {
        case HostCommand::Session:
            return TextServiceTestPeer::session_id(service_);
        case HostCommand::Focus:
            manager_.focused = TRUE;
            fprintf(stderr, "focus begin foreground=%p\n", GetForegroundWindow());
            // At callback time the old thread sample deliberately disagrees with the
            // requested state. It catches up before the automatic one-shot runs.
            caps_ = !caps;
            apply_keyboard_sample();
            ASSERT_EQ(service_.OnSetThreadFocus(), S_OK);
            fprintf(
                stderr, "focus end foreground=%p target=%llu timer=%llu\n", GetForegroundWindow(),
                static_cast<unsigned long long>(TextServiceTestPeer::target_generation(service_)),
                static_cast<unsigned long long>(TextServiceTestPeer::status_timer(service_)));
            ASSERT_NE(TextServiceTestPeer::status_timer(service_), static_cast<UINT_PTR>(0));
            // While paused, retain the first ID to replay after a later focus replaces it.
            if (!timers_paused_ || !last_focus_timer_) {
                last_focus_timer_ = TextServiceTestPeer::status_timer(service_);
            }
            caps_ = caps;
            apply_keyboard_sample();
            return static_cast<LRESULT>(TextServiceTestPeer::target_generation(service_));
        case HostCommand::Blur:
            manager_.focused = FALSE;
            ASSERT_EQ(service_.OnKillThreadFocus(), S_OK);
            return 1;
        case HostCommand::Keyboard:
            caps_ = caps;
            apply_keyboard_sample();
            return 1;
        case HostCommand::LateTimer:
            caps_ = caps;
            apply_keyboard_sample();
            TextServiceTestPeer::dispatch_status_timer(service_, last_focus_timer_);
            return 1;
        case HostCommand::PauseTimers:
            timers_paused_ = caps;
            if (timers_paused_) {
                last_focus_timer_ = 0;
            }
            return 1;
        case HostCommand::PendingTimer:
            return static_cast<LRESULT>(TextServiceTestPeer::status_timer(service_));
        case HostCommand::ConfirmFocus: {
            apply_keyboard_sample();
            const auto timer = TextServiceTestPeer::status_timer(service_);
            if (timer) {
                TextServiceTestPeer::dispatch_status_timer(service_, timer);
            }
            return 1;
        }
        case HostCommand::LanguageShortcut: {
            BOOL eaten = FALSE;
            ASSERT_EQ(service_.OnPreservedKey(&context_, c_guidPreservedKey_Toggle, &eaten), S_OK);
            return eaten;
        }
        case HostCommand::CapsKey: {
            caps_ = caps;
            apply_keyboard_sample();
            BOOL eaten = FALSE;
            ASSERT_EQ(service_.OnKeyDown(&context_, VK_CAPITAL, 0, &eaten), S_OK);
            ASSERT_EQ(service_.OnKeyUp(&context_, VK_CAPITAL, 0, &eaten), S_OK);
            return 1;
        }
        }
        return 0;
    }

    bool timers_paused() const { return timers_paused_; }

private:
    tsf_test::HostView view_;
    tsf_test::HostThreadManager manager_;
    tsf_test::HostContext context_;
    TextService service_;
    BYTE original_keyboard_[256] = {};
    bool caps_ = false;
    bool timers_paused_ = false;
    UINT_PTR last_focus_timer_ = 0;
};

LRESULT CALLBACK host_window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* host = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        host = static_cast<Host*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (message == kHostCommand && host) {
        return host->command(static_cast<HostCommand>(wp), lp != 0);
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

} // namespace

int run_host(const std::wstring& pipe, const std::wstring& title) {
    Host host(pipe);
    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = host_window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kHostWindowClass;
    ASSERT_TRUE(RegisterClassW(&window_class));
    // Hidden control window: no foreground activation or synthetic user keystrokes.
    const HWND window = CreateWindowExW(0, kHostWindowClass, title.c_str(), WS_POPUP, 0, 0, 0, 0,
                                        nullptr, nullptr, window_class.hInstance, &host);
    ASSERT_TRUE(window != nullptr);
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // Only test-controlled scheduling is paused. Control messages still run, and
        // the original Win32 timers generate messages again after dispatch resumes.
        if (message.message == WM_TIMER && host.timers_paused()) {
            continue;
        }
        host.apply_keyboard_sample();
        if (message.message == WM_TIMER) {
            host.trace_timer(message.wParam, "before");
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (message.message == WM_TIMER) {
            host.trace_timer(message.wParam, "after");
        }
    }
    return 0;
}

} // namespace status_test
