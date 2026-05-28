// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/renderer.h>
#include <cxxime/config.h>
#include <string>

namespace cxxime {

static D2D1_COLOR_F c2d(const Color& c) { return D2D1::ColorF(c.r/255.0f, c.g/255.0f, c.b/255.0f, c.a/255.0f); }

static std::wstring dec(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

static IDWriteTextFormat* mkfmt(IDWriteFactory* f, const wchar_t* name, float sz, DWRITE_TEXT_ALIGNMENT ha) {
    IDWriteTextFormat* fmt = nullptr;
    f->CreateTextFormat(name, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                        sz, L"zh-cn", &fmt);
    if (fmt) { fmt->SetTextAlignment(ha); fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
    return fmt;
}

bool D2DRenderer::initialize(HWND hwnd, int font_size, const wchar_t* font_name) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    if (FAILED(hr)) return false;
    RECT rc; GetClientRect(hwnd, &rc);
    hr = d2d_factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right-rc.left, rc.bottom-rc.top)), &render_target_);
    if (FAILED(hr)) return false;
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &text_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &bg_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DodgerBlue), &highlight_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &highlight_text_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(0.68f, 0.85f, 1.0f, 0.5f), &hover_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &preedit_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &label_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &nav_brush_);
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwrite_factory_));
    if (FAILED(hr)) return false;
    HDC dc = GetDC(hwnd);
    float dpi = (float)GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(hwnd, dc);
    float fsize = (float)font_size * dpi / 72.0f;  // pt→px, match GDI's MulDiv(font_size, dpi, 72)
    float psize = font_size > 2 ? (float)(font_size - 2) * dpi / 72.0f : fsize;  // preedit: font_size-2 pt
    fmt_left_   = mkfmt(dwrite_factory_, font_name, fsize, DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt_right_  = mkfmt(dwrite_factory_, font_name, fsize, DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt_preedit_ = mkfmt(dwrite_factory_, font_name, psize, DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt_small_  = mkfmt(dwrite_factory_, font_name, 9.0f * dpi / 72.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
    return fmt_left_ && fmt_right_ && fmt_preedit_ && fmt_small_;
}

void D2DRenderer::finalize() {
    for (auto* p : {&fmt_small_, &fmt_preedit_, &fmt_right_, &fmt_left_}) { if (*p) { (*p)->Release(); *p = nullptr; } }
    if (dwrite_factory_) { dwrite_factory_->Release(); dwrite_factory_ = nullptr; }
    for (auto* p : {&nav_brush_, &label_brush_, &preedit_brush_, &hover_brush_,
                    &highlight_text_brush_, &highlight_brush_, &bg_brush_, &text_brush_})
    { if (*p) { (*p)->Release(); *p = nullptr; } }
    if (render_target_) { render_target_->Release(); render_target_ = nullptr; }
    if (d2d_factory_) { d2d_factory_->Release(); d2d_factory_ = nullptr; }
}

void D2DRenderer::render(const RenderContext& ctx) {
    if (!render_target_) return;
    render_target_->BeginDraw();
    auto* cfg = ctx.layout_cfg;
    float corner = cfg ? (float)cfg->round_corner : 4.0f;
    float margin = cfg ? (float)cfg->margin_x : 12.0f;

    if (ctx.theme) {
        bg_brush_->SetColor(c2d(ctx.theme->background));
        text_brush_->SetColor(c2d(ctx.theme->text));
        highlight_brush_->SetColor(c2d(ctx.theme->hilited_back));
        highlight_text_brush_->SetColor(c2d(ctx.theme->hilited_text));
        preedit_brush_->SetColor(c2d(ctx.theme->preedit_text));
        label_brush_->SetColor(c2d(ctx.theme->label_text));
        nav_brush_->SetColor(c2d(ctx.theme->prev_page));
        D2D1::ColorF hover_col((ctx.theme->background.r + ctx.theme->hilited_back.r) / 2.0f / 255.0f,
                                (ctx.theme->background.g + ctx.theme->hilited_back.g) / 2.0f / 255.0f,
                                (ctx.theme->background.b + ctx.theme->hilited_back.b) / 2.0f / 255.0f, 1.0f);
        hover_brush_->SetColor(hover_col);
    }

    D2D1_SIZE_F sz = render_target_->GetSize();
    render_target_->FillRectangle(D2D1::RectF(0,0,sz.width,sz.height), bg_brush_);

    if (!ctx.rects || ctx.rects->empty()) {
        // No candidates — show preedit if available, otherwise placeholder
        if (!ctx.preedit.empty() && ctx.preedit_rect.right > ctx.preedit_rect.left) {
            auto wp = dec(ctx.preedit);
            if (!wp.empty()) {
                D2D1_RECT_F pr = {(float)ctx.preedit_rect.left, (float)ctx.preedit_rect.top,
                                  (float)ctx.preedit_rect.right, (float)ctx.preedit_rect.bottom};
                render_target_->DrawText(wp.c_str(), (UINT32)wp.length(), fmt_preedit_, pr, preedit_brush_);
            }
        } else {
            render_target_->DrawText(L"CxxIME", 6, fmt_left_, D2D1::RectF(0,0,sz.width,sz.height), preedit_brush_);
        }
        render_target_->EndDraw(); return;
    }

    // Preedit
    if (!ctx.preedit.empty() && ctx.preedit_rect.right > ctx.preedit_rect.left) {
        auto wp = dec(ctx.preedit);
        if (!wp.empty()) {
            D2D1_RECT_F pr = {(float)ctx.preedit_rect.left, (float)ctx.preedit_rect.top,
                              (float)ctx.preedit_rect.right, (float)ctx.preedit_rect.bottom};
            render_target_->DrawText(wp.c_str(), (UINT32)wp.length(), fmt_preedit_, pr, preedit_brush_);
        }
        // Thin separator
        float sep_y = (float)ctx.preedit_rect.bottom + (cfg ? (float)cfg->spacing/3 : 5.0f);
        auto mid = [](uint8_t a, uint8_t b) { return (float)((int)a + b) / 2.0f / 255.0f; };
        D2D1::ColorF sep_col(mid(ctx.theme->text.r, ctx.theme->background.r),
                             mid(ctx.theme->text.g, ctx.theme->background.g),
                             mid(ctx.theme->text.b, ctx.theme->background.b), 1.0f);
        preedit_brush_->SetColor(sep_col);
        render_target_->DrawLine({margin + 2.0f, sep_y}, {sz.width - margin - 2.0f, sep_y},
                                 preedit_brush_, 1.0f);
    }

    // Candidates
    for (const auto& cr : *ctx.rects) {
        int i = cr.index;
        bool hl = (i == ctx.highlighted), hv = (i == ctx.hovered_index);
        D2D1_RECT_F hr = {(float)cr.highlight_rect.left, (float)cr.highlight_rect.top,
                          (float)cr.highlight_rect.right, (float)cr.highlight_rect.bottom};
        if (hl || hv) {
            D2D1_ROUNDED_RECT rr = {hr, corner, corner};
            render_target_->FillRoundedRectangle(rr, hl ? highlight_brush_ : hover_brush_);
        }
        // Label
        std::wstring label = std::to_wstring(i+1) + L".";
        D2D1_RECT_F lr = {(float)cr.label_rect.left, (float)cr.label_rect.top,
                          (float)cr.label_rect.right, (float)cr.label_rect.bottom};
        render_target_->DrawText(label.c_str(), (UINT32)label.length(), fmt_right_, lr,
                                 hl ? highlight_text_brush_ : label_brush_);
        // Text
        auto wt = dec(cr.text);
        D2D1_RECT_F tr = {(float)cr.text_rect.left, (float)cr.text_rect.top,
                          (float)cr.text_rect.right, (float)cr.text_rect.bottom};
        render_target_->DrawText(wt.c_str(), (UINT32)wt.length(), fmt_left_, tr,
                                 hl ? highlight_text_brush_ : text_brush_);
    }

    // Page nav (always visible, grayed when disabled)
    if (ctx.page_total > 1 && fmt_small_) {
        float nc = corner > 2 ? corner-1 : 1;
        bool pe = (ctx.page_current > 1), ne = (ctx.page_current < ctx.page_total);
        // <
        {
            D2D1_RECT_F pr = {(float)ctx.prev_button_rect.left, (float)ctx.prev_button_rect.top,
                              (float)ctx.prev_button_rect.right, (float)ctx.prev_button_rect.bottom};
            bool h = pe && (ctx.hovered_index == -2);
            if (h) { D2D1_ROUNDED_RECT rr = {pr, nc, nc}; render_target_->FillRoundedRectangle(rr, highlight_brush_); }
            auto* b = h ? highlight_text_brush_ : (pe ? nav_brush_ : preedit_brush_);
            render_target_->DrawText(L"<", 1, fmt_small_, pr, b);
        }
        // >
        {
            D2D1_RECT_F nr = {(float)ctx.next_button_rect.left, (float)ctx.next_button_rect.top,
                              (float)ctx.next_button_rect.right, (float)ctx.next_button_rect.bottom};
            bool h = ne && (ctx.hovered_index == -3);
            if (h) { D2D1_ROUNDED_RECT rr = {nr, nc, nc}; render_target_->FillRoundedRectangle(rr, highlight_brush_); }
            auto* b = h ? highlight_text_brush_ : (ne ? nav_brush_ : preedit_brush_);
            render_target_->DrawText(L">", 1, fmt_small_, nr, b);
        }
    }

    render_target_->EndDraw();
}

void D2DRenderer::resize(int w, int h) { if (render_target_) render_target_->Resize(D2D1::SizeU(w, h)); }

} // namespace cxxime
