// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "edit_target.h"

#include <algorithm>
#include <cwchar>
#include <new>

#include <initguid.h>
#include <inputscope.h>

#include "globals.h"

namespace {

bool is_valid_rect(const RECT& rc) {
    return (rc.left != 0 || rc.top != 0) && rc.right >= rc.left && rc.bottom >= rc.top;
}

void normalize_rect_size(RECT* rc) {
    if (rc->right < rc->left) {
        std::swap(rc->left, rc->right);
    }
    if (rc->bottom < rc->top) {
        std::swap(rc->top, rc->bottom);
    }
    if (rc->right == rc->left) {
        rc->right = rc->left + 1;
    }
    if (rc->bottom - rc->top <= 2) {
        rc->bottom = rc->top + 20;
    }
}

bool rect_primary_point_in_rect(const RECT& outer, const RECT& inner) {
    return inner.left >= outer.left && inner.left <= outer.right && inner.top >= outer.top &&
           inner.top <= outer.bottom;
}

bool rect_has_area(const RECT& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

bool caret_origin_in_view(const RECT& view_rect, const RECT& caret_rect) {
    return rect_has_area(view_rect) && is_valid_rect(caret_rect) &&
           caret_rect.left >= view_rect.left && caret_rect.left < view_rect.right &&
           caret_rect.top >= view_rect.top && caret_rect.top < view_rect.bottom;
}

bool same_root_window(HWND a, HWND b) {
    if (!a || !b) {
        return false;
    }
    if (a == b || IsChild(a, b) || IsChild(b, a)) {
        return true;
    }
    HWND root_a = GetAncestor(a, GA_ROOT);
    HWND root_b = GetAncestor(b, GA_ROOT);
    return root_a && root_a == root_b;
}

bool map_client_rect_to_screen(HWND hwnd, const RECT& raw, RECT* mapped) {
    if (!hwnd || !mapped) {
        return false;
    }
    RECT client = {};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }
    if (raw.left < client.left || raw.left > client.right || raw.top < client.top ||
        raw.top > client.bottom) {
        return false;
    }
    POINT points[2] = {
        {raw.left, raw.top},
        {raw.right, raw.bottom}
    };
    if (!MapWindowPoints(hwnd, nullptr, points, 2)) {
        return false;
    }
    SetRect(mapped, points[0].x, points[0].y, points[1].x, points[1].y);
    normalize_rect_size(mapped);
    return is_valid_rect(*mapped);
}

} // namespace

namespace cxxime_tsf {

bool CaretViewportTracker::remember(std::uint64_t target_generation, const RECT& view_rect,
                                    const RECT& caret_rect) {
    if (!caret_origin_in_view(view_rect, caret_rect)) {
        return false;
    }
    target_generation_ = target_generation;
    view_rect_ = view_rect;
    caret_rect_ = caret_rect;
    valid_ = true;
    return true;
}

CaretViewportFallback CaretViewportTracker::resolve(std::uint64_t target_generation,
                                                    const RECT* view_rect, const RECT* logical_rect,
                                                    bool clipped, RECT* caret_rect) const {
    if (!caret_rect) {
        return CaretViewportFallback::None;
    }

    const bool has_anchor = valid_ && target_generation_ == target_generation;
    RECT anchor = {};
    if (has_anchor) {
        anchor = caret_rect_;
        if (view_rect && rect_has_area(*view_rect) && rect_has_area(view_rect_)) {
            OffsetRect(&anchor, view_rect->left - view_rect_.left,
                       view_rect->top - view_rect_.top);
        }
    }

    if (view_rect && logical_rect && !clipped && rect_has_area(*view_rect) &&
        is_valid_rect(*logical_rect)) {
        const bool vertically_hidden =
            logical_rect->bottom <= view_rect->top || logical_rect->top >= view_rect->bottom;
        const bool horizontal_position_available =
            logical_rect->left >= view_rect->left && logical_rect->left < view_rect->right;
        if (vertically_hidden && horizontal_position_available) {
            if (has_anchor) {
                OffsetRect(&anchor, logical_rect->left - anchor.left, 0);
                *caret_rect = anchor;
                return CaretViewportFallback::Projected;
            }

            RECT boundary = *logical_rect;
            normalize_rect_size(&boundary);
            const LONG caret_height = boundary.bottom - boundary.top;
            const LONG view_height = view_rect->bottom - view_rect->top;
            if (caret_height <= view_height) {
                const LONG edge = logical_rect->top >= view_rect->bottom
                                      ? view_rect->bottom - boundary.bottom
                                      : view_rect->top - boundary.top;
                OffsetRect(&boundary, 0, edge);
                *caret_rect = boundary;
                return CaretViewportFallback::Boundary;
            }
        }
    }

    if (!has_anchor) {
        return CaretViewportFallback::None;
    }
    *caret_rect = anchor;
    return CaretViewportFallback::Anchor;
}

bool map_fallback_caret_rect(HWND caret_window, POINT caret, RECT* rect) {
    if (!caret_window || !rect || !ClientToScreen(caret_window, &caret)) {
        return false;
    }
    OffsetRect(rect, caret.x - rect->left, caret.y - rect->top);
    return true;
}

bool map_current_thread_caret_rect(HWND foreground, RECT* rect, TextExtRectTrace* trace) {
    if (!rect || !foreground) {
        return false;
    }
    GUITHREADINFO gui = {sizeof(gui)};
    const bool gui_ok = GetGUIThreadInfo(GetCurrentThreadId(), &gui) != FALSE;
    if (trace) {
        trace->gui_info_ok = gui_ok;
        trace->caret_hwnd = gui_ok ? gui.hwndCaret : nullptr;
    }
    if (!gui_ok || !gui.hwndCaret || !same_root_window(foreground, gui.hwndCaret)) {
        if (trace) {
            // The old fallback could use GetCaretPos even without a confirmed owner.
            trace->caret_pos_queried = true;
            trace->caret_pos_ok = GetCaretPos(&trace->caret_pos) != FALSE;
        }
        return false;
    }
    POINT caret = {};
    const bool caret_ok = GetCaretPos(&caret) != FALSE;
    if (trace) {
        trace->caret_pos_queried = true;
        trace->caret_pos_ok = caret_ok;
        trace->caret_pos = caret;
    }
    const bool mapped = caret_ok && map_fallback_caret_rect(gui.hwndCaret, caret, rect);
    if (trace) {
        trace->caret_map_ok = mapped;
    }
    return mapped;
}

bool resolve_native_caret_rect(HWND foreground, RECT* out) {
    if (!out) {
        return false;
    }
    GUITHREADINFO gti = {sizeof(gti)};
    DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    if (foreground_thread && GetGUIThreadInfo(foreground_thread, &gti) && gti.hwndCaret &&
        GetAncestor(gti.hwndCaret, GA_ROOT) != gti.hwndCaret &&
        same_root_window(foreground, gti.hwndCaret)) {
        RECT rc = gti.rcCaret;
        POINT points[2] = {
            {rc.left, rc.top},
            {rc.right, rc.bottom}
        };
        MapWindowPoints(gti.hwndCaret, nullptr, points, 2);
        SetRect(&rc, points[0].x, points[0].y, points[1].x, points[1].y);
        normalize_rect_size(&rc);
        if (is_valid_rect(rc)) {
            *out = rc;
            return true;
        }
    }

    RECT rc = {0, 0, 1, 20};
    if (map_current_thread_caret_rect(foreground, &rc) && is_valid_rect(rc)) {
        *out = rc;
        return true;
    }
    return false;
}

bool normalize_text_ext_rect(HWND view_hwnd, HWND foreground, RECT* rc,
                             TextExtRectTrace* trace) {
    if (!rc || !is_valid_rect(*rc)) {
        if (trace) {
            trace->branch = "invalid_rect";
        }
        return false;
    }
    RECT foreground_rect = {};
    bool has_foreground_rect = foreground && GetWindowRect(foreground, &foreground_rect);
    normalize_rect_size(rc);

    if (has_foreground_rect && rect_primary_point_in_rect(foreground_rect, *rc)) {
        if (trace) {
            trace->branch = "foreground_screen";
        }
        return true;
    }
    RECT mapped = {};
    if (map_client_rect_to_screen(view_hwnd, *rc, &mapped)) {
        if (!has_foreground_rect || rect_primary_point_in_rect(foreground_rect, mapped)) {
            *rc = mapped;
            if (trace) {
                trace->branch = "view_client";
            }
            return true;
        }
    }
    if (has_foreground_rect && trace) {
        trace->focus_hwnd = GetFocus();
    }
    if (has_foreground_rect && map_current_thread_caret_rect(foreground, rc, trace)) {
        normalize_rect_size(rc);
        if (trace) {
            trace->branch = "native_caret";
        }
        return is_valid_rect(*rc);
    }
    const bool on_monitor = MonitorFromRect(rc, MONITOR_DEFAULTTONULL) != nullptr;
    if (trace) {
        trace->branch = on_monitor ? "monitor" : "unresolved";
    }
    return on_monitor;
}

bool text_rect_is_outside_view(HRESULT screen_rect_hr, const RECT& screen_rect,
    HRESULT text_rect_hr, const RECT& text_rect, bool text_clipped) {
    const bool screen_rect_valid = SUCCEEDED(screen_rect_hr) &&
                                   screen_rect.right > screen_rect.left &&
                                   screen_rect.bottom > screen_rect.top;
    return SUCCEEDED(text_rect_hr) && screen_rect_valid && !text_clipped &&
           (text_rect.right <= screen_rect.left || text_rect.left >= screen_rect.right ||
            text_rect.bottom <= screen_rect.top || text_rect.top >= screen_rect.bottom);
}

bool text_rect_is_placeholder(const RECT& view_rect, const RECT& text_rect) {
    const bool valid_view =
        view_rect.right > view_rect.left && view_rect.bottom > view_rect.top;
    const bool valid_text =
        text_rect.right > text_rect.left && text_rect.bottom > text_rect.top;
    if (!valid_view || !valid_text || text_rect.right - text_rect.left > 2 ||
        view_rect.right - view_rect.left <= 100 ||
        view_rect.bottom - view_rect.top <= 100) {
        return false;
    }

    constexpr LONG kBoundaryTolerance = 2;
    const bool at_view_origin =
        text_rect.left >= view_rect.left - kBoundaryTolerance &&
        text_rect.left <= view_rect.left + kBoundaryTolerance &&
        text_rect.top >= view_rect.top - kBoundaryTolerance &&
        text_rect.top <= view_rect.top + kBoundaryTolerance;
    return at_view_origin;
}

bool text_rect_requires_composition_refresh(const RECT& view_rect,
    const RECT& text_rect) {
    const bool valid_view =
        view_rect.right > view_rect.left && view_rect.bottom > view_rect.top;
    const bool valid_text =
        text_rect.right >= text_rect.left && text_rect.bottom >= text_rect.top;
    if (!valid_view || !valid_text || text_rect.right - text_rect.left > 2 ||
        view_rect.right - view_rect.left <= 100 ||
        view_rect.bottom - view_rect.top <= 100) {
        return false;
    }

    constexpr LONG kBoundaryTolerance = 2;
    const bool at_view_origin =
        text_rect.left >= view_rect.left - kBoundaryTolerance &&
        text_rect.left <= view_rect.left + kBoundaryTolerance &&
        text_rect.top >= view_rect.top - kBoundaryTolerance &&
        text_rect.top <= view_rect.top + kBoundaryTolerance;
    // Qt can expose an uninitialized selection just beyond a full-screen view's right edge.
    const bool outside_right_boundary =
        text_rect.left >= view_rect.right &&
        text_rect.left <= view_rect.right + kBoundaryTolerance &&
        text_rect.right > view_rect.right &&
        text_rect.top >= view_rect.top &&
        text_rect.top < view_rect.bottom;
    return at_view_origin || outside_right_boundary;
}

bool text_rect_is_meaningful(HRESULT text_rect_hr, const RECT& text_rect,
    bool placeholder_text_rect) {
    return SUCCEEDED(text_rect_hr) && text_rect.right > text_rect.left &&
           text_rect.bottom > text_rect.top && !placeholder_text_rect;
}

namespace {

bool is_shell_process(HWND hwnd) {
    if (!hwnd) {
        return false;
    }

    DWORD process_id = 0;
    if (!GetWindowThreadProcessId(hwnd, &process_id) || !process_id) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) {
        return false;
    }

    wchar_t path[MAX_PATH] = {};
    DWORD path_length = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    const bool queried = QueryFullProcessImageNameW(process, 0, path, &path_length) != FALSE;
    CloseHandle(process);
    if (!queried) {
        return false;
    }

    const wchar_t* filename = std::wcsrchr(path, L'\\');
    filename = filename ? filename + 1 : path;
    return _wcsicmp(filename, L"explorer.exe") == 0;
}

class EditTargetSession final : public ITfEditSession {
public:
    EditTargetSession(ITfContext* context, EditTargetEvidence* evidence)
        : context_(context)
        , evidence_(evidence) {
        context_->AddRef();
        DllAddRef();
    }
    ~EditTargetSession() {
        context_->Release();
        DllRelease();
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_INVALIDARG;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfEditSession)) {
            *object = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_count_); }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
        TF_SELECTION selection = {};
        evidence_->selection_hr = context_->GetSelection(edit_cookie, TF_DEFAULT_SELECTION, 1,
            &selection, &evidence_->selection_count);
        evidence_->selection_available = SUCCEEDED(evidence_->selection_hr) &&
            evidence_->selection_count == 1 && selection.range;
        if (!evidence_->selection_available) {
            if (selection.range) {
                selection.range->Release();
            }
            return S_OK;
        }

        evidence_->selection_ase = selection.style.ase;
        evidence_->selection_interim = selection.style.fInterimChar != FALSE;
        evidence_->has_active_selection =
            evidence_->selection_interim || selection.style.ase != TF_AE_NONE;
        inspect_input_scope(edit_cookie, selection.range);
        inspect_view(edit_cookie, selection.range);
        selection.range->Release();
        return S_OK;
    }

private:

    void inspect_input_scope(TfEditCookie edit_cookie, ITfRange* range) {
        ITfReadOnlyProperty* property = nullptr;
        evidence_->input_scope_property_hr =
            context_->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);

        VARIANT value = {};
        VariantInit(&value);
        evidence_->input_scope_value_hr = SUCCEEDED(evidence_->input_scope_property_hr) && property
            ? property->GetValue(edit_cookie, range, &value)
            : E_UNEXPECTED;
        if (property) {
            property->Release();
        }

        if (SUCCEEDED(evidence_->input_scope_value_hr) && value.vt == VT_UNKNOWN && value.punkVal) {
            ITfInputScope* input_scope = nullptr;
            evidence_->input_scope_interface_hr = value.punkVal->QueryInterface(
                IID_ITfInputScope, reinterpret_cast<void**>(&input_scope));
            if (SUCCEEDED(evidence_->input_scope_interface_hr) && input_scope) {
                InputScope* values = nullptr;
                UINT count = 0;
                evidence_->input_scopes_hr = input_scope->GetInputScopes(&values, &count);
                evidence_->input_scope_count = count;
                if (SUCCEEDED(evidence_->input_scopes_hr) && count > 0 && values) {
                    evidence_->first_input_scope = static_cast<int32_t>(values[0]);
                }
                evidence_->has_input_scope = SUCCEEDED(evidence_->input_scopes_hr) && count > 0;
                CoTaskMemFree(values);
                input_scope->Release();
            }
        }
        VariantClear(&value);
    }

    void inspect_view(TfEditCookie edit_cookie, ITfRange* range) {
        ITfContextView* view = nullptr;
        evidence_->view_hr = context_->GetActiveView(&view);
        evidence_->window_hr = view ? view->GetWnd(&evidence_->context_hwnd) : E_POINTER;
        evidence_->screen_rect_hr = view ? view->GetScreenExt(&evidence_->screen_rect) : E_POINTER;
        BOOL clipped = FALSE;
        evidence_->text_rect_hr =
            view ? view->GetTextExt(edit_cookie, range, &evidence_->text_rect, &clipped)
                 : E_POINTER;
        evidence_->text_clipped = clipped != FALSE;
        if (view) {
            view->Release();
        }

        const DWORD context_thread_id =
            evidence_->context_hwnd ? GetWindowThreadProcessId(evidence_->context_hwnd, nullptr)
                                    : 0;
        GUITHREADINFO gui_thread_info = {sizeof(gui_thread_info)};
        SetLastError(ERROR_SUCCESS);
        evidence_->gui_thread_info_ok =
            context_thread_id && GetGUIThreadInfo(context_thread_id, &gui_thread_info) != FALSE;
        evidence_->gui_thread_info_error =
            evidence_->gui_thread_info_ok ? ERROR_SUCCESS : GetLastError();
        evidence_->caret_hwnd = gui_thread_info.hwndCaret;
        evidence_->focus_hwnd = gui_thread_info.hwndFocus;
        evidence_->foreground_hwnd = GetForegroundWindow();
        const HWND foreground_root = evidence_->foreground_hwnd
                                        ? GetAncestor(evidence_->foreground_hwnd, GA_ROOT)
                                        : nullptr;
        evidence_->foreground_is_shell_window =
            foreground_root && foreground_root == GetShellWindow();
        evidence_->context_is_focused_child =
            evidence_->gui_thread_info_ok && evidence_->context_hwnd &&
            evidence_->focus_hwnd == evidence_->context_hwnd && evidence_->foreground_hwnd &&
            evidence_->context_hwnd != evidence_->foreground_hwnd &&
            IsChild(evidence_->foreground_hwnd, evidence_->context_hwnd) != FALSE;
        const HWND context_root =
            evidence_->context_hwnd ? GetAncestor(evidence_->context_hwnd, GA_ROOT) : nullptr;
        const HWND caret_root =
            evidence_->caret_hwnd ? GetAncestor(evidence_->caret_hwnd, GA_ROOT) : nullptr;
        evidence_->has_native_caret =
            evidence_->gui_thread_info_ok && context_root && caret_root == context_root;

        const bool text_rect_valid = SUCCEEDED(evidence_->text_rect_hr) &&
            evidence_->text_rect.right > evidence_->text_rect.left &&
            evidence_->text_rect.bottom > evidence_->text_rect.top;
        const bool screen_rect_valid = SUCCEEDED(evidence_->screen_rect_hr) &&
            evidence_->screen_rect.right > evidence_->screen_rect.left &&
            evidence_->screen_rect.bottom > evidence_->screen_rect.top;
        constexpr LONG kOriginTolerance = 2;
        evidence_->text_rect_at_view_origin =
            text_rect_valid && screen_rect_valid &&
            evidence_->text_rect.left >= evidence_->screen_rect.left - kOriginTolerance &&
            evidence_->text_rect.left <= evidence_->screen_rect.left + kOriginTolerance &&
            evidence_->text_rect.top >= evidence_->screen_rect.top - kOriginTolerance &&
            evidence_->text_rect.top <= evidence_->screen_rect.top + kOriginTolerance;
        evidence_->placeholder_text_rect =
            text_rect_is_placeholder(evidence_->screen_rect, evidence_->text_rect);
        evidence_->text_rect_outside_view = text_rect_is_outside_view(
            evidence_->screen_rect_hr, evidence_->screen_rect, evidence_->text_rect_hr,
            evidence_->text_rect, evidence_->text_clipped);
        evidence_->has_meaningful_text_rect = text_rect_is_meaningful(
            evidence_->text_rect_hr, evidence_->text_rect, evidence_->placeholder_text_rect);
        if (!evidence_->foreground_is_shell_window && foreground_root &&
            !evidence_->has_active_selection && !evidence_->has_input_scope &&
            !evidence_->has_native_caret && !evidence_->has_meaningful_text_rect) {
            evidence_->foreground_is_shell_window = is_shell_process(foreground_root);
        }
    }

    LONG ref_count_ = 1;
    ITfContext* context_ = nullptr;
    EditTargetEvidence* evidence_ = nullptr;
};

} // namespace

EditTargetState classify_edit_target(const EditTargetEvidence& evidence) {
    if (FAILED(evidence.request_hr) || FAILED(evidence.session_hr) ||
        !evidence.selection_available) {
        return EditTargetState::Unknown;
    }
    if (evidence.has_active_selection || evidence.has_input_scope || evidence.has_native_caret ||
        evidence.has_meaningful_text_rect) {
        return EditTargetState::Editable;
    }
    if (evidence.placeholder_text_rect) {
        return EditTargetState::NoEditTarget;
    }
    if (evidence.foreground_is_shell_window) {
        return EditTargetState::NoEditTarget;
    }
    return evidence.context_is_focused_child ? EditTargetState::Unknown
                                             : EditTargetState::NoEditTarget;
}

EditTargetState inspect_edit_target(ITfContext* context, TfClientId client_id,
    EditTargetEvidence* evidence) {
    if (!evidence) {
        return EditTargetState::Unknown;
    }
    *evidence = {};
    if (!context || client_id == TF_CLIENTID_NULL) {
        evidence->request_hr = context ? E_UNEXPECTED : E_POINTER;
        return EditTargetState::Unknown;
    }

    auto* session = new (std::nothrow) EditTargetSession(context, evidence);
    if (!session) {
        evidence->request_hr = E_OUTOFMEMORY;
        return EditTargetState::Unknown;
    }
    evidence->request_hr = context->RequestEditSession(client_id, session, TF_ES_SYNC | TF_ES_READ,
        &evidence->session_hr);
    session->Release();
    return classify_edit_target(*evidence);
}

const char* edit_target_state_name(EditTargetState state) {
    switch (state) {
    case EditTargetState::Editable:
        return "editable";
    case EditTargetState::NoEditTarget:
        return "no_edit_target";
    case EditTargetState::Unknown:
    default:
        return "unknown";
    }
}

} // namespace cxxime_tsf
