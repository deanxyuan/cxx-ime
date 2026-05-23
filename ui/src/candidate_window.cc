// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cxxime/candidate_window.h>

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

void CandidateWindow::update(const CandidatePage& page) {
    if (!hwnd_)
        return;

    page_ = page;

    // Calculate window size based on candidates
    int width = 300;
    int row_height = 24;
    int rows = (int)page.candidates.size();
    if (rows < 1) rows = 1;
    int height = rows * row_height + 8;

    if (!page.candidates.empty()) {
        width = 200;
        for (const auto& c : page.candidates) {
            int w = (int)c.text.size() * 16 + 60;
            if (w > width) width = w;
        }
        if (width > 500) width = 500;
    }

    SetWindowPos(hwnd_, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::set_position(int x, int y) {
    if (hwnd_)
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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

            for (int i = 0; i < (int)self->page_.candidates.size(); ++i) {
                RECT row_rc = {4, y, rc.right - 4, y + row_height};

                // Highlight selected candidate
                if (i == self->page_.highlighted) {
                    FillRect(hdc, &row_rc, GetSysColorBrush(COLOR_HIGHLIGHT));
                    SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                }

                // Draw "N. candidate_text"
                std::wstring text = std::to_wstring(i + 1) + L". ";
                int len = MultiByteToWideChar(CP_UTF8, 0, self->page_.candidates[i].text.c_str(),
                                              -1, nullptr, 0);
                if (len > 0) {
                    std::wstring wtext(len - 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, self->page_.candidates[i].text.c_str(),
                                        -1, &wtext[0], len);
                    text += wtext;
                }

                DrawTextW(hdc, text.c_str(), -1, &row_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                y += row_height;
            }

            if (hOldFont) SelectObject(hdc, hOldFont);
        } else {
            DrawTextW(hdc, L"CxxIME", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace cxxime
