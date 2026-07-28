// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdint>

#include <windows.h>

#include <cxxime/context.h>
#include <cxxime/processor.h>
#include <cxxime/wubi_processor.h>

#include "util/testutil.h"

namespace {

void prepare_context(cxxime::Context& context) {
    context.pinyin_buffer = "ni";
    cxxime::Candidate candidate;
    candidate.text = "candidate";
    context.candidates.candidates.push_back(candidate);
    context.candidates.total_count = 27;
    context.candidates.page_size = 9;
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
    ASSERT_EQ(context.page_index, 1);

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_MINUS), context),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.page_index, 0);

    ASSERT_EQ(processor.process_key(make_key(VK_OEM_PLUS, 0x01), context),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(processor.process_key(make_key(VK_OEM_MINUS, 0x02), context),
              cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(context.page_index, 0);
}

} // namespace

TEST(ProcessorPagination, pinyin_supports_minus_and_equal) {
    verify_oem_pagination<cxxime::PinyinProcessor>();
}

TEST(ProcessorPagination, wubi_supports_minus_and_equal) {
    verify_oem_pagination<cxxime::WubiProcessor>();
}

RUN_ALL_TESTS()
