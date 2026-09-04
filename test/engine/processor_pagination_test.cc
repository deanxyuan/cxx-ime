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
    page.total_count = 27;
    page.page_size = 9;
    context.update_candidates(std::move(page));
}

cxxime::KeyEvent make_key(uint32_t keycode, uint32_t modifiers = 0) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    event.modifiers = modifiers;
    return event;
}

template <typename Processor>
void verify_oem_pagination() {
    Processor processor;
    cxxime::Context context;
    prepare_context(context);

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_PLUS), context),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_index(), 1);

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_MINUS), context),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_index(), 0);

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_PLUS, 0x01), context),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(processor.process_key(make_key(VK_OEM_MINUS, 0x02), context),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(context.page_index(), 0);
}

template <typename Processor>
void verify_variable_page_offsets() {
    Processor processor;
    cxxime::Context context;
    ASSERT_TRUE(context.set_preedit("ni"));
    cxxime::CandidatePage page;
    page.total_count = 12;
    page.page_size = 7;
    for (int i = 0; i < 7; ++i) {
        cxxime::Candidate candidate;
        candidate.text = "candidate";
        page.candidates.push_back(candidate);
    }
    context.update_candidates(std::move(page));

    context.visible_candidate_count = 2;
    ASSERT_EQ(processor.process_key(make_key('3'), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(context.committed_text.empty());

    ASSERT_EQ(processor.process_key(make_key(VK_NEXT), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_index(), 1);
    ASSERT_EQ(context.page_offset(), 2);

    context.visible_candidate_count = 3;
    ASSERT_EQ(processor.process_key(make_key(VK_NEXT), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_index(), 2);
    ASSERT_EQ(context.page_offset(), 5);

    ASSERT_EQ(processor.process_key(make_key(VK_PRIOR), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_index(), 1);
    ASSERT_EQ(context.page_offset(), 2);
    cxxime::CandidatePage previous_page;
    previous_page.page_index = 1;
    previous_page.page_offset = 2;
    previous_page.total_count = 12;
    for (int index = 0; index < 3; ++index) {
        cxxime::Candidate candidate;
        candidate.text = "candidate";
        previous_page.candidates.push_back(std::move(candidate));
    }
    context.update_candidates(std::move(previous_page));
    ASSERT_EQ(context.translation().highlighted, 0);

    ASSERT_EQ(processor.process_key(make_key(VK_PRIOR), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_index(), 0);
    ASSERT_EQ(context.page_offset(), 0);
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

cxxime::CandidatePage make_candidate_page(int page_index, int page_offset, int total_count,
                                          int candidate_count) {
    cxxime::CandidatePage page;
    page.page_index = page_index;
    page.page_offset = page_offset;
    page.total_count = total_count;
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
    ASSERT_EQ(context.page_offset(), 5);

    context.update_candidates(make_candidate_page(1, 5, 7, 2));
    context.visible_candidate_count = 2;
    ASSERT_EQ(context.translation().highlighted, 0);
    ASSERT_EQ(processor.process_key(make_key(VK_DOWN), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.translation().highlighted, 1);
    ASSERT_EQ(processor.process_key(make_key(VK_DOWN), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.translation().highlighted, 1);
    ASSERT_EQ(context.page_offset(), 5);

    context.translation().highlighted = 0;
    ASSERT_EQ(processor.process_key(make_key(VK_UP), context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_offset(), 0);
    context.update_candidates(make_candidate_page(0, 0, 7, 5));
    ASSERT_EQ(context.translation().highlighted, 4);
}

} // namespace

TEST(ProcessorPagination, pinyin_supports_minus_and_equal) {
    verify_oem_pagination<cxxime::PinyinProcessor>();
}

TEST(ProcessorPagination, wubi_supports_minus_and_equal) {
    verify_oem_pagination<cxxime::WubiProcessor>();
}

TEST(ProcessorPagination, pinyin_uses_visible_candidate_count_as_page_step) {
    verify_variable_page_offsets<cxxime::PinyinProcessor>();
}

TEST(ProcessorPagination, wubi_uses_visible_candidate_count_as_page_step) {
    verify_variable_page_offsets<cxxime::WubiProcessor>();
}

TEST(ProcessorPagination, pinyin_selects_second_visible_candidate) {
    verify_second_visible_candidate_is_selectable<cxxime::PinyinProcessor>();
}

TEST(ProcessorPagination, wubi_selects_second_visible_candidate) {
    verify_second_visible_candidate_is_selectable<cxxime::WubiProcessor>();
}

TEST(ProcessorPagination, pinyin_arrows_cross_pages_without_wrapping) {
    verify_arrow_pagination_without_wrapping<cxxime::PinyinProcessor>();
}

TEST(ProcessorPagination, wubi_arrows_cross_pages_without_wrapping) {
    verify_arrow_pagination_without_wrapping<cxxime::WubiProcessor>();
}

RUN_ALL_TESTS()
