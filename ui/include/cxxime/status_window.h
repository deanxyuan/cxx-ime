// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_STATUS_WINDOW_H_
#define CXXIME_STATUS_WINDOW_H_

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include <cxxime/render_context.h>

namespace cxxime {

enum class StatusButton {
    CHINESE_MODE,   // 中/EN
    FULL_SHAPE,     // 全/半
    CHINESE_PUNCT,  // 。/.
    SETTINGS,       // ⚙
};

struct ButtonState {
    bool chinese_mode = true;
    bool full_shape = false;
    bool chinese_punct = true;
};

using StatusButtonClickCallback = std::function<void(StatusButton)>;
using StatusPositionChangeCallback = std::function<void(int x, int y)>;
using StatusConfigActionCallback = std::function<void(const std::string& action)>;

struct ButtonDrawInfo;

class StatusWindow {
public:
    StatusWindow();
    ~StatusWindow();

    StatusWindow(const StatusWindow&) = delete;
    StatusWindow& operator=(const StatusWindow&) = delete;

    // Called from DllMain(DLL_PROCESS_DETACH) — destroy all lingering windows.
    static void cleanup_all();

    bool create(HWND parent, const StatusTheme& theme);
    void destroy();
    bool is_created() const;

    void show();
    void hide();
    bool is_visible() const;

    void set_enabled(bool enabled);

    void update_state(const ButtonState& state);
    void set_position(int x, int y);
    void get_position(int& x, int& y) const;
    void set_click_callback(StatusButtonClickCallback callback);
    void set_position_callback(StatusPositionChangeCallback callback);
    void set_config_action_callback(StatusConfigActionCallback callback);

    // Logo icon (loaded from DLL resources by StatusController)
    void set_logo_icon(HICON icon);

    // Test accessor
    HWND hwnd_for_test() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void RedrawLayered();
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseLeave();
    void OnRButtonUp(int x, int y);

    // Rendering: D2D primary, GDI+ fallback — both render to layered_dc_
    void InitLayeredSurface();
    void CleanupLayeredSurface();
    void InitD2D();
    void CleanupD2D();
    void ComputeButtonDrawInfo(std::vector<ButtonDrawInfo>& out);
    void PaintD2D();
    void PaintGdiplus();

    // Coordinate helpers
    RECT GetLogoRect() const;
    RECT GetSeparatorRect() const;
    RECT GetPillButtonRect(int index) const;
    int HitTest(int x, int y) const;

    void CreateFonts();
    void InitTooltip();
    void ShowContextMenu(int x, int y);

    // Drag: DPI-aware threshold to distinguish click from drag
    int drag_threshold() const { return Scaled(6); }
    void BeginTracking(int x, int y);
    void ContinueTracking(int x, int y);
    void EndTracking();

    // Layout constants (base values; actual rendering multiplied by dpi_scale_)
    static constexpr int BUTTON_COUNT = 4;            // Interactive buttons (excludes logo)
    static constexpr int BASE_BUTTON_WIDTH = 28;      // Function button width
    static constexpr int BASE_SETTINGS_WIDTH = 24;    // Settings button width
    static constexpr int BASE_BUTTON_HEIGHT = 22;     // Button height
    static constexpr int BASE_BUTTON_GAP = 4;         // Gap between buttons
    static constexpr int BASE_SEPARATOR_GAP = 8;      // Gap on each side of separator
    static constexpr int BASE_SEPARATOR_WIDTH = 1;    // Separator line width
    static constexpr int BASE_WINDOW_PADDING = 6;     // Inner padding
    static constexpr int BASE_LOGO_WIDTH = 28;        // Logo placeholder width

    // DPI scaling
    float dpi_scale_ = 1.0f;
    int Scaled(int base) const { return static_cast<int>(base * dpi_scale_ + 0.5f); }
    int WindowWidth() const {
        // Use GetPillButtonRect accumulation to match actual button positions
        RECT last = GetPillButtonRect(BUTTON_COUNT - 1);
        return last.right + Scaled(BASE_WINDOW_PADDING);
    }
    int WindowHeight() const {
        return Scaled(BASE_BUTTON_HEIGHT + 2 * BASE_WINDOW_PADDING);
    }

    // ── Window ───────────────────────────────────────────────
    HWND hwnd_ = nullptr;
    HWND tooltip_hwnd_ = nullptr;
    int win_w_ = 0, win_h_ = 0;

    // ── Layered window: offscreen 32-bit ARGB surface ────────
    HBITMAP layered_bmp_ = nullptr;
    HDC layered_dc_ = nullptr;
    void* layered_bits_ = nullptr;

    // ── D2D (primary, renders to layered DC) ──────────────────
    ID2D1Factory* d2d_factory_ = nullptr;
    ID2D1DCRenderTarget* d2d_rt_ = nullptr;
    IDWriteFactory* dwrite_factory_ = nullptr;
    IDWriteTextFormat* d2d_font_cn_ = nullptr;
    IDWriteTextFormat* d2d_font_en_ = nullptr;
    IDWriteTextFormat* d2d_font_icon_ = nullptr;
    bool use_d2d_ = false;

    // ── GDI+ fallback fonts ───────────────────────────────────
    HFONT font_cn_ = nullptr;
    HFONT font_en_ = nullptr;
    HFONT font_icon_ = nullptr;

    // ── State ─────────────────────────────────────────────────
    StatusTheme theme_;
    ButtonState state_;
    int hovered_button_ = -1;
    bool is_enabled_ = true;
    bool layered_ready_ = false;
    HICON logo_icon_ = nullptr;  // drawn in logo pill

    // ── Drag state ────────────────────────────────────────────
    bool is_tracking_ = false;
    bool is_dragging_ = false;
    POINT track_start_ = {};
    POINT window_start_ = {};

    StatusButtonClickCallback click_callback_;
    StatusPositionChangeCallback position_callback_;
    StatusConfigActionCallback config_action_callback_;
};

} // namespace cxxime

#endif // CXXIME_STATUS_WINDOW_H_
