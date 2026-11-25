// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <windows.h>
#include <dwmapi.h>

#include <cxxime/window_position.h>

#include "support/testutil.h"

namespace {

const HWND kView = reinterpret_cast<HWND>(1);
const HWND kRoot = reinterpret_cast<HWND>(2);
const auto kSourceContext = reinterpret_cast<DPI_AWARENESS_CONTEXT>(0x6011);

enum class Failure {
    None,
    Context,
    Enter,
    Client,
    Mapping,
    Conversion,
    Scale,
};

struct WindowModel {
    RECT logical = {2800, 400, 4000, 1000};
    int numerator = 4;
    int denominator = 5;
    DPI_AWARENESS_CONTEXT source_context = kSourceContext;
    DPI_AWARENESS_CONTEXT root_context = kSourceContext;
    DPI_AWARENESS_CONTEXT caller_context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
    UINT view_dpi = 120;
    UINT root_dpi = 120;
    Failure failure = Failure::None;
    unsigned conversions = 0;
    unsigned rejected = 0;
    unsigned inverse_calls = 0;
    unsigned switches = 0;
};

WindowModel model;

BOOL WINAPI model_is_window(HWND window) { return window == kView || window == kRoot; }

DPI_AWARENESS_CONTEXT WINAPI model_window_context(HWND window) {
    if (!model_is_window(window) || model.failure == Failure::Context) {
        return nullptr;
    }
    return window == kRoot ? model.root_context : model.source_context;
}

DPI_AWARENESS_CONTEXT WINAPI model_set_context(DPI_AWARENESS_CONTEXT context) {
    ++model.switches;
    if (!context || (model.failure == Failure::Enter && context == model.source_context)) {
        return nullptr;
    }
    const auto previous = model.caller_context;
    model.caller_context = context;
    return previous;
}

UINT WINAPI model_window_dpi(HWND window) {
    return window == kView ? model.view_dpi : window == kRoot ? model.root_dpi : 0;
}

BOOL WINAPI model_contexts_equal(DPI_AWARENESS_CONTEXT left, DPI_AWARENESS_CONTEXT right) {
    return left && left == right;
}

BOOL WINAPI model_client(HWND window, RECT* rect) {
    ASSERT_EQ(model.caller_context, model.source_context);
    if (!model_is_window(window) || model.failure == Failure::Client) {
        return FALSE;
    }
    *rect = {0, 0, model.logical.right - model.logical.left,
             model.logical.bottom - model.logical.top};
    return TRUE;
}

BOOL WINAPI model_to_screen(HWND window, POINT* point) {
    ASSERT_EQ(model.caller_context, model.source_context);
    if (!model_is_window(window) || model.failure == Failure::Mapping) {
        return FALSE;
    }
    point->x += model.logical.left;
    point->y += model.logical.top;
    return TRUE;
}

BOOL WINAPI model_to_physical(HWND window, POINT* point) {
    ++model.conversions;
    RECT bounds = model.logical;
    // Reproduce the observed Windows boundary rule, not an unconditional mock
    // success: a foreign caller context checks the physical window bounds.
    if (model.caller_context != model.source_context) {
        bounds = {
            MulDiv(bounds.left, model.numerator, model.denominator),
            MulDiv(bounds.top, model.numerator, model.denominator),
            MulDiv(bounds.right, model.numerator, model.denominator),
            MulDiv(bounds.bottom, model.numerator, model.denominator),
        };
    }
    if (!model_is_window(window) || model.failure == Failure::Conversion ||
        point->x < bounds.left || point->x > bounds.right || point->y < bounds.top ||
        point->y > bounds.bottom ||
        (model.failure == Failure::Scale &&
         (point->x != model.logical.left || point->y != model.logical.top))) {
        ++model.rejected;
        return FALSE;
    }
    point->x = MulDiv(point->x, model.numerator, model.denominator);
    point->y = MulDiv(point->y, model.numerator, model.denominator);
    return TRUE;
}

BOOL WINAPI model_to_logical(HWND, POINT*) {
    ++model.inverse_calls;
    return FALSE;
}

} // namespace

// Compile the production algorithm with narrow Win32 bindings, as in
// settings_activation_test. No alternate algorithm or production injection hooks.
#define IsWindow                               model_is_window
#define GetWindowDpiAwarenessContext           model_window_context
#define SetThreadDpiAwarenessContext           model_set_context
#define GetDpiForWindow                        model_window_dpi
#define AreDpiAwarenessContextsEqual           model_contexts_equal
#define GetClientRect                          model_client
#define ClientToScreen                         model_to_screen
#define LogicalToPhysicalPointForPerMonitorDPI model_to_physical
#define PhysicalToLogicalPointForPerMonitorDPI model_to_logical
#include "../../ui/src/window_position.cc"
#include "../../server/src/ui_presentation_target.h"
#undef IsWindow
#undef GetWindowDpiAwarenessContext
#undef SetThreadDpiAwarenessContext
#undef GetDpiForWindow
#undef AreDpiAwarenessContextsEqual
#undef GetClientRect
#undef ClientToScreen
#undef LogicalToPhysicalPointForPerMonitorDPI
#undef PhysicalToLogicalPointForPerMonitorDPI

namespace {

void expect_conversion(const RECT& source, const RECT& expected) {
    const auto caller = model.caller_context;
    RECT actual = {};
    ASSERT_TRUE(cxxime::logical_screen_rect_to_physical(kView, source, &actual));
    ASSERT_TRUE(EqualRect(&actual, &expected));
    ASSERT_EQ(model.caller_context, caller);
    ASSERT_EQ(model.inverse_calls, 0u);
}

TEST(CoordinateConversion, mixed_dpi_left_right_left_uses_source_bounds_and_restores_caller) {
    model = {};
    POINT right = {3704, 454};
    ASSERT_TRUE(!model_to_physical(kView, &right));
    ASSERT_EQ(model.rejected, 1u);
    model.conversions = 0;
    model.rejected = 0;
    const RECT source[] = {{2900, 454, 2901, 487}, {3704, 454, 3705, 487}, {2900, 454, 2901, 487}};
    const RECT expected[] = {
        {2320, 363, 2321, 390},
        {2963, 363, 2964, 390},
        {2320, 363, 2321, 390},
    };
    for (int i = 0; i < 3; ++i) {
        expect_conversion(source[i], expected[i]);
    }
    ASSERT_EQ(model.conversions, 6u);
    ASSERT_EQ(model.rejected, 0u);
    ASSERT_EQ(model.switches, 6u);
}

TEST(CoordinateConversion, each_call_uses_current_mapping_including_negative_and_equal_dpi) {
    model = {};
    expect_conversion({3000, 500, 3001, 520}, {2400, 400, 2401, 416});
    model.logical = {-2000, -1000, -1000, -200};
    model.numerator = 3;
    model.denominator = 2;
    model.source_context = DPI_AWARENESS_CONTEXT_UNAWARE;
    expect_conversion({-1800, -800, -1798, -780}, {-2700, -1200, -2697, -1170});
    model.numerator = 1;
    model.denominator = 1;
    model.source_context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
    expect_conversion({-1800, -800, -1798, -780}, {-1800, -800, -1798, -780});
    expect_conversion({-1800, -800, -1800, -800}, {-1800, -800, -1799, -799});
}

TEST(CoordinateConversion, clipped_rectangles_keep_one_logical_space_through_fallback) {
    model = {};
    model.logical = {200, 200, 400, 400};
    model.numerator = 1;
    model.denominator = 2;
    expect_conversion({398, 398, 402, 420}, {199, 199, 201, 210});
    expect_conversion({180, 200, 202, 220}, {90, 100, 101, 110});
    model.logical.right = 401;
    expect_conversion({180, 280, 420, 300}, {90, 140, 210, 150});
    ASSERT_GT(model.rejected, 0u);
    ASSERT_EQ(model.inverse_calls, 0u);
}

TEST(CoordinateConversion, failed_operations_preserve_output_and_caller_context) {
    const Failure failures[] = {Failure::Context, Failure::Enter,      Failure::Client,
                                Failure::Mapping, Failure::Conversion, Failure::Scale};
    for (Failure failure : failures) {
        model = {};
        model.failure = failure;
        const auto caller = model.caller_context;
        const RECT sentinel = {11, 12, 13, 14};
        RECT actual = sentinel;
        // Force client-intersection fallback except for the axis-sampling failure.
        const RECT source =
            failure == Failure::Scale ? RECT{2800, 400, 4010, 1010} : RECT{2700, 500, 4100, 520};
        ASSERT_TRUE(!cxxime::logical_screen_rect_to_physical(kView, source, &actual));
        ASSERT_TRUE(EqualRect(&actual, &sentinel));
        ASSERT_EQ(model.caller_context, caller);
    }
    model = {};
    const RECT sentinel = {11, 12, 13, 14};
    RECT actual = sentinel;
    ASSERT_TRUE(!cxxime::logical_screen_rect_to_physical(kView, {4100, 500, 4200, 520}, &actual));
    ASSERT_TRUE(!cxxime::logical_screen_rect_to_physical(nullptr, sentinel, &actual));
    ASSERT_TRUE(!cxxime::logical_screen_rect_to_physical(kView, {10, 10, 9, 11}, &actual));
    ASSERT_TRUE(EqualRect(&actual, &sentinel));
    ASSERT_EQ(model.caller_context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

TEST(CoordinateConversion, root_reuse_requires_equivalent_context_and_nonzero_equal_dpi) {
    model = {};
    ASSERT_TRUE(presentation_windows_share_coordinates(kView, kRoot));
    model.root_context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
    ASSERT_TRUE(!presentation_windows_share_coordinates(kView, kRoot));
    model.root_context = model.source_context;
    model.root_dpi = 96;
    ASSERT_TRUE(!presentation_windows_share_coordinates(kView, kRoot));
    model.root_dpi = 0;
    model.view_dpi = 0;
    ASSERT_TRUE(!presentation_windows_share_coordinates(kView, kRoot));
    model = {};
    model.root_context = nullptr;
    ASSERT_TRUE(!presentation_windows_share_coordinates(kView, kRoot));
    ASSERT_TRUE(!presentation_windows_share_coordinates(nullptr, kRoot));
}

} // namespace

RUN_ALL_TESTS()
