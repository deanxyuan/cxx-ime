// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cxxime/output_composer.h>
#include <cxxime/output_options.h>
#include <cxxime/key_event.h>
#include <cxxime/engine.h>
#include <cxxime/dict.h>
#include <windows.h>

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

TEST(OutputComposer, transform_raw_code_pretransformed) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("N", opts, cxxime::CommitSource::kRawCodePretransformed);
    ASSERT_EQ(r, "N");
}

// ============================================================
// intercept_key 测试
// ============================================================

TEST(OutputComposer, intercept_digit_english_fullwidth) {
    // 英文 + 全角 + 数字键 → 拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31), make_opts(false, true), out);
    ASSERT_TRUE(r);
    ASSERT_EQ(out, "１");
}

TEST(OutputComposer, intercept_digit_0) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x30), make_opts(false, true), out);
    ASSERT_TRUE(r);
    ASSERT_EQ(out, "０");
}

TEST(OutputComposer, no_intercept_shift_digit) {
    // Shift+数字 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31, true), make_opts(false, true), out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_letter) {
    // 字母 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x41), make_opts(false, true), out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_space) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x20), make_opts(false, true), out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_enter) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x0D), make_opts(false, true), out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_chinese_mode) {
    // 中文模式 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31), make_opts(true, true), out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_not_fullwidth) {
    // 非全角 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31), make_opts(false, false), out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_key_up) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31, false, false, true), make_opts(false, true), out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, intercept_digit_with_capslock) {
    // 全角 + CapsLock + 数字 → 拦截（数字不受 CapsLock 影响）
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31, false, true), make_opts(false, true, true), out);
    ASSERT_TRUE(r);
    ASSERT_EQ(out, "１");
}

// ============================================================
// transform 测试
// ============================================================

TEST(OutputComposer, transform_fullwidth_letters) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "abc");
}

TEST(OutputComposer, transform_fullwidth_digits) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("123", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "123");
}

TEST(OutputComposer, transform_fullwidth_space) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform(" ", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, " ");
}

TEST(OutputComposer, transform_fullwidth_chinese_unaffected) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("你好", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "你好");
}

TEST(OutputComposer, transform_fullwidth_mixed) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("hi你好", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "hi你好");
}

TEST(OutputComposer, transform_capslock_lowercase) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "ABC");
}

TEST(OutputComposer, transform_capslock_uppercase) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("ABC", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "abc");
}

TEST(OutputComposer, transform_capslock_chinese_unaffected) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("你好", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "你好");
}

TEST(OutputComposer, transform_capslock_mixed) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("ab你好", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "AB你好");
}

TEST(OutputComposer, transform_capslock_digits_unaffected) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("123", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "123");
}

TEST(OutputComposer, transform_candidate_no_conversion) {
    // kCandidate → 不转换
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("WiFi", opts, cxxime::CommitSource::kCandidate);
    ASSERT_EQ(r, "WiFi");
}

TEST(OutputComposer, transform_candidate_chinese_mixed) {
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("C++编程", opts, cxxime::CommitSource::kCandidate);
    ASSERT_EQ(r, "C++编程");
}

TEST(OutputComposer, transform_fullwidth_capslock_combined) {
    // CapsLock 反转大小写，全角不再由 transform 处理
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "ABC");
}

TEST(OutputComposer, transform_fullwidth_capslock_mixed) {
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("ab你好cd", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "AB你好CD");
}

TEST(OutputComposer, transform_control_chars_preserved) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("a\r\nb", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "a\r\nb");
}

TEST(OutputComposer, transform_empty_string) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "");
}

TEST(OutputComposer, transform_no_conversion) {
    // 无 full_shape 无 caps_lock → 原样
    cxxime::OutputOptions opts = make_opts(false, false, false);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode);
    ASSERT_EQ(r, "abc");
}

TEST(OutputComposer, transform_fullwidth_punctuation) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform(".,;:!?()[]{}", opts, cxxime::CommitSource::kRawCode);
    // transform 不再做全角转换，应保持 ASCII 标点
    ASSERT_EQ(r, ".,;:!?()[]{}");
}

TEST(OutputComposer, transform_raw_code_preserve_case) {
    // kRawCodePreserveCase: caps_lock=true 但不做大小写反转
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("nihaoSD", opts, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_EQ(r, "nihaoSD");
}

TEST(OutputComposer, transform_raw_code_preserve_case_mixed) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("AB你好", opts, cxxime::CommitSource::kRawCodePreserveCase);
    ASSERT_EQ(r, "AB你好");
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
    pm.half_shape["."] = {cxxime::PunctType::COMMIT, "\xe3\x80\x82", {}, {}};
    // "，" = U+FF0C
    pm.half_shape[","] = {cxxime::PunctType::COMMIT, "\xef\xbc\x8c", {}, {}};
    // pair: U+2018 left, U+2019 right
    cxxime::PunctEntry single_quote;
    single_quote.type = cxxime::PunctType::PAIR;
    single_quote.commit = "";
    single_quote.pair = {"\xe2\x80\x98", "\xe2\x80\x99"};
    single_quote.alternatives = {};
    pm.half_shape["'"] = single_quote;
    // pair: U+201C left, U+201D right
    cxxime::PunctEntry double_quote;
    double_quote.type = cxxime::PunctType::PAIR;
    double_quote.commit = "";
    double_quote.pair = {"\xe2\x80\x9c", "\xe2\x80\x9d"};
    double_quote.alternatives = {};
    pm.half_shape["\""] = double_quote;
    // alternatives: U+2014, U+2013, U+00B7
    pm.half_shape["-"] = {cxxime::PunctType::ALTERNATIVES, {}, {}, {"\xe2\x80\x94", "\xe2\x80\x93", "\xc2\xb7"}};
    // "【" = U+3010
    pm.half_shape["["] = {cxxime::PunctType::COMMIT, "\xe3\x80\x90", {}, {}};
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

TEST(OutputComposer, punct_chinese_pair_single_quote) {
    auto pm = make_punct_mapping();

    // Verify mapping is correct
    auto it = pm.half_shape.find("'");
    ASSERT_TRUE(it != pm.half_shape.end()) << "Single quote not found in mapping";
    ASSERT_EQ(it->second.type, cxxime::PunctType::PAIR);
    ASSERT_TRUE(it->second.commit.empty()) << "commit should be empty for PAIR type";
    ASSERT_EQ(it->second.pair.size(), 2u) << "pair should have 2 elements";

    std::string dp = punct_tmp("punct3.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    // First press: should produce some punctuation (not rejected)
    auto r1 = engine.process_key(make_key(0xDE), opts);  // VK_OEM_7, no shift
    ASSERT_EQ(r1, cxxime::ProcessResult::COMMITTED);
    auto text1 = engine.context().committed_text;
    ASSERT_TRUE(!text1.empty()) << "First press should commit punctuation";

    // Second press: pair alternation should produce something
    auto r2 = engine.process_key(make_key(0xDE), opts);
    ASSERT_EQ(r2, cxxime::ProcessResult::COMMITTED);
    auto text2 = engine.context().committed_text;
    ASSERT_TRUE(!text2.empty()) << "Second press should commit punctuation";

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, punct_chinese_pair_double_quote) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct4.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    // First press Shift+' → should produce punctuation
    auto r1 = engine.process_key(make_key(0xDE, true), opts);  // VK_OEM_7 + shift
    ASSERT_EQ(r1, cxxime::ProcessResult::COMMITTED);
    auto text1 = engine.context().committed_text;
    ASSERT_TRUE(!text1.empty()) << "First press should commit punctuation";

    // Second press Shift+' → should produce punctuation
    auto r2 = engine.process_key(make_key(0xDE, true), opts);
    ASSERT_EQ(r2, cxxime::ProcessResult::COMMITTED);
    auto text2 = engine.context().committed_text;
    ASSERT_TRUE(!text2.empty()) << "Second press should commit punctuation";

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(OutputComposer, punct_chinese_alternatives) {
    auto pm = make_punct_mapping();
    std::string dp = punct_tmp("punct5.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    // VK_OEM_MINUS '-' → alternatives: U+2014 U+2013 U+00B7
    auto r1 = engine.process_key(make_key(0xBD), opts);
    ASSERT_EQ(r1, cxxime::ProcessResult::COMMITTED);
    ASSERT_TRUE(!engine.context().committed_text.empty());

    auto r2 = engine.process_key(make_key(0xBD), opts);
    ASSERT_EQ(r2, cxxime::ProcessResult::COMMITTED);
    ASSERT_TRUE(!engine.context().committed_text.empty());

    auto r3 = engine.process_key(make_key(0xBD), opts);
    ASSERT_EQ(r3, cxxime::ProcessResult::COMMITTED);
    ASSERT_TRUE(!engine.context().committed_text.empty());

    auto r4 = engine.process_key(make_key(0xBD), opts);
    ASSERT_EQ(r4, cxxime::ProcessResult::COMMITTED);
    ASSERT_TRUE(!engine.context().committed_text.empty());

    engine.finalize();
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
    ASSERT_TRUE(engine.context().pinyin_buffer.empty());
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
