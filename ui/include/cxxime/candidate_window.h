// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_WINDOW_H_
#define CXXIME_CANDIDATE_WINDOW_H_

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cxxime/candidate.h>
#include <cxxime/layout.h>

namespace cxxime {

class CandidateWindow {
public:
    using ClickCallback = std::function<void(int)>;

    bool create(HWND parent);
    void destroy();
    void show();
    void hide();
    void update(const CandidatePage& page);
    void set_preedit(const std::string& preedit);
    void set_layout(const std::string& layout);
    void set_position(int x, int y);
    void set_click_callback(ClickCallback cb);

private:
    HWND hwnd_ = nullptr;
    CandidatePage page_;
    std::string preedit_text_;
    std::string layout_ = "horizontal";
    ClickCallback click_cb_ = nullptr;
    std::vector<CandidateRect> candidate_rects_;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

} // namespace cxxime

#endif // CXXIME_CANDIDATE_WINDOW_H_
