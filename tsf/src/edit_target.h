// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_EDIT_TARGET_H_
#define CXXIME_TSF_EDIT_TARGET_H_

#include <cstdint>

#include <windows.h>
#include <msctf.h>

namespace cxxime_tsf {

enum class EditTargetState : uint8_t {
    Unknown = 0,
    Editable,
    NoEditTarget,
};

struct EditTargetEvidence {
    HRESULT request_hr = E_UNEXPECTED;
    HRESULT session_hr = E_UNEXPECTED;
    HRESULT selection_hr = E_UNEXPECTED;
    ULONG selection_count = 0;
    TfActiveSelEnd selection_ase = TF_AE_NONE;
    bool selection_interim = false;
    bool selection_available = false;
    HRESULT input_scope_property_hr = E_UNEXPECTED;
    HRESULT input_scope_value_hr = E_UNEXPECTED;
    HRESULT input_scope_interface_hr = E_UNEXPECTED;
    HRESULT input_scopes_hr = E_UNEXPECTED;
    UINT input_scope_count = 0;
    int32_t first_input_scope = 0;
    bool has_input_scope = false;
    bool has_active_selection = false;
    HRESULT view_hr = E_UNEXPECTED;
    HRESULT window_hr = E_UNEXPECTED;
    HWND context_hwnd = nullptr;
    bool gui_thread_info_ok = false;
    DWORD gui_thread_info_error = ERROR_SUCCESS;
    HWND caret_hwnd = nullptr;
    bool has_native_caret = false;
    HWND focus_hwnd = nullptr;
    HWND foreground_hwnd = nullptr;
    bool foreground_is_shell_window = false; // Desktop or Explorer shell surface.
    bool context_is_focused_child = false;
    HRESULT screen_rect_hr = E_UNEXPECTED;
    RECT screen_rect = {};
    HRESULT text_rect_hr = E_UNEXPECTED;
    RECT text_rect = {};
    bool text_clipped = false;
    bool text_rect_at_view_origin = false;
    bool placeholder_text_rect = false;
    bool text_rect_outside_view = false;
    bool has_meaningful_text_rect = false;
};

struct TextExtRectTrace {
    RECT raw = {};
    RECT result = {};
    HWND view_hwnd = nullptr;
    HWND foreground_hwnd = nullptr;
    HWND caret_hwnd = nullptr;
    HWND focus_hwnd = nullptr;
    POINT caret_pos = {};
    HRESULT text_ext_hr = E_UNEXPECTED;
    const char* branch = "text_ext_failed";
    bool clipped = false;
    bool resolved = false;
    bool gui_info_ok = false;
    bool caret_pos_queried = false;
    bool caret_pos_ok = false;
    bool caret_map_ok = false;
};

enum class CaretViewportFallback : uint8_t {
    None = 0,
    Anchor,
    Projected,
    Boundary,
};

class CaretViewportTracker {
public:
    bool remember(std::uint64_t target_generation,
                  const RECT& view_rect,
                  const RECT& caret_rect);
    CaretViewportFallback resolve(std::uint64_t target_generation,
                                  const RECT* view_rect,
                                  const RECT* logical_rect,
                                  bool clipped,
                                  RECT* caret_rect) const;

private:
    std::uint64_t target_generation_ = 0;
    RECT view_rect_ = {};
    RECT caret_rect_ = {};
    bool valid_ = false;
};

bool text_rect_is_outside_view(HRESULT screen_rect_hr, const RECT& screen_rect,
    HRESULT text_rect_hr, const RECT& text_rect, bool text_clipped);
bool text_rect_is_placeholder(const RECT& view_rect, const RECT& text_rect);
bool text_rect_requires_composition_refresh(const RECT& view_rect, const RECT& text_rect);
bool text_rect_is_meaningful(HRESULT text_rect_hr, const RECT& text_rect,
    bool placeholder_text_rect);
bool map_fallback_caret_rect(HWND caret_window, POINT caret, RECT* rect);
bool map_current_thread_caret_rect(HWND foreground, RECT* rect,
                                   TextExtRectTrace* trace = nullptr);
bool resolve_native_caret_rect(HWND foreground, RECT* rect);
bool normalize_text_ext_rect(HWND view_hwnd, HWND foreground, RECT* rect,
                             TextExtRectTrace* trace = nullptr);
EditTargetState classify_edit_target(const EditTargetEvidence& evidence);
EditTargetState inspect_edit_target(ITfContext* context, TfClientId client_id,
    EditTargetEvidence* evidence);
const char* edit_target_state_name(EditTargetState state);

} // namespace cxxime_tsf

#endif // CXXIME_TSF_EDIT_TARGET_H_
