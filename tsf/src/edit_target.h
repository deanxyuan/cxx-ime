// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_EDIT_TARGET_H_
#define CXXIME_TSF_EDIT_TARGET_H_

#include <cstdint>

#include <windows.h>
#include <msctf.h>

namespace cxxime_tsf {

struct TextExtRectTrace {
    RECT raw = {};
    RECT result = {};
    HWND view_hwnd = nullptr;
    HWND foreground_hwnd = nullptr;
    HWND caret_hwnd = nullptr;
    HWND focus_hwnd = nullptr;
    POINT caret_pos = {};
    HRESULT text_ext_hr = E_UNEXPECTED;
    const char* branch = "text_ext_failed";
    bool clipped = false;
    bool resolved = false;
    bool gui_info_ok = false;
    bool caret_pos_queried = false;
    bool caret_pos_ok = false;
    bool caret_map_ok = false;
};

enum class CaretSource : uint8_t {
    None = 0,
    Anchor,
    Projected,
    Boundary,
    TextExtent,
    Native,
    WindowFallback,
};

struct CaretPosition {
    RECT rect = {};
    RECT view_rect = {};
    CaretSource source = CaretSource::None;
    bool clipped = false;

    bool valid() const { return source != CaretSource::None; }
    bool real() const { return source == CaretSource::Native || source == CaretSource::TextExtent; }
    bool observed() const {
        return real() || source == CaretSource::Projected || source == CaretSource::Boundary;
    }
    bool has_view() const {
        return view_rect.right > view_rect.left && view_rect.bottom > view_rect.top;
    }
};

struct CaretRequest {
    std::uint64_t presentation_generation = 0;
    std::uint64_t target_generation = 0;
};

class CaretViewportTracker {
public:
    bool remember(std::uint64_t target_generation,
                  const RECT& view_rect,
                  const RECT& caret_rect);
    CaretSource resolve(std::uint64_t target_generation,
                        const RECT* view_rect,
                        const RECT* logical_rect,
                        bool clipped,
                        RECT* caret_rect) const;

private:
    std::uint64_t target_generation_ = 0;
    RECT view_rect_ = {};
    RECT caret_rect_ = {};
    bool valid_ = false;
};

bool text_rect_is_outside_view(HRESULT screen_rect_hr, const RECT& screen_rect,
    HRESULT text_rect_hr, const RECT& text_rect, bool text_clipped);
bool text_rect_is_placeholder(const RECT& view_rect, const RECT& text_rect);
bool text_rect_requires_composition_refresh(const RECT& view_rect, const RECT& text_rect);
bool map_fallback_caret_rect(HWND caret_window, POINT caret, RECT* rect);
bool map_current_thread_caret_rect(HWND foreground, RECT* rect,
                                   TextExtRectTrace* trace = nullptr);
bool resolve_native_caret_rect(HWND foreground, RECT* rect);
bool normalize_text_ext_rect(HWND view_hwnd, HWND foreground, RECT* rect,
                             TextExtRectTrace* trace = nullptr);

} // namespace cxxime_tsf

#endif // CXXIME_TSF_EDIT_TARGET_H_
