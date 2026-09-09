// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdint>
#include <initializer_list>
#include <utility>

#include <windows.h>

#include <cxxime/context.h>
#include <cxxime/processor.h>
#include <cxxime/wubi_processor.h>

#include "support/testutil.h"

namespace {

void prepare_context(cxxime::Context& context) {
    ASSERT_TRUE(context.set_preedit("ni"));
    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text = "candidate";
    page.candidates.push_back(candidate);
    page.extent.known_count = 27;
    page.extent.state = cxxime::CandidateExtentState::kHasMore;
    page.page_size = 9;
    context.update_candidates(std::move(page));
}

cxxime::KeyEvent make_key(uint32_t keycode, uint32_t modifiers = 0) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    event.modifiers = modifiers;
    return event;
}

cxxime::CandidatePage make_candidate_page(int page_index, int page_offset, int known_count,
                                          int candidate_count);

template <typename Processor>
void verify_oem_pagination() {
    Processor processor;
    cxxime::Context context;
    prepare_context(context);

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_PLUS), context),
              cxxime::ProcessResult::ACCEPTED);
    const auto next = context.take_candidate_navigation();
    ASSERT_TRUE(next.has_value());
    ASSERT_EQ(next->direction, cxxime::CandidateNavigation::kNextPage);
    cxxime::CandidatePage second;
    second.page_index = 1;
    second.page_offset = 1;
    second.extent.known_count = 2;
    second.candidates.push_back({"second"});
    ASSERT_TRUE(context.apply_next_page(
        cxxime::make_translation_result(std::move(second), context.active_input().size()), 1));

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_MINUS), context),
              cxxime::ProcessResult::ACCEPTED);
    const auto previous = context.take_candidate_navigation();
    ASSERT_TRUE(previous.has_value());
    ASSERT_EQ(previous->direction, cxxime::CandidateNavigation::kPreviousPage);
    ASSERT_TRUE(context.apply_previous_page());
    ASSERT_EQ(context.page_index(), 0);

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_PLUS, 0x01), context),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(processor.process_key(make_key(VK_OEM_MINUS, 0x02), context),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(context.page_index(), 0);
}

template <typename Processor>
void verify_second_visible_candidate_is_selectable() {
    Processor processor;
    cxxime::Context context;
    ASSERT_TRUE(context.set_preedit("code"));
    context.visible_candidate_count = 2;
    cxxime::CandidatePage page;
    for (const char* text : {"first", "second"}) {
        cxxime::Candidate candidate;
        candidate.text = text;
        page.candidates.push_back(std::move(candidate));
    }
    context.update_candidates(std::move(page));

    ASSERT_EQ(processor.process_key(make_key('2'), context),
              cxxime::ProcessResult::CANDIDATE_SELECTED);
    ASSERT_EQ(context.take_requested_candidate_selection().value_or(-1), 1);
}

cxxime::CandidatePage make_candidate_page(int page_index, int page_offset, int known_count,
                                          int candidate_count) {
    cxxime::CandidatePage page;
    page.page_index = page_index;
    page.page_offset = page_offset;
    page.extent.known_count = known_count;
    page.extent.state = page_offset + candidate_count < known_count
                            ? cxxime::CandidateExtentState::kHasMore
                            : cxxime::CandidateExtentState::kExhausted;
    for (int i = 0; i < candidate_count; ++i) {
        cxxime::Candidate candidate;
        candidate.text = "candidate";
        page.candidates.push_back(std::move(candidate));
    }
    return page;
}

template <typename Processor>
void verify_arrow_pagination_without_wrapping() {
    Processor processor;
    cxxime::Context context;
    ASSERT_TRUE(context.set_preedit("ni"));
    context.visible_candidate_count = 5;
    context.update_candidates(make_candidate_page(0, 0, 7, 5));

    ASSERT_EQ(processor.process_key(make_key(VK_UP), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.translation().highlighted, 0);
    ASSERT_EQ(context.page_offset(), 0);

    ASSERT_EQ(processor.process_key(make_key(VK_DOWN), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.translation().highlighted, 1);
    context.translation().highlighted = 4;
    ASSERT_EQ(processor.process_key(make_key(VK_DOWN), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(context.take_candidate_navigation().has_value());
    ASSERT_TRUE(context.apply_next_page(
        cxxime::make_translation_result(
            make_candidate_page(1, 5, 7, 2), context.active_input().size()),
        5));
    ASSERT_EQ(context.page_offset(), 5);

    context.visible_candidate_count = 2;
    ASSERT_EQ(context.translation().highlighted, 0);
    ASSERT_EQ(processor.process_key(make_key(VK_DOWN), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.translation().highlighted, 1);
    ASSERT_TRUE(!context.take_candidate_navigation().has_value());
    ASSERT_EQ(processor.process_key(make_key(VK_DOWN), context), cxxime::ProcessResult::ACCEPTED);
    const auto end = context.take_candidate_navigation();
    ASSERT_TRUE(end.has_value());
    ASSERT_TRUE(!cxxime::candidate_extent_may_continue(context.translation().extent, 7));
    ASSERT_EQ(context.translation().highlighted, 1);
    ASSERT_EQ(context.page_offset(), 5);

    context.translation().highlighted = 0;
    ASSERT_EQ(processor.process_key(make_key(VK_UP), context), cxxime::ProcessResult::ACCEPTED);
    const auto previous = context.take_candidate_navigation();
    ASSERT_TRUE(previous.has_value());
    ASSERT_TRUE(context.apply_previous_page(previous->highlight_last));
    ASSERT_EQ(context.page_offset(), 0);
    ASSERT_EQ(context.translation().highlighted, 4);
}

} // namespace

TEST(ProcessorPagination, pinyin_and_wubi_support_minus_and_equal) {
    verify_oem_pagination<cxxime::PinyinProcessor>();
    verify_oem_pagination<cxxime::WubiProcessor>();
}

TEST(ProcessorPagination, pinyin_and_wubi_select_the_second_visible_candidate) {
    verify_second_visible_candidate_is_selectable<cxxime::PinyinProcessor>();
    verify_second_visible_candidate_is_selectable<cxxime::WubiProcessor>();
}

TEST(ProcessorPagination, pinyin_and_wubi_arrows_cross_pages_without_wrapping) {
    verify_arrow_pagination_without_wrapping<cxxime::PinyinProcessor>();
    verify_arrow_pagination_without_wrapping<cxxime::WubiProcessor>();
}

RUN_ALL_TESTS()
