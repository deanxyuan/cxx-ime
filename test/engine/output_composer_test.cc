// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/key_event.h>
#include <cxxime/output_composer.h>
#include <cxxime/output_options.h>

#include "support/testutil.h"

// 辅助函数：构造 KeyEvent
static cxxime::KeyEvent make_key(uint32_t vk, bool shift = false, bool caps = false, bool up = false) {
    cxxime::KeyEvent e;
    e.keycode = vk;
    e.is_key_up = up;
    if (shift) e.set_shift();
    if (caps) e.set_caps_lock();
    return e;
}

// 辅助函数：构造 OutputOptions
static cxxime::OutputOptions make_opts(bool chinese = false, bool full = true, bool caps = false) {
    cxxime::OutputOptions o;
    o.chinese_mode = chinese;
    o.full_shape = full;
    o.caps_lock = caps;
    o.chinese_punct = chinese;
    return o;
}

TEST(OutputComposer, intercepts_only_unmodified_digits_in_english_full_shape) {
    struct Case {
        cxxime::KeyEvent key;
        cxxime::OutputOptions options;
        bool intercepted;
        const char* output;
    };
    const Case cases[] = {
        {make_key('1'), make_opts(false, true), true, u8"１"},
        {make_key('0'), make_opts(false, true), true, u8"０"},
        {make_key('1', false, true), make_opts(false, true, true), true, u8"１"},
        {make_key('1', true), make_opts(false, true), false, ""},
        {make_key('A'), make_opts(false, true), false, ""},
        {make_key(VK_SPACE), make_opts(false, true), false, ""},
        {make_key(VK_RETURN), make_opts(false, true), false, ""},
        {make_key('1'), make_opts(true, true), false, ""},
        {make_key('1'), make_opts(false, false), false, ""},
        {make_key('1', false, false, true), make_opts(false, true), false, ""},
    };

    for (const Case& item : cases) {
        std::string output;
        ASSERT_EQ(cxxime::OutputComposer::intercept_key(item.key, item.options, output),
                  item.intercepted);
        ASSERT_EQ(output, item.output);
    }
}

TEST(OutputComposer, transforms_only_raw_code_case) {
    struct Case {
        const char* input;
        cxxime::OutputOptions options;
        const char* output;
    };
    const Case cases[] = {
        {"abc", make_opts(false, false, true), "ABC"},
        {"ABC", make_opts(false, false, true), "abc"},
        {u8"ab你好cd", make_opts(false, true, true), u8"AB你好CD"},
        {u8"你好", make_opts(false, false, true), u8"你好"},
        {"123", make_opts(false, false, true), "123"},
        {"abc", make_opts(false, true), "abc"},
        {"123", make_opts(false, true), "123"},
        {" ", make_opts(false, true), " "},
        {u8"hi你好", make_opts(false, true), u8"hi你好"},
        {"a\r\nb", make_opts(false, true), "a\r\nb"},
        {".,;:!?()[]{}", make_opts(false, true), ".,;:!?()[]{}"},
        {"", make_opts(false, true), ""},
    };

    for (const Case& item : cases) {
        ASSERT_EQ(cxxime::OutputComposer::transform(item.input, item.options,
                                                   cxxime::CommitSource::kRawCode),
                  item.output);
    }
}

TEST(OutputComposer, preserves_non_raw_commit_sources) {
    struct Case {
        const char* input;
        cxxime::OutputOptions options;
        cxxime::CommitSource source;
    };
    const Case cases[] = {
        {"N", make_opts(false, false, true), cxxime::CommitSource::kRawCodePretransformed},
        {"WiFi", make_opts(false, true, true), cxxime::CommitSource::kCandidate},
        {u8"C++编程", make_opts(false, true, true), cxxime::CommitSource::kCandidate},
        {"nihaoSD", make_opts(false, false, true), cxxime::CommitSource::kRawCodePreserveCase},
        {u8"AB你好", make_opts(false, false, true), cxxime::CommitSource::kRawCodePreserveCase},
    };

    for (const Case& item : cases) {
        ASSERT_EQ(cxxime::OutputComposer::transform(item.input, item.options, item.source),
                  item.input);
    }
}

// ============================================================
// Punctuation test helpers
// ============================================================

static char g_punct_tmp[MAX_PATH] = {};
static std::string punct_tmp(const char* name) {
    return std::string(g_punct_tmp) + name;
}
static bool _init_punct_tmp = []() {
    GetTempPathA(MAX_PATH, g_punct_tmp);
    return true;
}();

static cxxime::PunctMapping make_punct_mapping() {
    cxxime::PunctMapping pm;
    // half_shape: used when chinese_punct=true
    // "。" = U+3002
    pm.half_shape["."] = {cxxime::PunctType::COMMIT, u8"。", {}, {}};
    // "，" = U+FF0C
    pm.half_shape[","] = {cxxime::PunctType::COMMIT, u8"，", {}, {}};
    // pair: U+2018 left, U+2019 right
    cxxime::PunctEntry single_quote;
    single_quote.type = cxxime::PunctType::PAIR;
    single_quote.commit = "";
    single_quote.pair = {u8"‘", u8"’"};
    single_quote.alternatives = {};
    pm.half_shape["'"] = single_quote;
    // pair: U+201C left, U+201D right
    cxxime::PunctEntry double_quote;
    double_quote.type = cxxime::PunctType::PAIR;
    double_quote.commit = "";
    double_quote.pair = {u8"“", u8"”"};
    double_quote.alternatives = {};
    pm.half_shape["\""] = double_quote;
    // alternatives: U+2014, U+2013, U+00B7
    pm.half_shape["-"] = {
        cxxime::PunctType::ALTERNATIVES, {}, {}, {u8"—", u8"–", u8"·"}};
    // "【" = U+3010
    pm.half_shape["["] = {cxxime::PunctType::COMMIT, u8"【", {}, {}};
    return pm;
}

// ============================================================
// handle_punctuation tests
// ============================================================

TEST(OutputComposer, punct_chinese_period) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct1.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    auto r = engine.process_key(make_key(0xBE), opts);  // VK_OEM_PERIOD '.'
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "。");  // 。

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, punct_chinese_comma) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct2.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    auto r = engine.process_key(make_key(0xBC), opts);  // VK_OEM_COMMA ','
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "，");  // ，

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, punct_pairs_and_alternatives_follow_exact_sequences) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct3.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    struct Case {
        uint32_t keycode;
        bool shift;
        std::vector<std::string> expected;
    };
    const std::vector<Case> cases = {
        {0xDE, false, {u8"‘", u8"’", u8"‘"}},
        {0xDE, true, {u8"“", u8"”", u8"“"}},
        {0xBD, false, {u8"—", u8"–", u8"·", u8"—"}},
    };
    for (const Case& item : cases) {
        cxxime::Engine engine;
        ASSERT_TRUE(engine.initialize(dp));
        engine.set_trace_enabled(false);
        for (const std::string& expected : item.expected) {
            ASSERT_EQ(engine.process_key(make_key(item.keycode, item.shift), opts),
                      cxxime::ProcessResult::COMMITTED);
            ASSERT_EQ(engine.get_commit_text(), expected);
        }
        engine.finalize();
    }

    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, punct_digit_separator_guard) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct6.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    // Simulate: last committed char was '3', pinyin buffer empty
    engine.context().last_committed_char = '3';

    auto r = engine.process_key(make_key(0xBE), opts);  // VK_OEM_PERIOD '.'
    ASSERT_EQ(r, cxxime::ProcessResult::REJECTED);  // Not intercepted (digit guard)

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, ctrl_period_toggles_punct) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct7.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    cxxime::KeyEvent e;
    e.keycode = 0xBE;  // VK_OEM_PERIOD
    e.is_key_up = false;
    e.set_ctrl();

    auto r = engine.process_key(e, opts);
    ASSERT_EQ(r, cxxime::ProcessResult::TOGGLE_PUNCT);  // Ctrl+. toggles punctuation

e.set_shift();
r = engine.process_key(e, opts);
ASSERT_EQ(r, cxxime::ProcessResult::REJECTED);

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, punct_key_up_not_intercepted) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct8.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    auto r = engine.process_key(make_key(0xBE, false, false, true), opts);  // key-up
    ASSERT_EQ(r, cxxime::ProcessResult::REJECTED);  // key-up not intercepted

    engine.finalize();
    DeleteFileA(dp.c_str());
}

// ============================================================
// handle_full_shape tests
// ============================================================

TEST(OutputComposer, full_shape_period) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct9.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = false;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    // Literal full-shape conversion handles the idle period.
    auto r = engine.process_key(make_key(0xBE), opts);  // VK_OEM_PERIOD '.'
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "．");  // ．

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, full_shape_letter) {
    // In the current engine flow, letters in Chinese mode are accepted by
    // PinyinProcessor, so handle_full_shape is not reached.
    // Verify to_full_width conversion logic directly.
    ASSERT_EQ(cxxime::OutputComposer::to_full_width('a'), "ａ");  // ａ
    ASSERT_EQ(cxxime::OutputComposer::to_full_width('z'), "ｚ");  // ｚ
    ASSERT_EQ(cxxime::OutputComposer::to_full_width('A'), "Ａ");  // Ａ
}

TEST(OutputComposer, full_shape_digit) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct11.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = false;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    // Not composing → processor rejects digit → handle_full_shape converts
    auto r = engine.process_key(make_key(0x31), opts);  // '1'
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "１");  // １

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, full_shape_idle_symbol_ignores_semantic_mapping) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct12.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    // Full-shape literal input takes priority over the semantic punctuation table.
    auto r = engine.process_key(make_key(0xDB), opts);  // VK_OEM_4 '['
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "［");  // ［

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, full_shape_ctrl_not_intercepted) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct13.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = false;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    cxxime::KeyEvent e;
    e.keycode = 0x41;  // 'A'
    e.is_key_up = false;
    e.set_ctrl();

    // Ctrl+letter belongs to the host even when full-shape output is enabled.
    auto r = engine.process_key(e, opts);
    ASSERT_EQ(r, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(engine.context().active_input().empty());
    ASSERT_TRUE(engine.context().committed_text.empty());

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, ascii_mode_modified_letters_pass_through) {
    std::string dp = punct_tmp("shortcut_ascii.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "test", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);
    engine.ascii_composer().set_ascii_mode(true);

    cxxime::KeyEvent event;
    event.keycode = 'C';
    event.set_ctrl();

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(engine.context().committed_text.empty());

    event.modifiers = 0;
    event.set_alt();
    result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(engine.context().committed_text.empty());

    engine.finalize();
    DeleteFileA(dp.c_str());
}

RUN_ALL_TESTS()
