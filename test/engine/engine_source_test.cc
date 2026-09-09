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

#include "support/testutil.h"

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
    pm.half_shape["."] = {cxxime::PunctType::COMMIT, u8"。", {}, {}};
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

TEST(EngineSource, context_returns_committed_text_with_its_source) {
    const std::pair<const char*, cxxime::CommitSource> cases[] = {
        {"hello", cxxime::CommitSource::kRawCode},
        {u8"你好", cxxime::CommitSource::kCandidate},
    };
    for (const auto& item : cases) {
        cxxime::Context context;
        context.committed_text = item.first;
        context.set_commit_source(item.second);
        const auto committed = context.commit_with_source();
        ASSERT_EQ(committed.first, item.first);
        ASSERT_EQ(committed.second, item.second);
    }
}

TEST(EngineSource, commit_with_source_from_highlighted_candidate) {
    cxxime::Context ctx;
    ASSERT_TRUE(ctx.set_preedit("ni"));
    cxxime::CandidatePage page;
    page.candidates.push_back({"你", "", 100});
    page.candidates.push_back({"呢", "", 80});
    page.highlighted = 0;
    ctx.update_candidates(std::move(page));
    // committed_text is empty, so commit_with_source falls back to highlighted candidate

    auto [text, source] = ctx.commit_with_source();
    ASSERT_EQ(text, "你");
    ASSERT_EQ(source, cxxime::CommitSource::kCandidate);
}

TEST(EngineSource, commit_with_source_from_active_input) {
    cxxime::Context ctx;
    ASSERT_TRUE(ctx.set_preedit("nihao"));
    // No committed text or candidates falls back to the active input.

    auto [text, source] = ctx.commit_with_source();
    ASSERT_EQ(text, "nihao");
    ASSERT_EQ(source, cxxime::CommitSource::kRawCode);
}

// ============================================================
// Processor and Engine commit source tests (no dictionary needed)
// ============================================================

TEST(EngineSource, engine_enter_commits_raw_with_preserved_case) {
    cxxime::Engine engine;
    ASSERT_TRUE(
        engine.context().start_composition(cxxime::CompositionScheme::kPinyin, "ZzZ", 3));

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
    ASSERT_TRUE(ctx.set_preedit("de"));
    cxxime::CandidatePage page;
    page.candidates.push_back({"的", "", 100});
    page.highlighted = 0;
    ctx.update_candidates(std::move(page));

    cxxime::KeyEvent event;
    event.keycode = 0x20;  // VK_SPACE
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::CANDIDATE_SELECTED);
    ASSERT_EQ(ctx.take_requested_candidate_selection().value_or(-1), 0);
}

TEST(EngineSource, pinyin_processor_digit_selects_candidate) {
    cxxime::Context ctx;
    ASSERT_TRUE(ctx.set_preedit("de"));
    cxxime::CandidatePage page;
    page.candidates.push_back({"的", "", 100});
    page.candidates.push_back({"地", "", 80});
    page.highlighted = 0;
    ctx.update_candidates(std::move(page));

    cxxime::KeyEvent event;
    event.keycode = '2';
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::CANDIDATE_SELECTED);
    ASSERT_EQ(ctx.take_requested_candidate_selection().value_or(-1), 1);
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

// ============================================================
// Punctuation integration tests
// ============================================================

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
    ASSERT_EQ(engine.context().active_input(), "");

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
    ASSERT_TRUE(!engine.context().candidate_page().candidates.empty());
    engine.context().translation().highlighted = -1;

    ASSERT_EQ(engine.process_key(make_punct_key(VK_OEM_PERIOD), opts),
              cxxime::ProcessResult::COMMITTED);
    const auto committed = engine.take_commit_text_with_source();
    ASSERT_EQ(committed.first, u8"first。");
    ASSERT_EQ(committed.second, cxxime::CommitSource::kCandidate);

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
    ASSERT_TRUE(!engine.context().candidate_page().candidates.empty());

    ASSERT_EQ(engine.process_key(make_punct_key(VK_DIVIDE), opts),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().active_input(), "ni/");
    ASSERT_EQ(engine.context().composition_scheme(), cxxime::CompositionScheme::kInlineAscii);
    ASSERT_TRUE(engine.context().candidate_page().candidates.empty());
    ASSERT_TRUE(engine.context().committed_text.empty());

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
    ASSERT_EQ(text1, u8"ａ");

    // Letter 'Z' without Shift → lowercase 'z' → full-width ｚ (U+FF5A)
    auto result2 = engine.process_key(make_punct_key('Z'), opts);
    ASSERT_EQ(result2, cxxime::ProcessResult::COMMITTED);
    auto [text2, src2] = engine.take_commit_text_with_source();
    ASSERT_EQ(text2, u8"ｚ");

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
    ASSERT_EQ(text, u8"　");

    engine.finalize();
    DeleteFileA(dp.c_str());
}

RUN_ALL_TESTS()
