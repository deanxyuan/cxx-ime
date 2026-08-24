// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_WINDOW_H_
#define CXXIME_CANDIDATE_WINDOW_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/candidate.h>
#include <cxxime/config.h>
#include <cxxime/layout.h>
#include <cxxime/render_context.h>

namespace cxxime {

struct Config;
enum class RenderBackend { GDI, D2D };
enum class CandidatePageDirection { Previous, Next };

class CandidateWindow {
public:
    using CandidateSelectionCallback = std::function<void(std::size_t)>;
    using LayoutChangedCallback = std::function<void()>;
    using PageCallback = std::function<void(CandidatePageDirection)>;

    ~CandidateWindow();

    bool create(HWND owner, const Config& config);
    bool ensure_created(HWND owner);
    void destroy();
    bool is_created() const;
    void show();
    void hide();
    bool is_visible() const;
    void set_config(const Config& config);
    void update(const CandidatePage& page);
    void set_preedit(const std::string& preedit);
    void set_preedit(const std::string& preedit, size_t cursor);
    void set_layout(const std::string& layout);
    void move_to_caret(const RECT& caretRect);
    void move_to_screen_position(int x, int y);
    void set_candidate_selection_callback(CandidateSelectionCallback cb);
    void set_layout_changed_callback(LayoutChangedCallback cb);
    void set_page_callback(PageCallback cb);
    void set_draggable(bool draggable);
    void set_theme(const Theme& theme);
    void set_render_backend(RenderBackend backend);
    void set_page_info(int current, int total);
    void set_owner(HWND owner);
    bool owner_matches(HWND owner) const;
    int visible_candidate_count() const;
    SIZE window_size() const;
    SIZE layout_size() const;
    UINT dpi() const;
    bool get_window_rect(RECT* rect) const;
    HWND hwnd_for_test() const { return hwnd_; }
    RECT page_button_rect_for_test(CandidatePageDirection direction) const {
        return direction == CandidatePageDirection::Previous ? render_ctx_.prev_button_rect
                                                             : render_ctx_.next_button_rect;
    }
    RECT candidate_rect_for_test(std::size_t index) const {
        return index < candidate_rects_.size() ? candidate_rects_[index].highlight_rect
                                               : RECT{};
    }

private:
    void rebuild_render_context(const LayoutConfig& cfg, int window_width);
    bool refresh_dpi_scale();
    bool refresh_preedit_cursor_width();
    void recreate_renderers_for_dpi();
    void init_gdi_renderer();
    void init_d2d_renderer();
    void move_window_now(int x, int y);
    bool calculate_target_position(const RECT& caret_rect, int width, int height, POINT& target) const;
    void update_window_region(int width, int height, int corner);
    int monitor_work_width() const;

    HWND hwnd_ = nullptr;
    float dpi_scale_ = 1.0f;
    LayoutConfig scaled_cfg_;
    CandidatePage page_;
    std::string preedit_text_;
    size_t preedit_cursor_ = 0;
    int preedit_cursor_width_ = 1;
    std::string layout_orientation_ = "horizontal";
    CandidateSelectionCallback candidate_selection_cb_;
    LayoutChangedCallback layout_changed_cb_;
    PageCallback page_cb_;
    bool draggable_ = false;
    std::vector<CandidateRect> candidate_rects_;
    RenderContext render_ctx_;
    Theme theme_;
    RenderBackend backend_ = RenderBackend::GDI;
    const Config* config_ = nullptr;

    class GdiRenderer;  GdiRenderer* gdi_renderer_ = nullptr;
    class D2DRenderer; D2DRenderer* d2d_renderer_ = nullptr;

    int page_current_ = 1, page_total_ = 1;
    int visible_candidate_count_ = 0;
    int window_width_ = 0, window_height_ = 0, window_corner_ = -1;
    bool has_last_caret_rect_ = false;
    RECT last_caret_rect_{};

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

} // namespace cxxime
#endif
