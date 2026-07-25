// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_PROBE_APP_H_
#define CXXIME_HOST_TAKEOVER_PROBE_APP_H_

#include <windows.h>
#include <imm.h>
#include <msctf.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cxxime_probe {

inline constexpr wchar_t kWindowClass[] = L"CxxImeHostProbeWindow";
inline constexpr int kGateCheckboxId = 1001;
inline constexpr int kOriginalUiCheckboxId = 1002;
inline constexpr UINT_PTR kCandidateUiVisibilityTimerId = 1;

enum class CandidateUiVisibilityCycle {
disabled,
armed,
waiting_to_show,
waiting_to_hide,
completed,
};

class UiElementSink;

class ProbeApp {
public:
    bool initialize(HINSTANCE instance);
    int run();
    void shutdown();
    const std::wstring& initialization_error() const;

    HRESULT on_begin_ui_element(DWORD element_id, BOOL* show);
    HRESULT on_update_ui_element(DWORD element_id);
    HRESULT on_end_ui_element(DWORD element_id);

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void update_ui_element(DWORD element_id, const char* action);
    void update_candidate(ITfUIElement* element, DWORD element_id, const char* action);
    void update_reading(ITfUIElement* element, DWORD element_id, const char* action);
    bool apply_candidate_ui_visibility(const char* trigger);
    void set_candidate_ui_visibility_cycle(bool enabled, const char* trigger);
    void schedule_candidate_ui_visibility_cycle(const char* trigger);
    void advance_candidate_ui_visibility_cycle();
    void reset_candidate_ui_visibility_cycle(const char* trigger);
    void read_composition(LPARAM flags);
    void trace_imm_candidate_snapshot(const char* trigger,
                                      DWORD element_id,
                                      const char* action);
    void trace_ime_message(UINT message,
                           WPARAM command,
                           LPARAM flags,
                           const char* action);
    void paint(HDC dc);
    bool candidate_should_draw() const;
    uint64_t ensure_composition_id();
    bool fail_initialization(const char* stage, HRESULT result);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND gate_checkbox_ = nullptr;
    HWND original_ui_checkbox_ = nullptr;
    HIMC himc_ = nullptr;
    ITfThreadMgrEx* thread_mgr_ = nullptr;
    ITfUIElementMgr* ui_element_mgr_ = nullptr;
    ITfSource* source_ = nullptr;
    UiElementSink* sink_ = nullptr;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    DWORD sink_cookie_ = TF_INVALID_COOKIE;
    bool com_initialized_ = false;
    bool thread_mgr_active_ = false;
    bool composition_active_ = false;
    bool gate_on_signal_ = false;
    bool original_candidate_ui_requested_ = false;
    bool original_candidate_ui_shown_ = false;
    bool candidate_ui_visibility_pending_ = false;
    bool candidate_ui_visibility_cycle_enabled_ = false;
    CandidateUiVisibilityCycle candidate_ui_visibility_cycle_ =
        CandidateUiVisibilityCycle::disabled;
    uint64_t composition_id_ = 0;
    DWORD candidate_element_id_ = TF_INVALID_UIELEMENTID;
    DWORD reading_element_id_ = TF_INVALID_UIELEMENTID;
    UINT selection_ = 0;
    UINT current_page_ = 0;
    std::wstring composition_;
    std::wstring reading_;
    std::wstring committed_;
    std::wstring initialization_error_;
    std::vector<std::wstring> candidates_;
};

} // namespace cxxime_probe

#endif // CXXIME_HOST_TAKEOVER_PROBE_APP_H_
