// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

// Engine commit source tests: verify CommitSource is set correctly
// for different input paths (ASCII, intercept_key, pinyin, Enter).

#include "util/testutil.h"
#include <cxxime/engine.h>
#include <cxxime/context.h>
#include <cxxime/output_options.h>
#include <cxxime/key_event.h>
#include <cxxime/processor.h>

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
// PinyinProcessor commit source tests (no dictionary needed)
// ============================================================

TEST(EngineSource, pinyin_processor_enter_commits_raw) {
    // Enter with pinyin buffer but no candidates → committed_text = pinyin_buffer
    cxxime::Context ctx;
    ctx.pinyin_buffer = "zzz";  // invalid pinyin, no candidates

    cxxime::KeyEvent event;
    event.keycode = 0x0D;  // VK_RETURN
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "zzz");
    // Note: commit_source is set by Engine, not PinyinProcessor.
    // PinyinProcessor only sets committed_text.
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

RUN_ALL_TESTS()
