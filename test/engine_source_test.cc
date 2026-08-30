// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

// Engine commit source tests: verify CommitSource is set correctly
// for different input paths (ASCII, intercept_key, pinyin, Enter).

#include <string>

#include <windows.h>

#include <cxxime/context.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/key_event.h>
#include <cxxime/output_options.h>
#include <cxxime/processor.h>
#include <cxxime/punct_types.h>

#include "util/testutil.h"

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

static cxxime::PunctMapping make_test_punct_mapping() {
    cxxime::PunctMapping pm;
    // half_shape: used when chinese_punct=true
    pm.half_shape["."] = {cxxime::PunctType::COMMIT, "\xe3\x80\x82", {}, {}};  // 。
    pm.half_shape[","] = {cxxime::PunctType::COMMIT, "\xef\xbc\x8c", {}, {}};  // ，
    pm.half_shape["'"] = {cxxime::PunctType::PAIR, {}, {"\xe2\x80\x98", "\xe2\x80\x99"}, {}};  // ' '
    pm.half_shape["\""] = {cxxime::PunctType::PAIR, {}, {"\xe2\x80\x9c", "\xe2\x80\x9d"}, {}};  // " "
    pm.half_shape["-"] = {cxxime::PunctType::ALTERNATIVES, {}, {}, {"\xe2\x80\x94", "\xe2\x80\x93", "\xc2\xb7"}};  // — – ·
    // full_shape: used when chinese_punct=false && full_shape=true
    pm.full_shape["["] = {cxxime::PunctType::COMMIT, "\xe3\x80\x90", {}, {}};  // 【
    return pm;
}

static cxxime::KeyEvent make_punct_key(uint32_t vk) {
    cxxime::KeyEvent event;
    event.keycode = vk;
    event.is_key_up = false;
    return event;
}

// ============================================================
// Context commit source tests (no dictionary needed)
// ============================================================

TEST(EngineSource, take_returns_committed_text_and_source) {
    cxxime::Context ctx;
    ctx.committed_text = "hello";
    ctx.set_commit_source(cxxime::CommitSource::kRawCode);

    auto [text, source] = ctx.commit_with_source();
    ASSERT_EQ(text, "hello");
    ASSERT_EQ(source, cxxime::CommitSource::kRawCode);
}

TEST(EngineSource, take_candidate_source) {
    cxxime::Context ctx;
    ctx.committed_text = "你好";
    ctx.set_commit_source(cxxime::CommitSource::kCandidate);

    auto [text, source] = ctx.commit_with_source();
    ASSERT_EQ(text, "你好");
    ASSERT_EQ(source, cxxime::CommitSource::kCandidate);
}

TEST(EngineSource, commit_with_source_from_highlighted_candidate) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "ni";
    ctx.candidates.candidates.push_back({"你", "", 100});
    ctx.candidates.candidates.push_back({"呢", "", 80});
    ctx.candidates.highlighted = 0;
    // committed_text is empty, so commit_with_source falls back to highlighted candidate

    auto [text, source] = ctx.commit_with_source();
    ASSERT_EQ(text, "你");
    ASSERT_EQ(source, cxxime::CommitSource::kCandidate);
}

TEST(EngineSource, commit_with_source_from_pinyin_buffer) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "nihao";
    // No committed_text, no candidates → falls back to pinyin_buffer

    auto [text, source] = ctx.commit_with_source();
    ASSERT_EQ(text, "nihao");
    ASSERT_EQ(source, cxxime::CommitSource::kRawCode);
}

TEST(EngineSource, reset_clears_commit_source) {
    cxxime::Context ctx;
    ctx.set_commit_source(cxxime::CommitSource::kCandidate);
    ctx.committed_text = "test";

    ctx.reset();
    // After reset, commit_source should be kRawCode
    ASSERT_EQ(ctx.commit_source(), cxxime::CommitSource::kRawCode);
}

TEST(EngineSource, commit_clears_commit_source) {
    cxxime::Context ctx;
    ctx.committed_text = "test";
    ctx.set_commit_source(cxxime::CommitSource::kCandidate);

    ctx.commit();
    // After commit, commit_source should be reset to kRawCode
    ASSERT_EQ(ctx.commit_source(), cxxime::CommitSource::kRawCode);
}

TEST(EngineSource, default_commit_source_is_kRawCode) {
    cxxime::Context ctx;
    ASSERT_EQ(ctx.commit_source(), cxxime::CommitSource::kRawCode);
}

// ============================================================
// Processor and Engine commit source tests (no dictionary needed)
// ============================================================

TEST(EngineSource, engine_enter_commits_raw_with_preserved_case) {
    cxxime::Engine engine;
    ASSERT_TRUE(engine.context().start_composition(cxxime::CompositionKind::kIme, "ZzZ", 3));

    cxxime::KeyEvent event;
    event.keycode = 0x0D;  // VK_RETURN
    event.is_key_up = false;

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    auto committed = engine.take_commit_text_with_source();
    ASSERT_EQ(committed.first, "ZzZ");
    ASSERT_EQ(committed.second, cxxime::CommitSource::kRawCodePreserveCase);
}

TEST(EngineSource, pinyin_processor_space_selects_candidate) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "de";
    ctx.candidates.candidates.push_back({"的", "", 100});
    ctx.candidates.highlighted = 0;

    cxxime::KeyEvent event;
    event.keycode = 0x20;  // VK_SPACE
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "的");
}

TEST(EngineSource, pinyin_processor_digit_selects_candidate) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "de";
    ctx.candidates.candidates.push_back({"的", "", 100});
    ctx.candidates.candidates.push_back({"地", "", 80});
    ctx.candidates.highlighted = 0;

    cxxime::KeyEvent event;
    event.keycode = '2';
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "地");
}

// ============================================================
// Engine::take_commit_text_with_source tests
// ============================================================

TEST(EngineSource, engine_take_clears_committed_text) {
    cxxime::Engine engine;

    // Directly set context state (bypassing process_key)
    engine.context().committed_text = "test";
    engine.context().set_commit_source(cxxime::CommitSource::kRawCode);

    auto [text, source] = engine.take_commit_text_with_source();
    ASSERT_EQ(text, "test");
    ASSERT_EQ(source, cxxime::CommitSource::kRawCode);

    // Second call should return empty
    auto [text2, source2] = engine.take_commit_text_with_source();
    ASSERT_EQ(text2, "");
}

TEST(EngineSource, engine_take_preserves_source) {
    cxxime::Engine engine;

    engine.context().committed_text = "WiFi";
    engine.context().set_commit_source(cxxime::CommitSource::kCandidate);

    auto [text, source] = engine.take_commit_text_with_source();
    ASSERT_EQ(text, "WiFi");
    ASSERT_EQ(source, cxxime::CommitSource::kCandidate);
}

// ============================================================
// Punctuation integration tests
// ============================================================

TEST(EngineSource, punctuation_chinese_period) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_punct1.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    auto result = engine.process_key(make_punct_key(0xBE), opts);  // VK_OEM_PERIOD
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    auto [text, source] = engine.take_commit_text_with_source();
    ASSERT_EQ(text, "\xe3\x80\x82");  // 。
    ASSERT_EQ(source, cxxime::CommitSource::kRawCodePretransformed);

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, punctuation_with_composing) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_punct2.bin");
    cxxime::Dict::create_test_dict(dp, {{"ni", "你", 100}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    // Type "ni" to get candidates
    engine.process_key(make_punct_key('N'), opts);
    engine.process_key(make_punct_key('I'), opts);

    // Now press period - should commit candidate + punctuation
    auto result = engine.process_key(make_punct_key(0xBE), opts);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    auto [text, source] = engine.take_commit_text_with_source();
    // Should be candidate text + punctuation
    ASSERT_TRUE(!text.empty());
    ASSERT_EQ(source, cxxime::CommitSource::kCandidate);
    ASSERT_EQ(engine.context().pinyin_buffer, "");

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, punctuation_falls_back_to_first_candidate_when_highlight_is_invalid) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_punct_invalid_highlight.bin");
    cxxime::Dict::create_test_dict(dp, {{"ni", "first", 100}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    engine.process_key(make_punct_key('N'), opts);
    engine.process_key(make_punct_key('I'), opts);
    ASSERT_TRUE(!engine.context().candidates.candidates.empty());
    engine.context().candidates.highlighted = -1;

    ASSERT_EQ(engine.process_key(make_punct_key(VK_OEM_PERIOD), opts),
              cxxime::ProcessResult::COMMITTED);
    const auto committed = engine.take_commit_text_with_source();
    ASSERT_EQ(committed.first, "first\xe3\x80\x82");
    ASSERT_EQ(committed.second, cxxime::CommitSource::kCandidate);

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, punctuation_pair_alternation) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_punct3.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    // First press: left quote
    engine.process_key(make_punct_key(0xDE), opts);  // VK_OEM_7 (')
    auto [text1, _1] = engine.take_commit_text_with_source();
    ASSERT_EQ(text1, "\xe2\x80\x98");  // '

    // Second press: right quote
    engine.process_key(make_punct_key(0xDE), opts);
    auto [text2, _2] = engine.take_commit_text_with_source();
    ASSERT_EQ(text2, "\xe2\x80\x99");  // '

    // Third press: left quote again
    engine.process_key(make_punct_key(0xDE), opts);
    auto [text3, _3] = engine.take_commit_text_with_source();
    ASSERT_EQ(text3, "\xe2\x80\x98");  // '

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, punctuation_alternatives_cycle) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_punct4.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.punct_mapping = &pm;

    // Cycle through alternatives
    engine.process_key(make_punct_key(0xBD), opts);  // VK_OEM_MINUS (-)
    auto [t1, _1] = engine.take_commit_text_with_source();
    ASSERT_EQ(t1, "\xe2\x80\x94");  // —

    engine.process_key(make_punct_key(0xBD), opts);
    auto [t2, _2] = engine.take_commit_text_with_source();
    ASSERT_EQ(t2, "\xe2\x80\x93");  // –

    engine.process_key(make_punct_key(0xBD), opts);
    auto [t3, _3] = engine.take_commit_text_with_source();
    ASSERT_EQ(t3, "\xc2\xb7");  // ·

    // Should cycle back
    engine.process_key(make_punct_key(0xBD), opts);
    auto [t4, _4] = engine.take_commit_text_with_source();
    ASSERT_EQ(t4, "\xe2\x80\x94");  // —

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, full_shape_period) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_punct5.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = false;
    opts.chinese_punct = false;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    auto result = engine.process_key(make_punct_key(0xBE), opts);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    auto [text, source] = engine.take_commit_text_with_source();
    ASSERT_EQ(text, "\xef\xbc\x8e");  // ．

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, numpad_text_stays_ascii_in_full_shape_chinese_mode) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_numpad_ascii.bin");
    cxxime::Dict::create_test_dict(dp, {{"ni", "你", 100}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    const struct {
        uint32_t key;
        const char* expected;
    } cases[] = {
        {VK_NUMPAD2, "2"},
        {VK_DIVIDE, "/"},
        {VK_MULTIPLY, "*"},
        {VK_SUBTRACT, "-"},
        {VK_ADD, "+"},
        {VK_DECIMAL, "."},
    };
    for (const auto& item : cases) {
        ASSERT_EQ(engine.process_key(make_punct_key(item.key), opts),
                  cxxime::ProcessResult::COMMITTED);
        const auto committed = engine.take_commit_text_with_source();
        ASSERT_EQ(committed.first, item.expected);
        ASSERT_EQ(committed.second, cxxime::CommitSource::kRawCodePretransformed);
    }

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, numpad_text_enters_inline_ascii_before_full_shape_conversion) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_numpad_inline_ascii.bin");
    cxxime::Dict::create_test_dict(dp, {{"ni", "你", 100}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = true;
    opts.chinese_punct = true;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    engine.process_key(make_punct_key('N'), opts);
    engine.process_key(make_punct_key('I'), opts);
    ASSERT_TRUE(!engine.context().candidates.candidates.empty());

    ASSERT_EQ(engine.process_key(make_punct_key(VK_DIVIDE), opts),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "ni/");
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    ASSERT_TRUE(engine.context().candidates.candidates.empty());
    ASSERT_TRUE(engine.context().committed_text.empty());

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, full_shape_letter_in_english_mode) {
    std::string dp = punct_tmp("es_punct6.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    // In English mode + full_shape, letter keys should be intercepted by handle_full_shape
    cxxime::OutputOptions opts;
    opts.chinese_mode = false;
    opts.chinese_punct = false;
    opts.full_shape = true;

    // Note: In Chinese mode, letter keys are handled by PinyinProcessor (as pinyin input),
    // not by handle_full_shape. This test verifies English mode behavior.
    auto result = engine.process_key(make_punct_key('A'), opts);
    // In English mode without ascii_mode set, the letter goes through PinyinProcessor first
    // which returns ACCEPTED. handle_full_shape is only called when result is REJECTED.
    // So the result depends on whether ascii_mode is active.
    // Since ascii_mode defaults to false, PinyinProcessor handles it.
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, full_shape_custom_mapping) {
    auto pm = make_test_punct_mapping();
    std::string dp = punct_tmp("es_punct7.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = false;
    opts.chinese_punct = false;
    opts.full_shape = true;
    opts.punct_mapping = &pm;

    auto result = engine.process_key(make_punct_key(0xDB), opts);  // VK_OEM_4 ([)
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    auto [text, source] = engine.take_commit_text_with_source();
    ASSERT_EQ(text, "\xe3\x80\x90");  // 【

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, english_half_shape_no_intercept) {
    std::string dp = punct_tmp("es_punct8.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);

    cxxime::OutputOptions opts;
    opts.chinese_mode = false;
    opts.full_shape = false;
    opts.chinese_punct = false;

    auto result = engine.process_key(make_punct_key(0xBE), opts);
    ASSERT_EQ(result, cxxime::ProcessResult::REJECTED);

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, ascii_mode_full_shape_letter) {
    std::string dp = punct_tmp("es_punct7.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);
    engine.ascii_composer().set_ascii_mode(true);

    cxxime::OutputOptions opts;
    opts.chinese_mode = false;
    opts.chinese_punct = false;
    opts.full_shape = true;

    // Letter 'A' without Shift → lowercase 'a' → full-width ａ (U+FF41)
    auto result = engine.process_key(make_punct_key('A'), opts);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    auto [text1, src1] = engine.take_commit_text_with_source();
    ASSERT_EQ(text1, "\xef\xbd\x81");  // U+FF41

    // Letter 'Z' without Shift → lowercase 'z' → full-width ｚ (U+FF5A)
    auto result2 = engine.process_key(make_punct_key('Z'), opts);
    ASSERT_EQ(result2, cxxime::ProcessResult::COMMITTED);
    auto [text2, src2] = engine.take_commit_text_with_source();
    ASSERT_EQ(text2, "\xef\xbd\x9a");  // U+FF5A

    engine.finalize();
    DeleteFileA(dp.c_str());
}

TEST(EngineSource, ascii_mode_full_shape_space) {
    std::string dp = punct_tmp("es_punct8.bin");
    cxxime::Dict::create_test_dict(dp, {{"de", "的", 1}});
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dp));
    engine.set_trace_enabled(false);
    engine.ascii_composer().set_ascii_mode(true);

    cxxime::OutputOptions opts;
    opts.chinese_mode = false;
    opts.chinese_punct = false;
    opts.full_shape = true;

    // Space should produce ideographic space U+3000
    cxxime::KeyEvent space_key;
    space_key.keycode = 0x20;  // VK_SPACE
    space_key.is_key_up = false;
    auto result = engine.process_key(space_key, opts);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    auto [text, src] = engine.take_commit_text_with_source();
    ASSERT_EQ(text, "\xe3\x80\x80");  // U+3000

    engine.finalize();
    DeleteFileA(dp.c_str());
}

RUN_ALL_TESTS()
