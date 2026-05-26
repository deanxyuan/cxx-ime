// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_WINDOW_H_
#define CXXIME_CANDIDATE_WINDOW_H_

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cxxime/candidate.h>
#include <cxxime/layout.h>
#include <cxxime/render_context.h>
#include <cxxime/config.h>

namespace cxxime {

struct Config;
enum class RenderBackend { GDI, D2D };

class CandidateWindow {
public:
    using ClickCallback = std::function<void(int)>;

    bool create(HWND parent, const Config& config);
    void destroy();
    void show();
    void hide();
    void update(const CandidatePage& page);
    void set_preedit(const std::string& preedit);
    void set_layout(const std::string& layout);
    void move_to_caret(const RECT& caretRect);
    void set_click_callback(ClickCallback cb);
    void set_theme(const Theme& theme);
    void set_render_backend(RenderBackend backend);
    void set_page_info(int current, int total);

private:
    void rebuild_render_context(const LayoutConfig& cfg);
    void init_gdi_renderer();
    void init_d2d_renderer();

    HWND hwnd_ = nullptr;
    HRGN hrgn_ = nullptr;
    float dpi_scale_ = 1.0f;
    LayoutConfig scaled_cfg_;
    CandidatePage page_;
    std::string preedit_text_;
    std::string layout_orientation_ = "horizontal";
    ClickCallback click_cb_;
    std::vector<CandidateRect> candidate_rects_;
    RenderContext render_ctx_;
    Theme theme_;
    RenderBackend backend_ = RenderBackend::GDI;
    const Config* config_ = nullptr;

    class GdiRenderer;  GdiRenderer* gdi_renderer_ = nullptr;
    class D2DRenderer; D2DRenderer* d2d_renderer_ = nullptr;

    int page_current_ = 1, page_total_ = 1;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

} // namespace cxxime
#endif
