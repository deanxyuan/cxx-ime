// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cstdio>
#include <cstring>
#include <windows.h>
#include <cxxime/engine.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name()
#define ASSERT(cond)                                                                           \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
            tests_failed++;                                                                    \
            return;                                                                            \
        }                                                                                      \
    } while (0)
#define RUN_TEST(name)                                                                         \
    do {                                                                                       \
        printf("  %s...", #name);                                                              \
        name();                                                                                \
        printf(" OK\n");                                                                       \
        tests_passed++;                                                                        \
    } while (0)

TEST(test_engine_init) {
    cxxime::Engine engine;
    ASSERT(true);
}

TEST(test_process_letter_key) {
    cxxime::Engine engine;
    cxxime::Context ctx;

    cxxime::KeyEvent event;
    event.keycode = 'N';
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT(result == cxxime::ProcessResult::ACCEPTED);
    ASSERT(ctx.pinyin_buffer == "n");
}

TEST(test_process_escape) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "nihao";

    cxxime::KeyEvent event;
    event.keycode = VK_ESCAPE;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT(result == cxxime::ProcessResult::ACCEPTED);
    ASSERT(ctx.pinyin_buffer.empty());
}

TEST(test_process_backspace) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "ni";

    cxxime::KeyEvent event;
    event.keycode = VK_BACK;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT(result == cxxime::ProcessResult::ACCEPTED);
    ASSERT(ctx.pinyin_buffer == "n");
}

int run_engine_tests() {
    printf("Engine tests:\n");
    tests_passed = 0;
    tests_failed = 0;
    RUN_TEST(test_engine_init);
    RUN_TEST(test_process_letter_key);
    RUN_TEST(test_process_escape);
    RUN_TEST(test_process_backspace);
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
