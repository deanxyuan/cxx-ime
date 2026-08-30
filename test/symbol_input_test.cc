// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdio>
#include <string>

#include <windows.h>

#include <cxxime/config.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/output_options.h>
#include <cxxime/spellings_index.h>
#include <cxxime/symbol_table.h>

#include "util/testutil.h"

namespace {

cxxime::KeyEvent make_key(uint32_t keycode, bool key_up = false) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    event.is_key_up = key_up;
    return event;
}

cxxime::KeyEvent make_shift_key(uint32_t keycode) {
    cxxime::KeyEvent event = make_key(keycode);
    event.set_shift();
    return event;
}

class SymbolEngineFixture {
public:
    bool initialize() {
        char temp_path[MAX_PATH] = {};
        if (GetTempPathA(MAX_PATH, temp_path) == 0) {
            return false;
        }
        dict_path_ = std::string(temp_path) + "cxxime_symbol_input.bin";
        user_path_ = std::string(temp_path) + "cxxime_symbol_input.tsv";
        DeleteFileA(dict_path_.c_str());
        DeleteFileA(user_path_.c_str());

        if (!cxxime::Dict::create_test_dict(
                dict_path_, {{"a", "first", 100}, {"a", "second", 90}}) ||
            !dict_.open(dict_path_, user_path_) ||
            !symbols_.load(std::string(CXXIME_DATA_DIR) + "symbols.json")) {
            return false;
        }

        config_.candidate_learning = true;
        config_.wubi_auto_commit = true;
        config_.wubi_commit_first_on_fifth_key = true;
        config_.ascii_switch_key["Shift_L"] = "code";
        if (!engine_.initialize(dict_, spellings_, nullptr, config_, &symbols_)) {
            return false;
        }
        engine_.set_trace_enabled(false);
        engine_.set_wubi_dict(&dict_);
        return true;
    }

    ~SymbolEngineFixture() {
        engine_.finalize();
        dict_.close();
        DeleteFileA(dict_path_.c_str());
        DeleteFileA(user_path_.c_str());
    }

    cxxime::Engine& engine() { return engine_; }

private:
    std::string dict_path_;
    std::string user_path_;
    cxxime::Dict dict_;
    cxxime::SpellingsIndex spellings_;
    cxxime::Config config_;
    cxxime::SymbolTable symbols_;
    cxxime::Engine engine_;
};

void type_symbol_code(cxxime::Engine& engine, const char* code,
                      const cxxime::OutputOptions& options = {}) {
    ASSERT_EQ(engine.process_key(make_key(VK_OEM_5), options), cxxime::ProcessResult::ACCEPTED);
    for (const char* ch = code; *ch; ++ch) {
        ASSERT_EQ(engine.process_key(make_key(static_cast<uint32_t>(*ch - 'a' + 'A')), options),
                  cxxime::ProcessResult::ACCEPTED);
    }
}

cxxime::PunctMapping make_enumeration_punctuation() {
    cxxime::PunctMapping punctuation;
    punctuation.half_shape["/"] = {cxxime::PunctType::COMMIT, "\xe3\x80\x81", {}, {}};
    punctuation.half_shape["\\"] = {cxxime::PunctType::COMMIT, "\xe3\x80\x81", {}, {}};
    return punctuation;
}

} // namespace

TEST(SymbolInput, works_in_all_input_modes) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    const cxxime::InputMode modes[] = {
        cxxime::InputMode::PINYIN,
        cxxime::InputMode::WUBI,
        cxxime::InputMode::MIXED,
    };
    for (cxxime::InputMode mode : modes) {
        fixture.engine().clear();
        fixture.engine().switch_mode(mode);
        type_symbol_code(fixture.engine(), "bd");
        ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\bd");
        ASSERT_EQ(fixture.engine().context().composition_kind(),
                  cxxime::CompositionKind::kSymbol);
        ASSERT_TRUE(!fixture.engine().context().candidates.candidates.empty());
        ASSERT_EQ(fixture.engine().context().candidates.candidates[0].source,
                  cxxime::CandidateSource::kSymbol);
    }
}

TEST(SymbolInput, enter_commits_raw_trigger) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "");

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_RETURN)), cxxime::ProcessResult::COMMITTED);
    const auto committed = fixture.engine().take_commit_text_with_source();
    ASSERT_EQ(committed.first, "\\");
    ASSERT_EQ(committed.second, cxxime::CommitSource::kRawCodePreserveCase);
}

TEST(SymbolInput, bare_trigger_navigates_categories_without_committing) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "");

    const auto& categories = fixture.engine().context().candidates;
    ASSERT_EQ(categories.total_count, 14);
    ASSERT_EQ(categories.candidates[0].text, "标点");
    ASSERT_EQ(categories.candidates[0].comment, "\\bd");

    ASSERT_EQ(fixture.engine().process_key(make_key('1')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\bd");
    ASSERT_TRUE(fixture.engine().context().committed_text.empty());
    ASSERT_EQ(fixture.engine().context().candidates.candidates[0].text, "。");

    fixture.engine().clear();
    type_symbol_code(fixture.engine(), "");
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_SPACE)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\bd");
}

TEST(SymbolInput, shift_commits_raw_trigger_and_switches_to_english) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "");

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_LSHIFT)), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_LSHIFT, true)),
              cxxime::ProcessResult::COMMITTED);
    const auto committed = fixture.engine().take_commit_text_with_source();
    ASSERT_EQ(committed.first, "\\");
    ASSERT_TRUE(fixture.engine().ascii_composer().is_ascii_mode());
}

TEST(SymbolInput, disabled_state_passes_trigger_to_application) {
    const uint32_t keys[] = {VK_OEM_2, VK_OEM_5};
    for (uint32_t key : keys) {
        SymbolEngineFixture fixture;
        ASSERT_TRUE(fixture.initialize());

        cxxime::OutputOptions options;
        options.chinese_punct = false;
        ASSERT_EQ(fixture.engine().process_key(make_key(key), options),
                  cxxime::ProcessResult::REJECTED);
        ASSERT_TRUE(!fixture.engine().context().is_composing());

        options.chinese_mode = false;
        options.chinese_punct = true;
        ASSERT_EQ(fixture.engine().process_key(make_key(key), options),
                  cxxime::ProcessResult::REJECTED);
        ASSERT_TRUE(!fixture.engine().context().is_composing());
    }
}

TEST(SymbolInput, slash_commits_enumeration_comma_while_idle) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const cxxime::PunctMapping punctuation = make_enumeration_punctuation();
    cxxime::OutputOptions options;
    options.punct_mapping = &punctuation;

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_OEM_2), options),
              cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine().take_commit_text_with_source().first, "\xe3\x80\x81");
    ASSERT_TRUE(!fixture.engine().context().is_composing());
}

TEST(SymbolInput, backslash_starts_symbol_input_before_idle_punctuation) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    const cxxime::PunctMapping punctuation = make_enumeration_punctuation();
    cxxime::OutputOptions options;
    options.punct_mapping = &punctuation;

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_OEM_5), options),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\");
    ASSERT_EQ(fixture.engine().context().composition_kind(), cxxime::CompositionKind::kSymbol);
    ASSERT_TRUE(!fixture.engine().context().candidates.candidates.empty());
}

TEST(SymbolInput, main_symbols_use_full_shape_while_idle) {
    const struct {
        uint32_t key;
        bool shift;
        const char* full_width;
    } cases[] = {
        {VK_OEM_PERIOD, false, "\xef\xbc\x8e"},
        {VK_OEM_2, false, "\xef\xbc\x8f"},
        {VK_OEM_5, false, "\xef\xbc\xbc"},
        {VK_OEM_MINUS, false, "\xef\xbc\x8d"},
        {VK_OEM_4, false, "\xef\xbc\xbb"},
        {VK_OEM_COMMA, true, "\xef\xbc\x9c"},
        {VK_OEM_7, false, "\xef\xbc\x87"},
        {VK_OEM_7, true, "\xef\xbc\x82"},
        {'1', true, "\xef\xbc\x81"},
    };
    for (const auto& test_case : cases) {
        SymbolEngineFixture fixture;
        ASSERT_TRUE(fixture.initialize());
        const cxxime::PunctMapping punctuation = make_enumeration_punctuation();
        cxxime::OutputOptions options;
        options.chinese_punct = true;
        options.full_shape = true;
        options.punct_mapping = &punctuation;
        const cxxime::KeyEvent event =
            test_case.shift ? make_shift_key(test_case.key) : make_key(test_case.key);

        ASSERT_EQ(fixture.engine().process_key(event, options),
                  cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(fixture.engine().take_commit_text_with_source().first, test_case.full_width);
        ASSERT_TRUE(!fixture.engine().context().is_composing());
    }
}

TEST(SymbolInput, candidate_pagination_keys_remain_commands_in_full_shape) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    cxxime::OutputOptions options;
    options.full_shape = true;

    ASSERT_EQ(fixture.engine().process_key(make_key('A'), options),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!fixture.engine().context().candidates.candidates.empty());
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_OEM_PLUS), options),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_OEM_MINUS), options),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "a");
    ASSERT_TRUE(fixture.engine().context().committed_text.empty());
}

TEST(SymbolInput, slash_and_backslash_commit_ime_candidate_with_enumeration_comma) {
    const uint32_t keys[] = {VK_OEM_2, VK_OEM_5};
    for (uint32_t key : keys) {
        SymbolEngineFixture fixture;
        ASSERT_TRUE(fixture.initialize());

        const cxxime::PunctMapping punctuation = make_enumeration_punctuation();
        cxxime::OutputOptions options;
        options.punct_mapping = &punctuation;

        ASSERT_EQ(fixture.engine().process_key(make_key('A'), options),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_GE(fixture.engine().context().candidates.candidates.size(), 2u);
        fixture.engine().context().candidates.highlighted = 1;
        const std::string expected =
            fixture.engine().context().candidates.candidates[1].text + "\xe3\x80\x81";
        ASSERT_EQ(fixture.engine().process_key(make_key(key), options),
                  cxxime::ProcessResult::COMMITTED);
        const auto committed = fixture.engine().take_commit_text_with_source();
        ASSERT_EQ(committed.first, expected);
        ASSERT_EQ(committed.second, cxxime::CommitSource::kCandidate);
    }
}

TEST(SymbolInput, slash_and_backslash_extend_ime_preedit_without_candidates) {
    const struct {
        uint32_t key;
        const char* expected;
    } cases[] = {
        {VK_OEM_2, "z/"},
        {VK_OEM_5, "z\\"},
    };
    for (const auto& test_case : cases) {
        SymbolEngineFixture fixture;
        ASSERT_TRUE(fixture.initialize());
        const cxxime::PunctMapping punctuation = make_enumeration_punctuation();
        cxxime::OutputOptions options;
        options.punct_mapping = &punctuation;

        ASSERT_EQ(fixture.engine().process_key(make_key('Z'), options),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(fixture.engine().context().candidates.candidates.empty());
        ASSERT_EQ(fixture.engine().process_key(make_key(test_case.key), options),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(fixture.engine().context().pinyin_buffer, test_case.expected);
        ASSERT_EQ(fixture.engine().context().composition_kind(),
                  cxxime::CompositionKind::kInlineAscii);
        ASSERT_TRUE(fixture.engine().context().committed_text.empty());
    }
}

TEST(SymbolInput, slash_and_backslash_preserve_full_shape_composition_behavior) {
    const struct {
        uint32_t key;
        const char* full_width;
    } cases[] = {
        {VK_OEM_2, "\xef\xbc\x8f"},
        {VK_OEM_5, "\xef\xbc\xbc"},
    };
    for (const auto& test_case : cases) {
        SymbolEngineFixture candidate_fixture;
        ASSERT_TRUE(candidate_fixture.initialize());
        cxxime::OutputOptions options;
        options.full_shape = true;

        ASSERT_EQ(candidate_fixture.engine().process_key(make_key('A'), options),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_GE(candidate_fixture.engine().context().candidates.candidates.size(), 2u);
        candidate_fixture.engine().context().candidates.highlighted = 1;
        const std::string expected_candidate =
            candidate_fixture.engine().context().candidates.candidates[1].text +
            test_case.full_width;
        ASSERT_EQ(candidate_fixture.engine().process_key(make_key(test_case.key), options),
                  cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(candidate_fixture.engine().take_commit_text_with_source().first,
                  expected_candidate);

        SymbolEngineFixture raw_fixture;
        ASSERT_TRUE(raw_fixture.initialize());
        ASSERT_EQ(raw_fixture.engine().process_key(make_key('Z'), options),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(raw_fixture.engine().context().candidates.candidates.empty());
        ASSERT_EQ(raw_fixture.engine().process_key(make_key(test_case.key), options),
                  cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(raw_fixture.engine().take_commit_text_with_source().first,
                  std::string("z") + test_case.full_width);
    }
}

TEST(SymbolInput, numpad_operator_stays_ascii_in_full_shape_composition) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    type_symbol_code(fixture.engine(), "bd");
    ASSERT_TRUE(!fixture.engine().context().candidates.candidates.empty());

    cxxime::OutputOptions options;
    options.full_shape = true;

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_ADD), options),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\bd+");
    ASSERT_EQ(fixture.engine().context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    ASSERT_TRUE(fixture.engine().context().candidates.candidates.empty());
    ASSERT_TRUE(fixture.engine().context().committed_text.empty());
}

TEST(SymbolInput, slash_and_backslash_finish_composition_with_english_punctuation) {
    const struct {
        uint32_t key;
        const char* text;
    } cases[] = {
        {VK_OEM_2, "/"},
        {VK_OEM_5, "\\"},
    };
    for (const auto& test_case : cases) {
        SymbolEngineFixture fixture;
        ASSERT_TRUE(fixture.initialize());
        cxxime::OutputOptions options;
        options.chinese_punct = false;

        ASSERT_EQ(fixture.engine().process_key(make_key('A'), options),
                  cxxime::ProcessResult::ACCEPTED);
        ASSERT_GE(fixture.engine().context().candidates.candidates.size(), 2u);
        fixture.engine().context().candidates.highlighted = 1;
        const std::string expected =
            fixture.engine().context().candidates.candidates[1].text + test_case.text;
        ASSERT_EQ(fixture.engine().process_key(make_key(test_case.key), options),
                  cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(fixture.engine().take_commit_text_with_source().first, expected);
        ASSERT_TRUE(!fixture.engine().context().is_composing());
    }
}

TEST(SymbolInput, rejected_printable_restores_symbol_origin_after_delete) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "bd");
    ASSERT_TRUE(!fixture.engine().context().candidates.candidates.empty());

    ASSERT_EQ(fixture.engine().process_key(make_shift_key(VK_OEM_PLUS)),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\bd+");
    ASSERT_EQ(fixture.engine().context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    ASSERT_TRUE(fixture.engine().context().candidates.candidates.empty());

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\bd");
    ASSERT_EQ(fixture.engine().context().composition_kind(), cxxime::CompositionKind::kSymbol);
    ASSERT_TRUE(!fixture.engine().context().candidates.candidates.empty());
}

TEST(SymbolInput, shifted_digit_does_not_select_candidate) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "bd");
    const int highlighted = fixture.engine().context().candidates.highlighted;
    ASSERT_GE(highlighted, 0);
    const std::string expected =
        fixture.engine().context().candidates.candidates[highlighted].text + "!";

    ASSERT_EQ(fixture.engine().process_key(make_shift_key('1')), cxxime::ProcessResult::COMMITTED);
    const auto committed = fixture.engine().take_commit_text_with_source();
    ASSERT_EQ(committed.first, expected);
    ASSERT_EQ(committed.second, cxxime::CommitSource::kCandidate);
}

TEST(SymbolInput, shifted_zero_commits_candidate_with_right_parenthesis) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "bd");
    const int highlighted = fixture.engine().context().candidates.highlighted;
    ASSERT_GE(highlighted, 0);
    const std::string expected =
        fixture.engine().context().candidates.candidates[highlighted].text + "\xef\xbc\x89";
    cxxime::PunctMapping punctuation;
    punctuation.half_shape[")"] = {cxxime::PunctType::COMMIT, "\xef\xbc\x89", {}, {}};
    cxxime::OutputOptions options;
    options.punct_mapping = &punctuation;

    ASSERT_EQ(fixture.engine().process_key(make_shift_key('0'), options),
              cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine().take_commit_text_with_source().first, expected);
    ASSERT_TRUE(!fixture.engine().context().is_composing());
}

TEST(SymbolInput, supports_navigation_pagination_editing_and_cancel) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "sz");

    const std::string first = fixture.engine().context().candidates.candidates[0].text;
    const int page_size = static_cast<int>(fixture.engine().context().candidates.candidates.size());
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_DOWN)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().candidates.highlighted, 1);
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_UP)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().candidates.highlighted, 0);
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_NEXT)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().candidates.page_offset, page_size);
    ASSERT_NE(fixture.engine().context().candidates.candidates[0].text, first);

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\s");
    ASSERT_TRUE(fixture.engine().context().candidates.candidates.empty());
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_ESCAPE)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!fixture.engine().context().is_composing());
}

TEST(SymbolInput, ignores_wubi_auto_commit_rules) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.engine().switch_mode(cxxime::InputMode::WUBI);
    type_symbol_code(fixture.engine(), "bd");

    ASSERT_EQ(fixture.engine().process_key(make_key('X')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "\\bdxa");
    ASSERT_TRUE(fixture.engine().context().committed_text.empty());
}

RUN_ALL_TESTS()
