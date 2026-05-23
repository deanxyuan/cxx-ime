// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/renderer.h>

namespace cxxime {

bool D2DRenderer::initialize(HWND hwnd) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    if (FAILED(hr))
        return false;

    RECT rc;
    GetClientRect(hwnd, &rc);

    hr = d2d_factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
        &render_target_);
    if (FAILED(hr))
        return false;

    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &text_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &bg_brush_);
    render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DodgerBlue), &highlight_brush_);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&dwrite_factory_));
    if (FAILED(hr))
        return false;

    hr = dwrite_factory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                            14.0f, L"zh-cn", &text_format_);
    if (FAILED(hr))
        return false;

    return true;
}

void D2DRenderer::finalize() {
    if (text_format_) {
        text_format_->Release();
        text_format_ = nullptr;
    }
    if (dwrite_factory_) {
        dwrite_factory_->Release();
        dwrite_factory_ = nullptr;
    }
    if (highlight_brush_) {
        highlight_brush_->Release();
        highlight_brush_ = nullptr;
    }
    if (bg_brush_) {
        bg_brush_->Release();
        bg_brush_ = nullptr;
    }
    if (text_brush_) {
        text_brush_->Release();
        text_brush_ = nullptr;
    }
    if (render_target_) {
        render_target_->Release();
        render_target_ = nullptr;
    }
    if (d2d_factory_) {
        d2d_factory_->Release();
        d2d_factory_ = nullptr;
    }
}

void D2DRenderer::render(const CandidatePage& page) {
    if (!render_target_)
        return;

    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(D2D1::ColorF::White));

    D2D1_SIZE_F size = render_target_->GetSize();

    // Draw candidates
    float x = 10.0f;
    for (size_t i = 0; i < page.candidates.size(); ++i) {
        std::wstring text = std::to_wstring(i + 1) + L". ";
        int len = MultiByteToWideChar(CP_UTF8, 0, page.candidates[i].text.c_str(), -1, nullptr, 0);
        if (len > 0) {
            std::wstring wtext(len - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, page.candidates[i].text.c_str(), -1, &wtext[0], len);
            text += wtext;
        }

        D2D1_RECT_F rect = D2D1::RectF(x, 5.0f, x + 80.0f, size.height - 5.0f);

        if ((int)i == page.highlighted) {
            render_target_->FillRectangle(rect, highlight_brush_);
        }

        render_target_->DrawText(text.c_str(), (UINT32)text.length(), text_format_, rect, text_brush_);
        x += 80.0f;
    }

    render_target_->EndDraw();
}

void D2DRenderer::resize(int width, int height) {
    if (render_target_) {
        render_target_->Resize(D2D1::SizeU(width, height));
    }
}

} // namespace cxxime
