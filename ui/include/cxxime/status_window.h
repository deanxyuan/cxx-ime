// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_STATUS_WINDOW_H_
#define CXXIME_STATUS_WINDOW_H_

#include <windows.h>
#include <string>
#include <functional>
#include <cstdint>

namespace cxxime {

enum class StatusButton {
    CHINESE_MODE,   // 中/英
    FULL_SHAPE,     // 全/半
    CHINESE_PUNCT,  // 。/.
    INPUT_MODE,     // 拼/五
    SETTINGS,       // 配置
};

struct ButtonState {
    bool chinese_mode = true;
    bool full_shape = false;
    bool chinese_punct = true;
    bool is_pinyin = true;
};

using StatusButtonClickCallback = std::function<void(StatusButton)>;
using StatusPositionChangeCallback = std::function<void(int x, int y)>;
using StatusConfigActionCallback = std::function<void(const std::string& action)>;

class StatusWindow {
public:
    StatusWindow();
    ~StatusWindow();

    StatusWindow(const StatusWindow&) = delete;
    StatusWindow& operator=(const StatusWindow&) = delete;

    bool create(HWND parent);
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

    // Test accessor
    HWND hwnd_for_test() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseLeave();
    void OnRButtonUp(int x, int y);

    void DrawButton(HDC hdc, int index, const RECT& rect, bool pressed);
    RECT GetButtonRect(int index) const;
    int HitTest(int x, int y) const;
    HFONT CreateButtonFont(int size);

    void InitTooltip();
    void ShowContextMenu(int x, int y);

    // 拖动：移动阈值区分点击和拖动
    static constexpr int DRAG_THRESHOLD = 4;
    void BeginTracking(int x, int y);
    void ContinueTracking(int x, int y);
    void EndTracking();

    // 成员
    HWND hwnd_ = nullptr;
    HWND owner_hwnd_ = nullptr;   // hidden owner — process exit auto-destroys status window
    HWND tooltip_hwnd_ = nullptr;
    HFONT font_ = nullptr;
    ButtonState state_;
    int hovered_button_ = -1;
    bool is_enabled_ = true;

    // 拖动状态
    bool is_tracking_ = false;
    bool is_dragging_ = false;
    POINT track_start_ = {};
    POINT window_start_ = {};

    StatusButtonClickCallback click_callback_;
    StatusPositionChangeCallback position_callback_;
    StatusConfigActionCallback config_action_callback_;

    // 常量（基准值，实际渲染乘以 dpi_scale_）
    static constexpr int BUTTON_COUNT = 5;
    static constexpr int BASE_BUTTON_WIDTH = 32;
    static constexpr int BASE_BUTTON_HEIGHT = 24;
    static constexpr int BASE_BUTTON_SPACING = 2;
    static constexpr int BASE_WINDOW_PADDING = 4;

    // DPI 缩放
    float dpi_scale_ = 1.0f;
    int Scaled(int base) const { return static_cast<int>(base * dpi_scale_ + 0.5f); }
    int WindowWidth() const {
        return Scaled(BUTTON_COUNT * BASE_BUTTON_WIDTH +
                      (BUTTON_COUNT - 1) * BASE_BUTTON_SPACING +
                      2 * BASE_WINDOW_PADDING);
    }
    int WindowHeight() const {
        return Scaled(BASE_BUTTON_HEIGHT + 2 * BASE_WINDOW_PADDING);
    }
};

} // namespace cxxime

#endif // CXXIME_STATUS_WINDOW_H_
