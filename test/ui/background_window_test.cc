// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include <windows.h>
#include <commctrl.h>
#include <psapi.h>

#include <cxxime/candidate_window.h>
#include <cxxime/renderer.h>

#include "gdi_renderer.h"
#include "support/background_test_image.h"
#include "support/dpi_testutil.h"
#include "support/window_transition_probe.h"

namespace {
using Clock = std::chrono::steady_clock;

std::shared_ptr<cxxime::BackgroundImage> image(unsigned width = 64, unsigned height = 32) {
    auto result = std::make_shared<cxxime::BackgroundImage>();
    result->width = width;
    result->height = height;
    result->pixels = background_test::solid(width, height, 0, 0, 255);
    return result;
}

cxxime::CandidatePage page() {
    cxxime::CandidatePage result;
    for (const char* word : {u8"你", u8"你好", u8"年", u8"能", u8"那", u8"南", u8"内"}) {
        result.candidates.push_back({word, ""});
    }
    result.highlighted = 0;
    return result;
}

void set_background_dimensions(cxxime::Config& config, unsigned width = 64, unsigned height = 32) {
    config.background.source = std::string(64, 'a');
    config.background.source_width = width;
    config.background.source_height = height;
    config.background.horizontal.width = config.background.vertical.width = width;
    config.background.horizontal.height = config.background.vertical.height = height;
}

struct Surface {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    explicit Surface(int width, int height) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        ASSERT_TRUE(bitmap != nullptr && dc != nullptr);
        old = SelectObject(dc, bitmap);
    }
    ~Surface() {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
};

void pump_for(unsigned milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < deadline);
}

struct PaintProbe {
    cxxime::CandidateWindow& window;
    int paints = 0;
    int plain_frames = 0;
    explicit PaintProbe(cxxime::CandidateWindow& window)
        : window(window) {
        ASSERT_TRUE(SetWindowSubclass(window.native_handle(), observe, 1,
                                      reinterpret_cast<DWORD_PTR>(this)));
    }
    ~PaintProbe() { RemoveWindowSubclass(window.native_handle(), observe, 1); }
    static LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                    DWORD_PTR data) {
        auto& probe = *reinterpret_cast<PaintProbe*>(data);
        if (message == WM_PAINT) {
            ++probe.paints;
            if (!probe.window.render_context_for_test().background_image) {
                ++probe.plain_frames;
            }
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }
};

int benchmark() {
    test::ScopedDpiAwarenessContext dpi;
    background_test::Files files;
    const auto source =
        files.write(L"benchmark.png", 3000, 2000, background_test::solid(3000, 2000, 50, 100, 150));
    std::cout << "# preparation: 3000x2000 PNG; OS file cache uncontrolled\n";
    for (int round = 0; round < 7; ++round) {
        const auto begin = Clock::now();
        auto imported = background_test::import_image(source);
        ASSERT_TRUE(imported.image != nullptr);
        const double import_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        const auto id = imported.resource;
        imported.image.reset();
        const auto load_begin = Clock::now();
        auto loaded = cxxime::load_background_image(id);
        ASSERT_TRUE(loaded != nullptr);
        const double load_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - load_begin).count();
        std::cout << "# preparation," << round << ',' << import_ms << ',' << load_ms << '\n';
    }
    cxxime::Config config;
    ASSERT_TRUE(config.load_themes(std::string(CXXIME_DATA_DIR) + "themes.json"));
    config.theme = config.preset_color_schemes.begin()->first;
    config.background.enabled = true;
    auto texture = image(2048, 2048);
    set_background_dimensions(config, 2048, 2048);
    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.set_preedit("ni'hao");
    const auto candidates = page();
    window.set_preview_background(texture);
    window.update(candidates);
    window.move_to_screen_position(60, 60);
    const auto size = window.layout_size();
    Surface surface(size.cx, size.cy);
    cxxime::GdiRenderer gdi;
    gdi.initialize(window.native_handle(), *window.render_context_for_test().theme, window.dpi());
    cxxime::D2DRenderer d2d;
    ASSERT_TRUE(d2d.initialize(window.native_handle(), *window.render_context_for_test().theme,
                               window.dpi()));
    const RECT rect{0, 0, size.cx, size.cy};
    std::cout << "round,operation,image,p50_us,p95_us,p99_us\n";
    for (int round = 0; round < 7; ++round) {
        for (int mode = 0; mode < 2; ++mode) {
            const bool enabled = (mode + round) % 2 != 0;
            window.hide();
            config.background.enabled = enabled;
            window.set_config(config);
            window.set_preview_background(texture);
            window.update(candidates);
            for (int operation = 0; operation < 3; ++operation) {
                if (operation == 2) {
                    window.show();
                    pump_for(20);
                }
                std::vector<double> samples;
                for (int i = -100; i < 1000; ++i) {
                    const auto begin = Clock::now();
                    if (operation == 0) {
                        window.update(candidates);
                    } else if (operation == 1) {
                        gdi.render(surface.dc, rect, window.render_context_for_test());
                        GdiFlush();
                    } else {
                        ASSERT_TRUE(d2d.render(window.render_context_for_test()));
                    }
                    const double us =
                        std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
                    if (i >= 0) {
                        samples.push_back(us);
                    }
                }
                std::sort(samples.begin(), samples.end());
                std::cout << round << ','
                          << (operation == 0   ? "hidden_update"
                              : operation == 1 ? "gdi_offscreen"
                                               : "d2d_visible_submit")
                          << ',' << enabled << ',' << samples[500] << ',' << samples[950] << ','
                          << samples[990] << '\n';
            }
        }
    }
    d2d.finalize();
    gdi.finalize();
    PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        std::cout << "# working_set_bytes," << memory.WorkingSetSize
                  << "\n# peak_working_set_bytes," << memory.PeakWorkingSetSize << '\n';
    }
    return 0;
}

int preparation_benchmark(const std::wstring& path) {
    background_test::Files files;
    cxxime::BackgroundImport previous_source;
    cxxime::BackgroundPreparation previous_result;
    // A different, fully sized published pair stays alive throughout all samples.
    // Do not let identical input hashes hide the replacement overlap in the cache.
    previous_source.image = image(1024, 1024);
    previous_result.horizontal = image(2048, 2048);
    previous_result.vertical = image(2048, 2048);
    std::cout << "round,source_width,source_height,import_ms,prepare_ms,derived_bytes,"
                 "working_set_bytes,peak_working_set_bytes,private_bytes,peak_private_bytes\n";
    for (int round = 0; round < 7; ++round) {
        const auto begin = Clock::now();
        auto source = cxxime::import_background_source(path);
        ASSERT_TRUE(source.image != nullptr);
        const auto imported_at = Clock::now();
        cxxime::BackgroundConfig config;
        config.enabled = true;
        config.source = source.resource;
        config.source_width = source.image->source_width;
        config.source_height = source.image->source_height;
        config.horizontal.zoom = 4;
        config.horizontal.x = 0.3 + round * 0.05;
        config.vertical.fit = "contain";
        auto prepared = cxxime::prepare_background(config);
        ASSERT_TRUE(prepared.error.empty() && prepared.horizontal && prepared.vertical);
        const auto prepared_at = Clock::now();
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        ASSERT_TRUE(GetProcessMemoryInfo(GetCurrentProcess(),
                                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                         sizeof(memory)));
        const auto derived_bytes =
            prepared.horizontal->pixels.size() + prepared.vertical->pixels.size();
        std::cout << round << ',' << config.source_width << ',' << config.source_height << ','
                  << std::chrono::duration<double, std::milli>(imported_at - begin).count() << ','
                  << std::chrono::duration<double, std::milli>(prepared_at - imported_at).count()
                  << ',' << derived_bytes << ',' << memory.WorkingSetSize << ','
                  << memory.PeakWorkingSetSize << ',' << memory.PrivateUsage << ','
                  << memory.PeakPagefileUsage << '\n';
    }
    return 0;
}
} // namespace

TEST(BackgroundWindow, layout_hit_testing_and_hidden_updates_do_not_depend_on_image) {
    test::ScopedDpiAwarenessContext dpi;
    cxxime::Config config;
    ASSERT_TRUE(config.load_themes(std::string(CXXIME_DATA_DIR) + "themes.json"));
    config.theme = config.preset_color_schemes.begin()->first;
    auto texture = image();
    set_background_dimensions(config);
    for (const char* backend : {"gdi", "d2d"}) {
        for (const char* orientation : {"horizontal", "vertical"}) {
            config.render_backend = backend;
            config.layout = orientation;
            config.background.enabled = false;
            cxxime::CandidateWindow window;
            ASSERT_TRUE(window.create(nullptr, config));
            window.set_preedit("ni'hao");
            window.set_page_info(1, 2);
            window.update(page());
            const auto size = window.layout_size();
            const RECT candidate = window.candidate_rect_for_test(1);
            test::WindowTransitionProbe transitions(window.native_handle());
            config.background.enabled = true;
            window.set_config(config);
            window.set_preview_background(texture);
            for (int i = 0; i < 20; ++i) {
                config.background.horizontal.zoom = 1 + i / 10.0;
                config.background.horizontal.x = i / 20.0;
                window.update(page());
                ASSERT_EQ(window.layout_size().cx, size.cx);
                ASSERT_EQ(window.layout_size().cy, size.cy);
                ASSERT_TRUE(window.render_context_for_test().background_image == texture);
                ASSERT_EQ(window.candidate_rect_for_test(1).right, candidate.right);
                ASSERT_TRUE(!window.is_visible());
            }
            int selected = -1;
            window.set_candidate_selection_callback(
                [&](std::size_t index) { selected = int(index); });
            SendMessageW(window.native_handle(), WM_LBUTTONDOWN, 0,
                         MAKELPARAM((candidate.left + candidate.right) / 2,
                                    (candidate.top + candidate.bottom) / 2));
            ASSERT_EQ(selected, 1);
            transitions.expect(0, 0);
        }
    }
}

TEST(BackgroundWindow, asynchronous_resource_never_shows_hidden_window_or_switches_current_frame) {
    background_test::Files files;
    auto imported = background_test::import_image(
        files.write(L"async.png", 64, 32, background_test::solid(64, 32, 10, 30, 60)));
    ASSERT_TRUE(imported.image != nullptr);
    cxxime::Config config;
    config.background.enabled = true;
    config.background.horizontal.resource = imported.resource;
    set_background_dimensions(config);
    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page());
    const auto first = window.render_context_for_test().background_image;
    {
        test::WindowTransitionProbe transitions(window.native_handle());
        pump_for(100);
        for (int i = 0; i < 20; ++i) {
            window.update(page());
            ASSERT_TRUE(window.render_context_for_test().background_image == first);
        }
        transitions.expect(0, 0);
    }
    const auto deadline = GetTickCount64() + 3000;
    do {
        window.hide();
        window.update(page());
        if (window.render_context_for_test().background_image) {
            break;
        }
        pump_for(5);
    } while (GetTickCount64() < deadline);
    ASSERT_TRUE(window.render_context_for_test().background_image != nullptr);
    ASSERT_TRUE(!window.is_visible());
}

TEST(BackgroundRenderer, image_is_under_opaque_selection_and_high_contrast_remains_plain) {
    cxxime::Config config;
    config.render_backend = "gdi";
    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page());
    cxxime::Theme theme = cxxime::make_light_theme();
    cxxime::LayoutConfig layout;
    layout.border_width = 0;
    layout.round_corner = 0;
    std::vector<cxxime::CandidateRect> rects(1);
    rects[0].index = 0;
    rects[0].highlight_rect = {10, 10, 90, 40};
    cxxime::RenderContext context;
    context.theme = &theme;
    context.layout_cfg = &layout;
    context.rects = &rects;
    context.highlighted = 0;
    context.background_image = image();
    context.background_width = 160;
    context.background_height = 80;
    cxxime::GdiRenderer renderer;
    renderer.initialize(window.native_handle(), theme, 96);
    Surface surface(160, 80);
    const RECT area{0, 0, 160, 80};
    for (double opacity : {0.0, 0.15, 0.5, 1.0}) {
        context.background_opacity = opacity;
        renderer.render(surface.dc, area, context);
        ASSERT_EQ(GetPixel(surface.dc, 40, 20),
                  RGB(theme.hilited_back.r, theme.hilited_back.g, theme.hilited_back.b));
        const auto background = GetPixel(surface.dc, 120, 60);
        ASSERT_LE(std::abs(int(GetRValue(background)) -
                           int(std::round(theme.background.r * (1 - opacity) + 255 * opacity))),
                  1);
        ASSERT_LE(std::abs(int(GetBValue(background)) -
                           int(std::round(theme.background.b * (1 - opacity)))),
                  1);
    }
    context.high_contrast = true;
    renderer.render(surface.dc, area, context);
    ASSERT_EQ(GetPixel(surface.dc, 120, 60),
              RGB(theme.background.r, theme.background.g, theme.background.b));
    renderer.finalize();
    context.high_contrast = false;
    cxxime::D2DRenderer d2d;
    ASSERT_TRUE(d2d.initialize(window.native_handle(), theme, 96));
    ASSERT_TRUE(d2d.render(context));
    d2d.finalize(); // The same path used after D2DERR_RECREATE_TARGET.
    ASSERT_TRUE(d2d.render(context));
    d2d.finalize();
}

TEST(BackgroundWindow, explicit_apply_never_paints_a_temporary_plain_frame) {
    cxxime::Config config;
    ASSERT_TRUE(config.load_themes(std::string(CXXIME_DATA_DIR) + "themes.json"));
    config.theme = config.preset_color_schemes.begin()->first;
    config.background.enabled = true;
    config.render_backend = "gdi";
    auto texture = image();
    set_background_dimensions(config);
    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.set_preview_background(texture);
    window.update(page());
    window.show();
    ASSERT_GT(window.render_context_for_test().background_opacity, 0);
    PaintProbe paints(window);
    test::WindowTransitionProbe transitions(window.native_handle());
    for (int i = 0; i < 10; ++i) {
        config.background.strength = (i + 1) / 10.0;
        window.set_config(config);
        window.set_preview_background(texture);
        window.update(page());
        ASSERT_LT(std::abs(window.render_context_for_test().background_opacity -
                           config.background.strength),
                  0.000001);
        RedrawWindow(window.native_handle(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    ASSERT_GE(paints.paints, 10);
    ASSERT_EQ(paints.plain_frames, 0);
    transitions.expect(0, 0);
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
        return benchmark();
    }
    if (argc == 3 && std::string(argv[1]) == "--prepare-benchmark") {
        return preparation_benchmark(std::filesystem::u8path(argv[2]).wstring());
    }
    return test::RunAllTests();
}
