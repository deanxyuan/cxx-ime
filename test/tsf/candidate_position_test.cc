// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>
#include <utility>

#include "candidate_presentation.h"
#include "support/testutil.h"

namespace {

using cxxime_tsf::CandidatePresentation;
using cxxime_tsf::CaretPosition;
using cxxime_tsf::CaretSource;

CandidatePresentation::TimePoint at(int milliseconds) {
    return CandidatePresentation::TimePoint(std::chrono::milliseconds(1000 + milliseconds));
}

CaretPosition position(LONG x, LONG y, CaretSource source = CaretSource::TextExtent) {
    return {{x, y, x + 1, y + 20}, {}, source};
}

CandidatePresentation presentation() {
    CandidatePresentation result;
    result.update_content(cxxime::CandidatePage{}, "code", 4, 0, 0);
    result.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    result.begin_positioning(false, at(0));
    return result;
}

void assert_shown(const CandidatePresentation& value, const CaretPosition& expected) {
    ASSERT_TRUE(value.should_show_external_window(true));
    ASSERT_TRUE(value.caret_ready_to_show());
    ASSERT_EQ(value.displayed_caret().left, expected.rect.left);
    ASSERT_EQ(value.displayed_caret().top, expected.rect.top);
    ASSERT_EQ(value.displayed_caret_source(), expected.source);
}

} // namespace

TEST(CandidatePosition, transient_projection_never_becomes_visible_before_real_recovery) {
    // Edge and Feishu, plus translated coordinates: the behavior must not depend on an app
    // name or a particular monitor origin. Check every intermediate observation.
    for (const LONG shift : {0L, -900L}) {
        for (const auto& pair : {std::pair<LONG, LONG>{643, 1121}, {325, 733}}) {
            auto value = presentation();
            const auto real = position(pair.first + shift, 200);
            const auto projected = position(pair.second + shift, 200, CaretSource::Projected);
            value.observe_caret(real, 1, at(0));
            ASSERT_TRUE(!value.caret_ready_to_show());
            value.observe_caret(projected, 1, at(1));
            ASSERT_TRUE(!value.caret_ready_to_show());
            value.observe_caret(projected, 1, at(2));
            ASSERT_TRUE(!value.caret_ready_to_show());
            value.observe_caret(real, 1, at(5));
            ASSERT_TRUE(!value.caret_ready_to_show());
            value.update_content(cxxime::CandidatePage{}, "codes", 5, 0, 0);
            value.observe_caret(real, 1, at(31));
            assert_shown(value, real);
            ASSERT_TRUE(value.displayed_caret_confirmed());
            ASSERT_TRUE(!value.caret_poll_pending());
        }
    }
}

TEST(CandidatePosition, cache_reads_cannot_confirm_a_single_projection) {
    auto value = presentation();
    const auto real = position(100, 100);
    const auto projected = position(500, 100, CaretSource::Projected);
    value.observe_caret(real, 1, at(0));
    value.observe_caret(projected, 1, at(1));
    auto cached = projected;
    cached.source = CaretSource::Anchor;
    for (int time : {30, 60, 89}) {
        value.observe_caret(cached, 1, at(time));
        ASSERT_TRUE(!value.settle_caret(at(time)));
        ASSERT_TRUE(!value.caret_ready_to_show());
    }
    ASSERT_TRUE(value.settle_caret(at(90)));
    assert_shown(value, real);
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
}

TEST(CandidatePosition, persistent_offscreen_observations_settle_without_another_host_event) {
    auto value = presentation();
    const auto real = position(100, 100);
    const auto projected = position(500, 100, CaretSource::Projected);
    value.observe_caret(real, 1, at(0));
    value.observe_caret(projected, 1, at(10));
    value.observe_caret(projected, 1, at(45));
    ASSERT_TRUE(!value.caret_ready_to_show());
    ASSERT_TRUE(!value.settle_caret(at(89)));
    ASSERT_TRUE(value.settle_caret(at(90)));
    assert_shown(value, projected);
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
    value.observe_caret(projected, 1, at(120));
    ASSERT_TRUE(!value.caret_poll_pending());
    value.observe_caret(real, 1, at(121));
    assert_shown(value, real);
    ASSERT_TRUE(value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
}

TEST(CandidatePosition, cold_fallback_is_bounded_but_not_a_real_confirmation) {
    for (auto source : {CaretSource::Projected, CaretSource::Boundary, CaretSource::Anchor,
                        CaretSource::WindowFallback}) {
        auto value = presentation();
        const auto fallback = position(500, 100, source);
        value.observe_caret(fallback, 1, at(0));
        ASSERT_TRUE(!value.settle_caret(at(29)));
        ASSERT_TRUE(value.settle_caret(at(30)));
        assert_shown(value, fallback);
        ASSERT_TRUE(!value.displayed_caret_confirmed());
        const auto real = position(100, 100);
        value.observe_caret(real, 1, at(31));
        assert_shown(value, fallback);
        value.observe_caret(real, 1, at(60));
        assert_shown(value, fallback);
        value.observe_caret(real, 1, at(61));
        assert_shown(value, real);
        ASSERT_TRUE(!value.caret_poll_pending());
    }
}

TEST(CandidatePosition, native_caret_does_not_add_initial_or_recovery_delay) {
    auto value = presentation();
    const auto native = position(100, 100, CaretSource::Native);
    value.observe_caret(native, 1, at(0));
    assert_shown(value, native);
    ASSERT_TRUE(!value.caret_poll_pending());
    const auto projected = position(500, 100, CaretSource::Projected);
    value.observe_caret(projected, 1, at(10));
    value.observe_caret(projected, 1, at(40));
    assert_shown(value, projected);
    value.observe_caret(native, 1, at(41));
    assert_shown(value, native);
}

TEST(CandidatePosition, transient_real_jump_is_filtered_and_persistent_jump_is_confirmed_once) {
    for (auto source : {CaretSource::TextExtent, CaretSource::Native}) {
        auto value = presentation();
        const auto real = position(100, 100, source);
        const auto distant = position(500, 400, source);
        value.observe_caret(real, 1, at(0));
        value.observe_caret(real, 1, at(30));
        value.observe_caret(distant, 1, at(40));
        assert_shown(value, real);
        value.observe_caret(real, 1, at(45));
        assert_shown(value, real);
        ASSERT_TRUE(!value.caret_poll_pending());
        value.observe_caret(distant, 1, at(50));
        assert_shown(value, real);
        value.observe_caret(distant, 1, at(80));
        assert_shown(value, distant);
        ASSERT_TRUE(!value.caret_poll_pending());
    }
}

TEST(CandidatePosition, ordinary_confirmed_caret_movement_remains_immediate) {
    auto value = presentation();
    const auto real = position(100, 100);
    value.observe_caret(real, 1, at(0));
    value.observe_caret(real, 1, at(30));
    for (int step = 1; step <= 5; ++step) {
        const auto next = position(100 + step * 10, 100);
        value.observe_caret(next, 1, at(30 + step));
        assert_shown(value, next);
        ASSERT_TRUE(!value.caret_poll_pending());
    }
}

TEST(CandidatePosition, changing_samples_and_content_do_not_extend_initial_budget) {
    auto value = presentation();
    value.observe_caret(position(100, 100), 1, at(0));
    value.observe_caret(position(400, 100), 1, at(40));
    const auto latest = position(700, 100);
    value.update_content(cxxime::CandidatePage{}, "updated", 7, 0, 0);
    value.observe_caret(latest, 1, at(80));
    ASSERT_TRUE(!value.settle_caret(at(89)));
    ASSERT_TRUE(value.settle_caret(at(90)));
    assert_shown(value, latest);
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    value.observe_caret(latest, 1, at(100));
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    value.observe_caret(latest, 1, at(110));
    ASSERT_TRUE(value.displayed_caret_confirmed());
}

TEST(CandidatePosition, alternating_sources_share_one_displayed_position_budget) {
    auto value = presentation();
    const auto anchor = position(100, 100, CaretSource::Native);
    const auto projected = position(500, 100, CaretSource::Projected);
    value.observe_caret(anchor, 1, at(0));
    for (int time : {10, 30, 50, 70}) {
        value.observe_caret(projected, 1, at(time));
        value.observe_caret(position(800 + time, 400), 1, at(time + 10));
        assert_shown(value, anchor);
    }
    value.observe_caret(projected, 1, at(90));
    ASSERT_TRUE(!value.settle_caret(at(99)));
    ASSERT_TRUE(value.settle_caret(at(100)));
    assert_shown(value, position(870, 400));
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
}

TEST(CandidatePosition, view_changes_invalidate_real_and_projection_confirmation_not_budget) {
    auto value = presentation();
    auto real = position(100, 100);
    real.view_rect = {50, 50, 900, 600};
    value.observe_caret(real, 1, at(0));
    value.observe_caret(real, 1, at(30));
    auto projected = position(500, 100, CaretSource::Projected);
    projected.view_rect = real.view_rect;
    value.observe_caret(projected, 1, at(40));
    real.view_rect.bottom += 100;
    value.observe_caret(real, 1, at(50));
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    value.observe_caret(real, 1, at(80));
    assert_shown(value, real);
    ASSERT_TRUE(value.displayed_caret_confirmed());
}

TEST(CandidatePosition, continuous_offscreen_typing_cannot_pin_a_previous_real_position) {
    auto value = presentation();
    const auto real = position(100, 100);
    value.observe_caret(real, 1, at(0));
    value.observe_caret(real, 1, at(30));
    for (int time : {40, 60, 80, 100, 120}) {
        value.observe_caret(position(400 + time, 100, CaretSource::Projected), 1, at(time));
        assert_shown(value, real);
    }
    ASSERT_TRUE(value.settle_caret(at(130)));
    assert_shown(value, position(520, 100, CaretSource::Projected));
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
    // Keep the confirmed real observation for a genuine recovery, not for timeout selection.
    value.observe_caret(real, 1, at(131));
    assert_shown(value, real);
    ASSERT_TRUE(value.displayed_caret_confirmed());
}

TEST(CandidatePosition, moved_view_with_only_a_cached_anchor_gets_one_bounded_retry) {
    auto value = presentation();
    auto real = position(100, 100);
    real.view_rect = {50, 50, 900, 600};
    value.observe_caret(real, 1, at(0));
    value.observe_caret(real, 1, at(30));
    auto translated = position(200, 100, CaretSource::Anchor);
    translated.view_rect = {150, 50, 1000, 600};
    value.observe_caret(translated, 1, at(40));
    ASSERT_TRUE(value.caret_poll_pending());
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    assert_shown(value, real);
    value.observe_caret(translated, 1, at(100));
    ASSERT_TRUE(!value.settle_caret(at(129)));
    ASSERT_TRUE(value.settle_caret(at(130)));
    assert_shown(value, translated);
    ASSERT_TRUE(!value.caret_poll_pending());
    value.observe_caret(translated, 1, at(200));
    ASSERT_TRUE(!value.caret_poll_pending());
}

TEST(CandidatePosition, no_layout_stops_polling_and_recovers_on_a_later_event) {
    auto value = presentation();
    ASSERT_TRUE(!value.needs_caret_fallback(at(29)));
    ASSERT_TRUE(value.needs_caret_fallback(at(30)));
    ASSERT_TRUE(!value.settle_caret(at(90)));
    ASSERT_TRUE(!value.caret_poll_pending());
    ASSERT_TRUE(value.needs_caret_fallback(at(1000)));
    ASSERT_TRUE(!value.caret_ready_to_show());
    const auto real = position(100, 100);
    value.observe_caret(real, 1, at(1000));
    assert_shown(value, real);
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
}

TEST(CandidatePosition, restart_budget_begins_after_queued_write_and_is_not_renewed) {
    auto value = presentation();
    value.begin_positioning(true, at(0));
    const auto real = position(100, 100);
    value.observe_caret(real, 1, at(10));
    ASSERT_TRUE(!value.caret_poll_pending());
    ASSERT_TRUE(!value.settle_caret(at(500)));
    const auto old_generation = value.generation();
    value.update_content(cxxime::CandidatePage{}, "new", 3, 0, 0);
    ASSERT_TRUE(!value.complete_composition_restart(old_generation, at(500)));
    ASSERT_TRUE(value.complete_composition_restart(value.generation(), at(500)));
    ASSERT_TRUE(!value.settle_caret(at(649)));
    ASSERT_TRUE(value.complete_composition_restart(value.generation(), at(649)));
    ASSERT_TRUE(value.settle_caret(at(650)));
    assert_shown(value, real);
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
}

TEST(CandidatePosition, restart_without_layout_uses_fallback_only_after_its_write_and_deadline) {
    auto value = presentation();
    value.begin_positioning(true, at(0));
    ASSERT_TRUE(!value.needs_caret_fallback(at(500)));
    ASSERT_TRUE(value.complete_composition_restart(value.generation(), at(500)));
    ASSERT_TRUE(!value.needs_caret_fallback(at(649)));
    ASSERT_TRUE(value.needs_caret_fallback(at(650)));
    const auto fallback = position(100, 100, CaretSource::WindowFallback);
    value.observe_caret(fallback, 1, at(650));
    assert_shown(value, fallback);
    ASSERT_TRUE(!value.displayed_caret_confirmed());
    ASSERT_TRUE(!value.caret_poll_pending());
}

TEST(CandidatePosition, target_and_composition_boundaries_do_not_inherit_confirmation) {
    auto value = presentation();
    const auto real = position(100, 100);
    value.observe_caret(real, 1, at(0));
    value.observe_caret(real, 1, at(30));
    ASSERT_TRUE(value.can_retain_displayed_caret(1));
    ASSERT_TRUE(!value.can_retain_displayed_caret(2));
    value.observe_caret(real, 2, at(40));
    ASSERT_TRUE(!value.caret_ready_to_show());
    value.observe_caret(real, 2, at(70));
    assert_shown(value, real);
    value.finish();
    value.update_content(cxxime::CandidatePage{}, "new", 3, 0, 0);
    value.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    value.begin_positioning(false, at(100));
    value.observe_caret(real, 2, at(100));
    ASSERT_TRUE(!value.caret_ready_to_show());
    value.observe_caret(real, 2, at(130));
    assert_shown(value, real);
}
