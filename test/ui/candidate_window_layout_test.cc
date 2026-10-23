// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstddef>

#include <windows.h>

#include <cxxime/candidate_window.h>
#include <cxxime/config.h>

#include "support/dpi_testutil.h"
#include "support/testutil.h"

namespace {

void assert_full_width_rows(cxxime::CandidateWindow& window, const cxxime::Config& config,
                            std::size_t count) {
    const float scale = static_cast<float>(window.dpi()) / USER_DEFAULT_SCREEN_DPI;
    const int margin = static_cast<int>(config.layout_config.margin_x * scale);
    const int padding = static_cast<int>(config.layout_config.hilite_padding_x * scale);
    const int border = static_cast<int>(config.layout_config.border_width * scale);
    const int right = window.layout_size().cx - border * 2 - margin;
    for (std::size_t index = 0; index < count; ++index) {
        const RECT row = window.candidate_rect_for_test(index);
        ASSERT_EQ(row.left, margin - padding);
        ASSERT_EQ(row.right, right);
        ASSERT_GT(row.bottom, row.top);
        std::size_t selected = count;
        window.set_candidate_selection_callback([&](std::size_t value) { selected = value; });
        SendMessageW(window.hwnd_for_test(), WM_LBUTTONDOWN, 0,
                     MAKELPARAM(right - 1, (row.top + row.bottom) / 2));
        ASSERT_EQ(selected, index);
        window.set_candidate_selection_callback({});
    }
}

} // namespace

TEST(CandidateWindowLayout, vertical_rows_fill_and_select_across_final_width_changes) {
    test::ScopedDpiAwarenessContext dpi_awareness;
    for (const char* backend : {"gdi", "d2d"}) {
        cxxime::Config config;
        config.render_backend = backend;
        config.layout = "vertical";
        cxxime::CandidatePage page;
        page.candidates.push_back(cxxime::Candidate{u8"在", ""});
        page.candidates.push_back(cxxime::Candidate{u8"你", ""});

        cxxime::CandidateWindow window;
        ASSERT_TRUE(window.create(nullptr, config));
        window.set_preedit("d");
        window.update(page);
        const int short_width = window.layout_size().cx;
        assert_full_width_rows(window, config, page.candidates.size());

        page.candidates[0].text = u8"们";
        window.set_preedit("wu");
        window.update(page);
        ASSERT_EQ(window.layout_size().cx, short_width);
        assert_full_width_rows(window, config, page.candidates.size());

        window.set_preedit("wo'men'zheng'zai'shu'ru'zhong'wen");
        window.update(page);
        ASSERT_GT(window.layout_size().cx, short_width);
        assert_full_width_rows(window, config, page.candidates.size());

        window.set_preedit("");
        window.update(page);
        ASSERT_EQ(window.layout_size().cx, short_width);
        assert_full_width_rows(window, config, page.candidates.size());

        config.layout = "horizontal";
        window.set_config(config);
        window.update(page);
        const RECT first = window.candidate_rect_for_test(0);
        const RECT second = window.candidate_rect_for_test(1);
        ASSERT_LE(first.right, second.left);

        config.layout = "vertical";
        window.set_config(config);
        window.update(page);
        assert_full_width_rows(window, config, page.candidates.size());

        // Recreating a native window must use the same configuration.
        ASSERT_TRUE(DestroyWindow(window.hwnd_for_test()) != FALSE);
        ASSERT_TRUE(window.ensure_created(nullptr));
        window.update(page);
        assert_full_width_rows(window, config, page.candidates.size());
        window.destroy();
    }
}

TEST(CandidateWindowLayout, vertical_rows_include_width_reserved_for_page_buttons) {
    test::ScopedDpiAwarenessContext dpi_awareness;
    cxxime::Config config;
    config.render_backend = "gdi";
    config.layout = "vertical";
    config.font_size = 6;
    config.layout_config.min_width = 0;

    cxxime::CandidatePage page;
    page.candidates.push_back(cxxime::Candidate{"i", ""});
    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page);
    const int unpaged_width = window.layout_size().cx;

    window.set_page_info(1, 2);
    window.update(page);
    ASSERT_GT(window.layout_size().cx, unpaged_width);
    assert_full_width_rows(window, config, page.candidates.size());
    const RECT row = window.candidate_rect_for_test(0);
    const RECT next = window.page_button_rect_for_test(cxxime::CandidatePageDirection::Next);
    ASSERT_GE(next.top, row.top);
    ASSERT_GE(next.bottom, row.bottom);

    window.set_page_info(1, 1);
    window.update(page);
    ASSERT_EQ(window.layout_size().cx, unpaged_width);
    assert_full_width_rows(window, config, page.candidates.size());
    window.destroy();
}
