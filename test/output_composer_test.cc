// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cxxime/output_composer.h>
#include <cxxime/output_options.h>
#include <cxxime/key_event.h>

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

// ============================================================
// intercept_key 测试
// ============================================================

TEST(OutputComposer, intercept_digit_english_fullwidth) {
    // 英文 + 全角 + 数字键 → 拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31), make_opts(false, true), false, out);
    ASSERT_TRUE(r);
    ASSERT_EQ(out, "１");
}

TEST(OutputComposer, intercept_digit_0) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x30), make_opts(false, true), false, out);
    ASSERT_TRUE(r);
    ASSERT_EQ(out, "０");
}

TEST(OutputComposer, no_intercept_shift_digit) {
    // Shift+数字 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31, true), make_opts(false, true), false, out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_letter) {
    // 字母 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x41), make_opts(false, true), false, out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_space) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x20), make_opts(false, true), false, out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_enter) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x0D), make_opts(false, true), false, out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_chinese_mode) {
    // 中文模式 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31), make_opts(true, true), false, out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_not_fullwidth) {
    // 非全角 → 不拦截
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31), make_opts(false, false), false, out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, no_intercept_key_up) {
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31, false, false, true), make_opts(false, true), false, out);
    ASSERT_TRUE(!r);
}

TEST(OutputComposer, intercept_digit_with_capslock) {
    // 全角 + CapsLock + 数字 → 拦截（数字不受 CapsLock 影响）
    std::string out;
    auto r = cxxime::OutputComposer::intercept_key(make_key(0x31, false, true), make_opts(false, true, true), false, out);
    ASSERT_TRUE(r);
    ASSERT_EQ(out, "１");
}

// ============================================================
// transform 测试
// ============================================================

TEST(OutputComposer, transform_fullwidth_letters) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "ａｂｃ");
}

TEST(OutputComposer, transform_fullwidth_digits) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("123", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "１２３");
}

TEST(OutputComposer, transform_fullwidth_space) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform(" ", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "　");
}

TEST(OutputComposer, transform_fullwidth_chinese_unaffected) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("你好", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "你好");
}

TEST(OutputComposer, transform_fullwidth_mixed) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("hi你好", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "ｈｉ你好");
}

TEST(OutputComposer, transform_capslock_lowercase) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "ABC");
}

TEST(OutputComposer, transform_capslock_uppercase) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("ABC", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "abc");
}

TEST(OutputComposer, transform_capslock_chinese_unaffected) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("你好", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "你好");
}

TEST(OutputComposer, transform_capslock_mixed) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("ab你好", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "AB你好");
}

TEST(OutputComposer, transform_capslock_digits_unaffected) {
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("123", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "123");
}

TEST(OutputComposer, transform_capslock_good_old) {
    // good_old_caps_lock=true → 不反转
    cxxime::OutputOptions opts = make_opts(false, false, true);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode, true);
    ASSERT_EQ(r, "abc");
}

TEST(OutputComposer, transform_candidate_no_conversion) {
    // kCandidate → 不转换
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("WiFi", opts, cxxime::CommitSource::kCandidate, false);
    ASSERT_EQ(r, "WiFi");
}

TEST(OutputComposer, transform_candidate_chinese_mixed) {
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("C++编程", opts, cxxime::CommitSource::kCandidate, false);
    ASSERT_EQ(r, "C++编程");
}

TEST(OutputComposer, transform_fullwidth_capslock_combined) {
    // 全角 + CapsLock → 先反转再全角
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "ＡＢＣ");
}

TEST(OutputComposer, transform_fullwidth_capslock_mixed) {
    cxxime::OutputOptions opts = make_opts(false, true, true);
    auto r = cxxime::OutputComposer::transform("ab你好cd", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "ＡＢ你好ＣＤ");
}

TEST(OutputComposer, transform_control_chars_preserved) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("a\r\nb", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "ａ\r\nｂ");
}

TEST(OutputComposer, transform_empty_string) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform("", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "");
}

TEST(OutputComposer, transform_no_conversion) {
    // 无 full_shape 无 caps_lock → 原样
    cxxime::OutputOptions opts = make_opts(false, false, false);
    auto r = cxxime::OutputComposer::transform("abc", opts, cxxime::CommitSource::kRawCode, false);
    ASSERT_EQ(r, "abc");
}

TEST(OutputComposer, transform_fullwidth_punctuation) {
    cxxime::OutputOptions opts = make_opts(false, true);
    auto r = cxxime::OutputComposer::transform(".,;:!?()[]{}", opts, cxxime::CommitSource::kRawCode, false);
    // 每个 ASCII 标点应转为全角
    ASSERT_EQ(r, "．，；：！？（）［］｛｝");
}

RUN_ALL_TESTS()
