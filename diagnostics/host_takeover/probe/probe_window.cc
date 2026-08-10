// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <cxxime/host_trace.h>

#include <utility>
#include <windowsx.h>

namespace cxxime_probe {

void ProbeApp::read_composition(LPARAM flags) {
    if (!himc_) {
        return;
    }

    LONG comp_bytes = IMM_ERROR_NODATA;
    LONG result_bytes = IMM_ERROR_NODATA;
    std::string result_digest;
    if ((flags & GCS_COMPSTR) != 0) {
        comp_bytes = ImmGetCompositionStringW(himc_, GCS_COMPSTR, nullptr, 0);
        if (comp_bytes >= 0) {
            std::wstring text(static_cast<size_t>(comp_bytes) / sizeof(wchar_t), L'\0');
            if (comp_bytes > 0) {
                ImmGetCompositionStringW(himc_, GCS_COMPSTR, &text[0], comp_bytes);
            }
            composition_ = std::move(text);
        }
    }
    if ((flags & GCS_RESULTSTR) != 0) {
        result_bytes = ImmGetCompositionStringW(himc_, GCS_RESULTSTR, nullptr, 0);
        if (result_bytes > 0) {
            std::wstring text(static_cast<size_t>(result_bytes) / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(himc_, GCS_RESULTSTR, &text[0], result_bytes);
            result_digest = cxxime::host_trace_digest_utf16(text);
            committed_ += text;
        }
    }
    cxxime::write_host_trace("probe", "probe.imm_read", {
        {"composition_id", ensure_composition_id()},
        {"flags", static_cast<uint64_t>(flags)},
        {"comp_bytes", comp_bytes},
        {"result_bytes", result_bytes},
        {"comp_len", composition_.size()},
        {"comp_digest", cxxime::host_trace_digest_utf16(composition_)},
        {"result_digest", result_digest},
        {"committed_len", committed_.size()},
        {"result", "read"},
    });
    if (result_bytes > 0) {
        finish_candidate_click("committed", result_bytes);
    }
    trace_imm_candidate_snapshot("composition", candidate_element_id_, "update");
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void ProbeApp::trace_ime_message(UINT message,
                                    WPARAM command,
                                    LPARAM flags,
                                    const char* action) {
    cxxime::write_host_trace("probe", "probe.ime_message", {
        {"composition_id", composition_id_},
        {"message", message},
        {"command", static_cast<uint64_t>(command)},
        {"flags", static_cast<uint64_t>(flags)},
        {"action", action ? action : ""},
        {"result", "received"},
    });
}

bool ProbeApp::candidate_should_draw() const {
    if (original_candidate_ui_shown_) {
        return false;
    }
    if (!gate_on_signal_) {
        return true;
    }
    return composition_active_ || !composition_.empty() || !reading_.empty();
}

uint64_t ProbeApp::ensure_composition_id() {
    if (composition_id_ == 0) {
        composition_id_ = cxxime::host_trace_next_id();
    }
    return composition_id_;
}

void ProbeApp::paint(HDC dc) {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(30, 34, 40));

    HFONT font = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));

    int y = 96;
    const std::wstring signal = L"Composition: " + composition_ + L"    Reading: " + reading_;
    TextOutW(dc, 24, y, signal.c_str(), static_cast<int>(signal.size()));
    y += 38;
    const std::wstring committed = L"Committed length: " + std::to_wstring(committed_.size());
    TextOutW(dc, 24, y, committed.c_str(), static_cast<int>(committed.size()));
    y += 46;

    RECT separator = {24, y, client.right - 24, y + 1};
    FillRect(dc, &separator, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    y += 22;

    const bool draw_candidates = candidate_should_draw();
    std::wstring status = L"Host candidate UI: ";
    if (candidates_.empty()) {
        status += L"no candidate snapshot";
    } else if (original_candidate_ui_shown_) {
        status += L"hidden while original TIP UI is visible";
    } else {
        status += draw_candidates ? L"visible" : L"hidden by signal gate";
    }
    TextOutW(dc, 24, y, status.c_str(), static_cast<int>(status.size()));
    y += 38;

    if (draw_candidates) {
        y = kCandidateTop;
        for (size_t index = 0;
             index < candidates_.size() && index < kMaximumCandidateRows;
             ++index) {
            RECT row = {
                kCandidateLeft,
                y,
                client.right - kCandidateRightMargin,
                y + kCandidateRowHeight,
            };
            if (index == selection_) {
                HBRUSH selected = CreateSolidBrush(RGB(218, 235, 255));
                FillRect(dc, &row, selected);
                DeleteObject(selected);
            }
            const std::wstring line = std::to_wstring(index + 1) + L". " + candidates_[index];
            TextOutW(dc, kCandidateLeft + 10, y + 5, line.c_str(),
                     static_cast<int>(line.size()));
            y += kCandidateRowStride;
        }
    }

    SelectObject(dc, old_font);
    DeleteObject(font);
}

LRESULT CALLBACK ProbeApp::window_proc(HWND hwnd,
                                       UINT message,
                                       WPARAM wparam,
                                       LPARAM lparam) {
    ProbeApp* app = reinterpret_cast<ProbeApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<ProbeApp*>(create->lpCreateParams);
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->handle_message(message, wparam, lparam)
               : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT ProbeApp::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wparam) == kGateCheckboxId) {
            gate_on_signal_ = SendMessageW(gate_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            cxxime::write_host_trace("probe", "probe.signal_gate", {
                {"composition_id", composition_id_},
                {"enabled", gate_on_signal_},
                {"result", "changed"},
            });
            SetFocus(hwnd_);
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        if (LOWORD(wparam) == kOriginalUiCheckboxId) {
            const bool enabled =
                SendMessageW(original_ui_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            set_candidate_ui_visibility_cycle(enabled, "checkbox");
            SetFocus(hwnd_);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == kCandidateUiVisibilityTimerId) {
            advance_candidate_ui_visibility_cycle();
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wparam == kConversionHotKeyId) {
            toggle_conversion_compartment();
            return 0;
        }
        break;
    case WM_SETFOCUS:
        CreateCaret(hwnd_, nullptr, 2, 24);
        SetCaretPos(24, 52);
        ShowCaret(hwnd_);
        return 0;
    case WM_KILLFOCUS:
        DestroyCaret();
        return 0;
    case WM_LBUTTONDOWN: {
        const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        UINT index = 0;
        if (candidate_index_at_point(point, &index)) {
            click_candidate(index);
            return 0;
        }
        break;
    }
    case WM_IME_SETCONTEXT:
        trace_ime_message(message, wparam, lparam, "set_context_suppress_default_ui");
        return DefWindowProcW(hwnd_, message, wparam, 0);
    case WM_IME_STARTCOMPOSITION:
        ensure_composition_id();
        composition_active_ = true;
        trace_ime_message(message, wparam, lparam, "start");
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    case WM_IME_COMPOSITION:
        trace_ime_message(message, wparam, lparam, "update");
        read_composition(lparam);
        return 0;
    case WM_IME_ENDCOMPOSITION:
        trace_ime_message(message, wparam, lparam, "end");
        finish_candidate_click("ended_without_result", 0);
        composition_active_ = false;
        composition_.clear();
        InvalidateRect(hwnd_, nullptr, TRUE);
        if (candidate_element_id_ == TF_INVALID_UIELEMENTID &&
            reading_element_id_ == TF_INVALID_UIELEMENTID) {
            composition_id_ = 0;
        }
        return 0;
    case WM_IME_NOTIFY: {
        trace_ime_message(message, wparam, lparam, "notify");
        const bool candidate_notify =
            wparam == IMN_OPENCANDIDATE || wparam == IMN_CHANGECANDIDATE ||
            wparam == IMN_CLOSECANDIDATE;
        if (candidate_notify) {
            trace_imm_candidate_snapshot("ime_notify", candidate_element_id_, "before");
        }
        const LRESULT result = DefWindowProcW(hwnd_, message, wparam, lparam);
        if (candidate_notify) {
            trace_imm_candidate_snapshot("ime_notify", candidate_element_id_, "after");
        }
        return result;
    }
    case WM_INPUTLANGCHANGE:
        cxxime::write_host_trace("probe", "probe.input_language", {
            {"hkl", reinterpret_cast<uintptr_t>(reinterpret_cast<HKL>(lparam))},
            {"result", "changed"},
        });
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint_struct = {};
        HDC dc = BeginPaint(hwnd_, &paint_struct);
        paint(dc);
        EndPaint(hwnd_, &paint_struct);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd_, kCandidateUiVisibilityTimerId);
        if (himc_) {
            ImmReleaseContext(hwnd_, himc_);
            himc_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

} // namespace cxxime_probe
