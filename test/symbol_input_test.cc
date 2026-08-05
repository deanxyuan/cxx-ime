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

        if (!cxxime::Dict::create_test_dict(dict_path_, {{"a", "阿", 100}}) ||
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
    cxxime::Dict& dict() { return dict_; }

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
    ASSERT_EQ(engine.process_key(make_key(VK_OEM_2), options), cxxime::ProcessResult::ACCEPTED);
    for (const char* ch = code; *ch; ++ch) {
        ASSERT_EQ(engine.process_key(make_key(static_cast<uint32_t>(*ch - 'a' + 'A')), options),
                  cxxime::ProcessResult::ACCEPTED);
    }
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
        ASSERT_EQ(fixture.engine().context().pinyin_buffer, "/bd");
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
    ASSERT_EQ(committed.first, "/");
    ASSERT_EQ(committed.second, cxxime::CommitSource::kRawCodePreserveCase);
}

TEST(SymbolInput, bare_trigger_navigates_categories_without_committing) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "");

    const auto& categories = fixture.engine().context().candidates;
    ASSERT_EQ(categories.total_count, 14);
    ASSERT_EQ(categories.candidates[0].text, "标点");
    ASSERT_EQ(categories.candidates[0].comment, "/bd");

    ASSERT_EQ(fixture.engine().process_key(make_key('1')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "/bd");
    ASSERT_TRUE(fixture.engine().context().committed_text.empty());
    ASSERT_EQ(fixture.engine().context().candidates.candidates[0].text, "。");

    fixture.engine().clear();
    type_symbol_code(fixture.engine(), "");
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_SPACE)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "/bd");
}

TEST(SymbolInput, shift_commits_raw_trigger_and_switches_to_english) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    type_symbol_code(fixture.engine(), "");

    ASSERT_EQ(fixture.engine().process_key(make_key(VK_LSHIFT)), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_LSHIFT, true)),
              cxxime::ProcessResult::COMMITTED);
    const auto committed = fixture.engine().take_commit_text_with_source();
    ASSERT_EQ(committed.first, "/");
    ASSERT_TRUE(fixture.engine().ascii_composer().is_ascii_mode());
}

TEST(SymbolInput, disabled_state_passes_trigger_to_application) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    cxxime::OutputOptions options;
    options.chinese_punct = false;
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_OEM_2), options),
              cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(!fixture.engine().context().is_composing());

    options.chinese_mode = false;
    options.chinese_punct = true;
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_OEM_2), options),
              cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(!fixture.engine().context().is_composing());
}

TEST(SymbolInput, commits_existing_candidate_then_starts_symbol_input) {
    SymbolEngineFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    ASSERT_EQ(fixture.engine().process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!fixture.engine().context().candidates.candidates.empty());
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_OEM_2)), cxxime::ProcessResult::COMMITTED);

    const auto first_commit = fixture.engine().take_commit_text_with_source();
    ASSERT_EQ(first_commit.first, "阿");
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "/");
    ASSERT_EQ(fixture.engine().context().candidates.total_count, 14);
    ASSERT_EQ(fixture.engine().context().candidates.candidates[0].text, "标点");

    ASSERT_EQ(fixture.engine().process_key(make_key('B')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().process_key(make_key('D')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine().process_key(make_key(VK_SPACE)), cxxime::ProcessResult::COMMITTED);
    const auto symbol_commit = fixture.engine().take_commit_text_with_source();
    ASSERT_EQ(symbol_commit.first, "。");
    ASSERT_TRUE(!fixture.dict().has_user_entry("。"));
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
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "/s");
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
    ASSERT_EQ(fixture.engine().context().pinyin_buffer, "/bdxa");
    ASSERT_TRUE(fixture.engine().context().committed_text.empty());
}

RUN_ALL_TESTS()
