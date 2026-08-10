// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_WINDOW_OWNER_PROBE_H_
#define CXXIME_HOST_TAKEOVER_WINDOW_OWNER_PROBE_H_

#include <windows.h>

#include <string>
#include <vector>

namespace cxxime_probe {

class WindowOwnerProbe {
public:
    bool initialize(HINSTANCE instance);
    int run();
    void shutdown();
    const std::wstring& initialization_error() const;

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static void CALLBACK win_event_proc(HWINEVENTHOOK hook,
                                        DWORD event,
                                        HWND hwnd,
                                        LONG object_id,
                                        LONG child_id,
                                        DWORD event_thread,
                                        DWORD event_time);
    static BOOL CALLBACK enum_window_proc(HWND hwnd, LPARAM context);

    bool install_hook(DWORD event_min, DWORD event_max);
    bool fail_initialization(const char* step, DWORD error);
    void trace_window(const char* action,
                      DWORD event,
                      HWND hwnd,
                      LONG object_id,
                      LONG child_id,
                      DWORD event_thread,
                      DWORD event_time);
    void trace_initial_windows();
    void paint(HDC dc);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::wstring initialization_error_;
    std::vector<HWINEVENTHOOK> hooks_;
};

} // namespace cxxime_probe

#endif // CXXIME_HOST_TAKEOVER_WINDOW_OWNER_PROBE_H_
