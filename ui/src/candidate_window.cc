// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/candidate_window.h>
#include <cxxime/layout.h>

namespace cxxime {

bool CandidateWindow::create(HWND parent) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CxxIMECandidateWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            L"CxxIMECandidateWindow", L"", WS_POPUP, 0, 0, 300, 30,
                            parent, nullptr, GetModuleHandle(nullptr), this);
    if (hwnd_)
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return hwnd_ != nullptr;
}

void CandidateWindow::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void CandidateWindow::show() {
    if (hwnd_)
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void CandidateWindow::hide() {
    if (hwnd_)
        ShowWindow(hwnd_, SW_HIDE);
}

static std::wstring decode_utf8(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

static std::wstring format_candidate(int index, const Candidate& c) {
    std::wstring text = std::to_wstring(index + 1) + L". ";
    text += decode_utf8(c.text);
    return text;
}

void CandidateWindow::update(const CandidatePage& page) {
    if (!hwnd_)
        return;

    page_ = page;
    candidate_rects_.clear();

    int row_height = 24;
    int preedit_height = preedit_text_.empty() ? 0 : row_height + 5;

    std::vector<int> text_widths;
    for (const auto& c : page.candidates)
        text_widths.push_back(estimate_text_width(c.text));

    LayoutResult lr;
    if (layout_ == "horizontal")
        lr = calculate_horizontal_layout(text_widths, row_height, 8, 600, 8);
    else
        lr = calculate_vertical_layout(text_widths, row_height, 600, 8);

    // Offset rects by preedit height
    for (auto& cr : lr.rects) {
        cr.rc.top += preedit_height;
        cr.rc.bottom += preedit_height;
    }
    lr.height += preedit_height;

    candidate_rects_ = std::move(lr.rects);

    SetWindowPos(hwnd_, nullptr, 0, 0, lr.width, lr.height, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::set_preedit(const std::string& preedit) {
    preedit_text_ = preedit;
}

void CandidateWindow::set_layout(const std::string& layout) {
    layout_ = layout;
}

void CandidateWindow::set_position(int x, int y) {
    if (hwnd_)
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CandidateWindow::set_click_callback(ClickCallback cb) {
    click_cb_ = std::move(cb);
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

        auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && !self->page_.candidates.empty()) {
            int row_height = 24;
            int y = 4;
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;
            SetBkMode(hdc, TRANSPARENT);

            if (!self->preedit_text_.empty()) {
                std::wstring wpreedit = decode_utf8(self->preedit_text_);
                if (!wpreedit.empty()) {
                    SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
                    RECT preedit_rc = {4, y, rc.right - 4, y + row_height};
                    DrawTextW(hdc, wpreedit.c_str(), -1, &preedit_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
                y += row_height;
                HPEN hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_GRAYTEXT));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                MoveToEx(hdc, 4, y + 2, nullptr);
                LineTo(hdc, rc.right - 4, y + 2);
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
            }

            for (const auto& cr : self->candidate_rects_) {
                int i = cr.index;
                if (i == self->page_.highlighted) {
                    FillRect(hdc, &cr.rc, GetSysColorBrush(COLOR_HIGHLIGHT));
                    SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                }

                std::wstring text = format_candidate(i, self->page_.candidates[i]);
                DrawTextW(hdc, text.c_str(), -1, const_cast<RECT*>(&cr.rc), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            if (hOldFont) SelectObject(hdc, hOldFont);
        } else {
            DrawTextW(hdc, L"CxxIME", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && !self->page_.candidates.empty()) {
            int x = (int)(short)LOWORD(lp);
            int y = (int)(short)HIWORD(lp);
            POINT pt = {x, y};
            for (const auto& cr : self->candidate_rects_) {
                if (PtInRect(&cr.rc, pt)) {
                    if (self->click_cb_)
                        self->click_cb_(cr.index);
                    break;
                }
            }
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace cxxime
