// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_CANDIDATE_WINDOW_H_
#define CXXIME_CANDIDATE_WINDOW_H_

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cxxime/candidate.h>

namespace cxxime {

class CandidateWindow {
public:
    using ClickCallback = std::function<void(int)>;

    bool create(HWND parent);
    void destroy();
    void show();
    void hide();
    void update(const CandidatePage& page);
    void set_position(int x, int y);
    void set_click_callback(ClickCallback cb);

private:
    HWND hwnd_ = nullptr;
    CandidatePage page_;
    ClickCallback click_cb_ = nullptr;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

} // namespace cxxime

#endif // CXXIME_CANDIDATE_WINDOW_H_
