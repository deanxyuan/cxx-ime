// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_RENDERER_H_
#define CXXIME_RENDERER_H_

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cxxime/candidate.h>

namespace cxxime {

class D2DRenderer {
public:
    bool initialize(HWND hwnd);
    void finalize();
    void render(const CandidatePage& page);
    void resize(int width, int height);

private:
    ID2D1Factory* d2d_factory_ = nullptr;
    ID2D1HwndRenderTarget* render_target_ = nullptr;
    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* bg_brush_ = nullptr;
    ID2D1SolidColorBrush* highlight_brush_ = nullptr;
    IDWriteFactory* dwrite_factory_ = nullptr;
    IDWriteTextFormat* text_format_ = nullptr;
};

} // namespace cxxime

#endif // CXXIME_RENDERER_H_
