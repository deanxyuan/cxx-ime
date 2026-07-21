// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include "ui_element_sink.h"

#include <cxxime/stage_trace.h>

#include <ctfutb.h>

#include <new>

namespace cxxime_probe {

bool ProbeApp::initialize(HINSTANCE instance) {
    instance_ = instance;
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    com_initialized_ = SUCCEEDED(com_hr);
    if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = window_proc;
    window_class.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kWindowClass, L"CxxIME Host Candidate Probe",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                            760, 560, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        return false;
    }

    gate_checkbox_ = CreateWindowExW(
        0, L"BUTTON", L"Require composition/reading signal to show candidates",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 20, 470, 26, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGateCheckboxId)), instance_, nullptr);

    himc_ = ImmGetContext(hwnd_);
    const HRESULT create_hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_ITfThreadMgrEx,
                                               reinterpret_cast<void**>(&thread_mgr_));
    if (FAILED(create_hr) || !thread_mgr_) {
        return false;
    }

    const HRESULT activate_hr = thread_mgr_->ActivateEx(
        &client_id_, TF_TMAE_UIELEMENTENABLEDONLY);
    if (FAILED(activate_hr)) {
        return false;
    }
    thread_mgr_active_ = true;

    if (FAILED(thread_mgr_->QueryInterface(
            IID_ITfUIElementMgr, reinterpret_cast<void**>(&ui_element_mgr_))) ||
        !ui_element_mgr_) {
        return false;
    }
    if (FAILED(thread_mgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source_))) ||
        !source_) {
        return false;
    }

    sink_ = new (std::nothrow) UiElementSink(this);
    if (!sink_) {
        return false;
    }
    const HRESULT advise_hr = source_->AdviseSink(
        IID_ITfUIElementSink, static_cast<ITfUIElementSink*>(sink_), &sink_cookie_);
    if (FAILED(advise_hr)) {
        return false;
    }

    cxxime::write_stage_trace("probe", "probe.runtime", {
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd_)},
        {"himc", reinterpret_cast<uintptr_t>(himc_)},
        {"activate_flags", TF_TMAE_UIELEMENTENABLEDONLY},
        {"client_id", client_id_},
        {"result", "ready"},
    });
    SetFocus(hwnd_);
    return true;
}

int ProbeApp::run() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void ProbeApp::shutdown() {
    if (source_ && sink_cookie_ != TF_INVALID_COOKIE) {
        source_->UnadviseSink(sink_cookie_);
        sink_cookie_ = TF_INVALID_COOKIE;
    }
    if (sink_) {
        sink_->Release();
        sink_ = nullptr;
    }
    if (source_) {
        source_->Release();
        source_ = nullptr;
    }
    if (ui_element_mgr_) {
        ui_element_mgr_->Release();
        ui_element_mgr_ = nullptr;
    }
    if (thread_mgr_) {
        if (thread_mgr_active_) {
            thread_mgr_->Deactivate();
        }
        thread_mgr_->Release();
        thread_mgr_ = nullptr;
    }
    if (himc_ && hwnd_) {
        ImmReleaseContext(hwnd_, himc_);
        himc_ = nullptr;
    }
    if (com_initialized_) {
        CoUninitialize();
    }
}

} // namespace cxxime_probe
