// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstring>

#include "support/testutil.h"

#include "edit_target.h"

namespace {

cxxime_tsf::EditTargetEvidence captured_selection() {
    cxxime_tsf::EditTargetEvidence evidence;
    evidence.request_hr = S_OK;
    evidence.session_hr = S_OK;
    evidence.selection_hr = S_OK;
    evidence.selection_count = 1;
    evidence.selection_available = true;
    return evidence;
}

} // namespace

TEST(EditTarget, text_ext_fallback_uses_caret_owner_instead_of_focus) {
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 100, 100, 700, 200,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(parent != nullptr);
    HWND focus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 30, 200, 60, parent,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(focus != nullptr);
    HWND caret_owner = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 240, 30, 200, 60,
                                       parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(caret_owner != nullptr);

    SetFocus(focus);
    ASSERT_EQ(GetFocus(), focus);
    ASSERT_TRUE(CreateCaret(caret_owner, nullptr, 1, 20) != FALSE);
    ASSERT_TRUE(SetCaretPos(40, 12) != FALSE);
    POINT caret = {};
    ASSERT_TRUE(GetCaretPos(&caret) != FALSE);
    GUITHREADINFO gui = {sizeof(gui)};
    ASSERT_TRUE(GetGUIThreadInfo(GetCurrentThreadId(), &gui) != FALSE);
    ASSERT_EQ(gui.hwndCaret, caret_owner);

    POINT expected = caret;
    ASSERT_TRUE(ClientToScreen(caret_owner, &expected) != FALSE);
    RECT actual = {1000, 1000, 1001, 1020};
    cxxime_tsf::TextExtRectTrace trace;
    ASSERT_TRUE(cxxime_tsf::normalize_text_ext_rect(focus, parent, &actual, &trace));
    ASSERT_EQ(actual.left, expected.x);
    ASSERT_EQ(actual.top, expected.y);
    ASSERT_EQ(actual.right - actual.left, 1);
    ASSERT_EQ(std::strcmp(trace.branch, "native_caret"), 0);
    ASSERT_EQ(trace.caret_hwnd, caret_owner);
    ASSERT_TRUE(trace.gui_info_ok && trace.caret_pos_ok && trace.caret_map_ok);

    RECT screen = {130, 140, 131, 160};
    cxxime_tsf::TextExtRectTrace screen_trace;
    ASSERT_TRUE(cxxime_tsf::normalize_text_ext_rect(focus, parent, &screen, &screen_trace));
    ASSERT_EQ(std::strcmp(screen_trace.branch, "foreground_screen"), 0);
    ASSERT_TRUE(!screen_trace.caret_pos_queried);

    RECT native = {0, 0, 1, 20};
    ASSERT_TRUE(cxxime_tsf::map_current_thread_caret_rect(parent, &native));
    ASSERT_EQ(native.left, expected.x);
    ASSERT_EQ(native.top, expected.y);

    DestroyCaret();
    RECT unavailable = {0, 0, 1, 20};
    ASSERT_TRUE(!cxxime_tsf::map_current_thread_caret_rect(parent, &unavailable));
    RECT without_owner = {1000, 1000, 1001, 1020};
    cxxime_tsf::TextExtRectTrace without_owner_trace;
    cxxime_tsf::normalize_text_ext_rect(focus, parent, &without_owner, &without_owner_trace);
    ASSERT_TRUE(without_owner_trace.caret_hwnd == nullptr);
    ASSERT_TRUE(without_owner_trace.caret_pos_queried);
    ASSERT_TRUE(!without_owner_trace.caret_map_ok);
    DestroyWindow(parent);
}

TEST(EditTarget, native_fallback_uses_top_level_caret_owner) {
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 100, 100, 300, 100,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(window != nullptr);
    HWND focus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 10, 150, 60, window,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(focus != nullptr);
    SetFocus(focus);
    ASSERT_EQ(GetFocus(), focus);
    ASSERT_TRUE(CreateCaret(window, nullptr, 1, 20) != FALSE);
    ASSERT_TRUE(SetCaretPos(40, 12) != FALSE);
    POINT caret = {};
    ASSERT_TRUE(GetCaretPos(&caret) != FALSE);
    POINT expected = caret;
    ASSERT_TRUE(ClientToScreen(window, &expected) != FALSE);
    RECT actual = {};
    ASSERT_TRUE(cxxime_tsf::resolve_native_caret_rect(window, &actual));
    ASSERT_EQ(actual.left, expected.x);
    ASSERT_EQ(actual.top, expected.y);
    DestroyCaret();
    DestroyWindow(window);
}

TEST(EditTarget, viewport_tracker_keeps_hidden_input_on_the_last_visible_baseline) {
    cxxime_tsf::CaretViewportTracker tracker;
    const std::uint64_t target_generation = 7;
    const RECT view = {201, 111, 1580, 941};
    const RECT visible = {233, 846, 244, 871};
    ASSERT_TRUE(tracker.remember(target_generation, view, visible));

    RECT resolved = {};
    const RECT first_hidden = {255, 1896, 266, 1921};
    ASSERT_EQ(tracker.resolve(target_generation, &view, &first_hidden, false, &resolved),
              cxxime_tsf::CaretViewportFallback::Projected);
    ASSERT_EQ(resolved.left, 255);
    ASSERT_EQ(resolved.top, visible.top);
    ASSERT_EQ(resolved.right - resolved.left, visible.right - visible.left);
    ASSERT_EQ(resolved.bottom - resolved.top, visible.bottom - visible.top);

    const RECT next_hidden = {277, 1896, 288, 1921};
    ASSERT_EQ(tracker.resolve(target_generation, &view, &next_hidden, false, &resolved),
              cxxime_tsf::CaretViewportFallback::Projected);
    ASSERT_EQ(resolved.left, 277);
    ASSERT_EQ(resolved.top, visible.top);

    ASSERT_EQ(tracker.resolve(target_generation, &view, &next_hidden, true, &resolved),
              cxxime_tsf::CaretViewportFallback::Anchor);
    ASSERT_EQ(resolved.left, visible.left);
    ASSERT_EQ(tracker.resolve(target_generation + 1, &view, &next_hidden, false, &resolved),
              cxxime_tsf::CaretViewportFallback::None);
}

TEST(EditTarget, viewport_tracker_ignores_unrelated_horizontal_geometry) {
    cxxime_tsf::CaretViewportTracker tracker;
    const RECT view = {100, 100, 900, 700};
    const RECT visible = {180, 620, 181, 645};
    ASSERT_TRUE(tracker.remember(3, view, visible));

    RECT resolved = {};
    const RECT unrelated = {1200, 900, 1210, 925};
    ASSERT_EQ(tracker.resolve(3, &view, &unrelated, false, &resolved),
              cxxime_tsf::CaretViewportFallback::Anchor);
    ASSERT_EQ(resolved.left, visible.left);
    ASSERT_EQ(resolved.top, visible.top);

    const RECT moved_view = {140, 130, 940, 730};
    ASSERT_EQ(tracker.resolve(3, &moved_view, nullptr, false, &resolved),
              cxxime_tsf::CaretViewportFallback::Anchor);
    ASSERT_EQ(resolved.left, visible.left + 40);
    ASSERT_EQ(resolved.top, visible.top + 30);
}

TEST(EditTarget, unknown_when_inspection_failed) {
    cxxime_tsf::EditTargetEvidence evidence;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Unknown);

    evidence.request_hr = S_OK;
    evidence.session_hr = S_OK;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Unknown);
}

TEST(EditTarget, no_target_when_all_editing_evidence_is_absent) {
    const auto evidence = captured_selection();
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence),
        cxxime_tsf::EditTargetState::NoEditTarget);
}

TEST(EditTarget, unknown_when_focused_child_requires_provisional_composition) {
    auto evidence = captured_selection();
    evidence.context_is_focused_child = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Unknown);
}

TEST(EditTarget, outside_view_is_diagnostic_only) {
    const RECT uninstall_view = {1445, 857, 2368, 1235};
    const RECT uninstall_text = {3540, 1891, 3541, 1891};
    ASSERT_TRUE(cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, S_OK, uninstall_text, false));

    ASSERT_TRUE(cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, S_OK, {1444, 900, 1445, 920}, false));
    ASSERT_TRUE(cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, S_OK, {2368, 900, 2369, 920}, false));
    ASSERT_TRUE(cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, S_OK, {1500, 856, 1520, 857}, false));
    ASSERT_TRUE(cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, S_OK, {1500, 1235, 1520, 1236}, false));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, S_OK, {1445, 857, 1446, 858}, false));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, S_OK, uninstall_text, true));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_outside_view(
        E_FAIL, uninstall_view, S_OK, uninstall_text, false));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_outside_view(
        S_OK, {}, S_OK, uninstall_text, false));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_outside_view(
        S_OK, uninstall_view, E_FAIL, uninstall_text, false));

    const RECT dota_text = {-1000, -1000, -983, -980};
    ASSERT_TRUE(cxxime_tsf::text_rect_is_meaningful(S_OK, dota_text, false));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_meaningful(S_OK, uninstall_text, false));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_meaningful(S_OK, dota_text, true));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_meaningful(E_FAIL, dota_text, false));

    auto evidence = captured_selection();
    evidence.context_is_focused_child = true;
    evidence.text_rect_outside_view = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence),
        cxxime_tsf::EditTargetState::Unknown);

    evidence.foreground_is_shell_window = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence),
        cxxime_tsf::EditTargetState::NoEditTarget);
}

TEST(EditTarget, detects_only_known_narrow_view_placeholders) {
    const RECT fullscreen_view = {1, 1, 1506, 954};
    const RECT douyu_right_boundary = {1506, 914, 1507, 934};

    ASSERT_TRUE(cxxime_tsf::text_rect_is_placeholder(fullscreen_view, {1, 1, 2, 21}));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_placeholder(fullscreen_view, douyu_right_boundary));
    ASSERT_TRUE(cxxime_tsf::text_rect_requires_composition_refresh(
        fullscreen_view, douyu_right_boundary));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_placeholder(fullscreen_view, {1, 1, 1, 1}));
    ASSERT_TRUE(cxxime_tsf::text_rect_requires_composition_refresh(
        fullscreen_view, {1, 1, 1, 1}));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_placeholder(
        fullscreen_view, {1505, 914, 1506, 934}));
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_placeholder(
        fullscreen_view, {1490, 914, 1507, 934}));
    ASSERT_TRUE(!cxxime_tsf::text_rect_requires_composition_refresh(
        fullscreen_view, {1505, 914, 1506, 934}));

    auto evidence = captured_selection();
    evidence.context_is_focused_child = true;
    evidence.text_rect_outside_view = true;
    evidence.has_meaningful_text_rect =
        cxxime_tsf::text_rect_is_meaningful(S_OK, douyu_right_boundary, false);
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence),
        cxxime_tsf::EditTargetState::Editable);

    const RECT dota_view = {0, 0, 1920, 1080};
    ASSERT_TRUE(!cxxime_tsf::text_rect_is_placeholder(dota_view, {-1000, -1000, -983, -980}));
    ASSERT_TRUE(!cxxime_tsf::text_rect_requires_composition_refresh(
        dota_view, {-1000, -1000, -983, -980}));
}

TEST(EditTarget, no_target_on_shell_surface_without_editing_evidence) {
    auto evidence = captured_selection();
    evidence.context_is_focused_child = true;
    evidence.foreground_is_shell_window = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence),
              cxxime_tsf::EditTargetState::NoEditTarget);
}

TEST(EditTarget, editable_when_any_supported_evidence_is_present) {
    auto evidence = captured_selection();
    evidence.has_active_selection = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.has_input_scope = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.has_native_caret = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.has_meaningful_text_rect = true;
    evidence.text_rect_outside_view = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.has_input_scope = true;
    evidence.text_rect_outside_view = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);

    evidence = captured_selection();
    evidence.foreground_is_shell_window = true;
    evidence.has_native_caret = true;
    ASSERT_EQ(cxxime_tsf::classify_edit_target(evidence), cxxime_tsf::EditTargetState::Editable);
}

RUN_ALL_TESTS()
