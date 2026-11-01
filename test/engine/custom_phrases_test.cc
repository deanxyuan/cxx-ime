// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <windows.h>

#include <cxxime/custom_phrases.h>
#include <cxxime/engine.h>
#include <cxxime/pinyin_scheme.h>

#include "support/testutil.h"

namespace {

class PhraseFixture {
public:
    PhraseFixture() {
        char temp[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, temp) != 0);
        char unique[MAX_PATH] = {};
        ASSERT_TRUE(GetTempFileNameA(temp, "cxp", 0, unique) != 0);
        path = unique;
        ASSERT_TRUE(phrases->load(path));
    }

    ~PhraseFixture() {
        engine.finalize();
        pinyin->close();
        wubi->close();
        DeleteFileA(path.c_str());
        DeleteFileA((path + ".bin").c_str());
        DeleteFileA((path + ".export").c_str());
        DeleteFileA((path + ".spellings").c_str());
    }

    void initialize(const std::string& scheme_id = "pinyin", bool learning = false) {
        ASSERT_TRUE(cxxime::Dict::create_test_dict(path + ".bin", {{"ni", "ordinary-one", 90},
                                                                   {"ni", "ordinary-two", 80},
                                                                   {"ni", "ordinary-three", 70},
                                                                   {"abcd", "four-code", 100},
                                                                   {"hao", u8"好", 100},
                                                                   {"ni", "duplicate", 200}}));
        ASSERT_TRUE(pinyin->open_dict(path + ".bin"));
        ASSERT_TRUE(wubi->open_dict(path + ".bin"));
        cxxime::Config config;
        config.pinyin_scheme = scheme_id;
        config.page_size = 2;
        config.wubi_auto_commit = true;
        config.wubi_commit_first_on_fifth_key = true;
        config.wubi_restart_on_fifth_after_miss = true;
        config.candidate_learning = learning;
        const auto& scheme = cxxime::resolve_pinyin_scheme(scheme_id);
        const bool shuangpin = scheme.kind == cxxime::PinyinSchemeKind::kShuangpin;
        ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
            path + ".spellings", {{"ni", "ni", cxxime::kNormalSpelling, 0.0f},
                                  {"hao", "hao", cxxime::kNormalSpelling, 0.0f}}));
        auto resources = cxxime::PinyinResourceSet::create(
            scheme.id, scheme.kind,
            shuangpin ? std::string(CXXIME_DATA_DIR) + scheme.spelling_filename
                      : path + ".spellings",
            shuangpin ? cxxime::PinyinSpellingRequirement::kRequired
                      : cxxime::PinyinSpellingRequirement::kOptionalForFullPinyin);
        ASSERT_TRUE(engine.initialize(cxxime::EngineRuntimeState::create(
            config, pinyin, wubi, resources, nullptr, nullptr, phrases)));
        engine.set_trace_enabled(false);
        engine.set_query_deadline_ms(0);
    }

    cxxime::ProcessResult key(unsigned code) {
        cxxime::KeyEvent event;
        event.keycode = code;
        return engine.process_key(event, {}, 2);
    }

    void type(const std::string& code) {
        for (char ch : code) {
            ASSERT_EQ(key(static_cast<unsigned>(ch - 'a' + 'A')), cxxime::ProcessResult::ACCEPTED);
            ASSERT_TRUE(engine.context().committed_text.empty());
        }
    }

    std::string path;
    std::shared_ptr<cxxime::CustomPhrases> phrases = std::make_shared<cxxime::CustomPhrases>();
    std::shared_ptr<cxxime::Dict> pinyin =
        std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    std::shared_ptr<cxxime::Dict> wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
    cxxime::Engine engine;
};

} // namespace

TEST(CustomPhrases, literal_content_order_edit_and_reopen) {
    PhraseFixture fixture;
    auto& phrases = *fixture.phrases;
    ASSERT_TRUE(phrases.add("yx", "abc@qq.com"));
    ASSERT_TRUE(phrases.add("yx", "second@example.com"));
    ASSERT_TRUE(phrases.add("jtdz", u8"北京市朝阳区示例路 10 号"));
    ASSERT_TRUE(phrases.add("fh", "@"));
    ASSERT_TRUE(!phrases.add("yx", "abc@qq.com"));
    ASSERT_TRUE(phrases.has_prefix("jt"));
    ASSERT_TRUE(phrases.lookup("jt").empty());
    ASSERT_TRUE(phrases.move({"second@example.com", "yx"}, true));
    ASSERT_TRUE(phrases.replace({"abc@qq.com", "yx"}, {"new@example.com", "yx"}));
    cxxime::CustomPhrases reopened;
    ASSERT_TRUE(reopened.load(fixture.path));
    const std::vector<std::string> expected = {"second@example.com", "new@example.com"};
    ASSERT_TRUE(reopened.lookup("yx") == expected);
    auto page = reopened.query("yx", 1, 1);
    ASSERT_EQ(page.entries.size(), std::size_t{1});
    ASSERT_EQ(page.entries[0].sequence, std::uint64_t{2});
    ASSERT_TRUE(!page.has_more);
    ASSERT_TRUE(phrases.erase({{"@", "fh"}}));
    ASSERT_TRUE(phrases.lookup("fh").empty());
}

TEST(CustomPhrases, invalid_import_and_failed_save_leave_previous_data_intact) {
    PhraseFixture fixture;
    auto& phrases = *fixture.phrases;
    ASSERT_TRUE(phrases.add("yx", "abc@qq.com"));
    for (const std::string contents :
         {"ABC\taddress\n", "yx\tline\tbreak\n", "yx\tone\nyx\tone\n", "yx\t\n"}) {
        ASSERT_TRUE(!phrases.merge_contents(contents, nullptr));
        ASSERT_EQ(phrases.lookup("yx").front(), "abc@qq.com");
    }
    ASSERT_TRUE(!phrases.add("a1", "text"));
    ASSERT_TRUE(!phrases.add("a", std::string(256, 'x')));
    ASSERT_TRUE(!phrases.add(std::string(65, 'a'), "text"));
    ASSERT_TRUE(phrases.add(std::string(64, 'a'), std::string(255, 'x')));
    HANDLE locked = CreateFileA(fixture.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(locked != INVALID_HANDLE_VALUE);
    ASSERT_TRUE(!phrases.add("yx", "unpublished"));
    CloseHandle(locked);
    ASSERT_EQ(phrases.lookup("yx").size(), std::size_t{1});
}

TEST(CustomPhrases, export_and_merge_preserve_existing_rank_and_append_new_items) {
    PhraseFixture fixture;
    ASSERT_TRUE(fixture.phrases->add("yx", "first"));
    cxxime::UserDataMergeResult result;
    ASSERT_TRUE(fixture.phrases->merge_contents("yx\tsecond\r\nyx\tfirst\r\n", &result));
    ASSERT_EQ(result.imported_count, std::size_t{1});
    ASSERT_EQ(result.skipped_count, std::size_t{1});
    ASSERT_TRUE(fixture.phrases->export_file(fixture.path + ".export"));
    ASSERT_TRUE(fixture.phrases->erase({{"second", "yx"}}));
    ASSERT_TRUE(fixture.phrases->import_file(fixture.path + ".export"));
    const std::vector<std::string> expected = {"first", "second"};
    ASSERT_TRUE(fixture.phrases->lookup("yx") == expected);
}

TEST(CustomPhrases, exact_raw_keys_work_in_pinyin_shuangpin_wubi_and_mixed_without_learning) {
    for (const char* scheme : {"pinyin", "xiaohe_shuangpin"}) {
        PhraseFixture fixture;
        ASSERT_TRUE(fixture.phrases->add("jtdz", u8"示例路 10 号"));
        ASSERT_TRUE(fixture.phrases->add("youxiang", "abc@qq.com"));
        fixture.initialize(scheme, true);
        for (auto mode :
             {cxxime::InputMode::PINYIN, cxxime::InputMode::WUBI, cxxime::InputMode::MIXED}) {
            fixture.engine.switch_mode(mode);
            for (const char* code : {"jtdz", "youxiang"}) {
                fixture.type(code);
                ASSERT_TRUE(fixture.engine.context().candidate(0) != nullptr);
                ASSERT_EQ(fixture.engine.context().candidate(0)->source,
                          cxxime::CandidateSource::kCustomPhrase);
                ASSERT_EQ(fixture.key(VK_SPACE), cxxime::ProcessResult::COMMITTED);
                 ASSERT_EQ(fixture.engine.get_commit_text(),
                    std::string(code) == "jtdz" ? u8"示例路 10 号" : "abc@qq.com");
            }
        }
        ASSERT_EQ(fixture.pinyin->candidate_preference_count(), std::size_t{0});
        ASSERT_EQ(fixture.wubi->candidate_preference_count(), std::size_t{0});
        // Prove learning is active: an ordinary candidate must update its preference.
        fixture.engine.switch_mode(cxxime::InputMode::WUBI);
        fixture.type("ni");
        const std::string ordinary = fixture.engine.context().candidate(0)->text;
        ASSERT_EQ(fixture.key(VK_SPACE), cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(fixture.engine.get_commit_text(), ordinary);
        ASSERT_TRUE(fixture.wubi->has_candidate_preference(ordinary, "ni"));
    }
}

TEST(CustomPhrases, paging_deduplicates_ordinary_candidates_and_retains_phrase_order) {
    PhraseFixture fixture;
    ASSERT_TRUE(fixture.phrases->add("ni", "first-phrase"));
    ASSERT_TRUE(fixture.phrases->add("ni", "duplicate"));
    ASSERT_TRUE(fixture.phrases->add("ni", "third-phrase"));
    fixture.initialize();
    fixture.type("n");
    for (const auto& entry : fixture.engine.context().translation().entries) {
        ASSERT_TRUE(entry.candidate.source != cxxime::CandidateSource::kCustomPhrase);
    }
    fixture.type("i");
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "first-phrase");
    ASSERT_EQ(fixture.engine.context().candidate(1)->text, "duplicate");
    ASSERT_EQ(fixture.key(VK_NEXT), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "third-phrase");
    ASSERT_EQ(fixture.engine.context().candidate(1)->text, "ordinary-one");
    ASSERT_EQ(fixture.key(VK_NEXT), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "ordinary-two");
    ASSERT_EQ(fixture.engine.context().candidate(1)->text, "ordinary-three");
    ASSERT_EQ(fixture.key(VK_PRIOR), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "third-phrase");
    ASSERT_EQ(fixture.key('1'), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "third-phrase");
}

TEST(CustomPhrases, long_wubi_code_survives_fourth_and_fifth_keys_and_normal_auto_commit_remains) {
    PhraseFixture fixture;
    ASSERT_TRUE(fixture.phrases->add("abcdef", "long-code-phrase"));
    fixture.initialize();
    fixture.engine.switch_mode(cxxime::InputMode::WUBI);
    fixture.type("abcd");
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "four-code");
    fixture.type("ef");
    ASSERT_EQ(fixture.engine.context().active_input(), "abcdef");
    ASSERT_EQ(fixture.key(VK_SPACE), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "long-code-phrase");
    fixture.type("abcd");
    ASSERT_EQ(fixture.key('Z'), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.take_commit_text_with_source().first, "four-code");
    ASSERT_EQ(fixture.engine.context().active_input(), "z");
    fixture.key(VK_ESCAPE);
    ASSERT_TRUE(fixture.phrases->erase({{"long-code-phrase", "abcdef"}}));
    fixture.type("abc");
    ASSERT_EQ(fixture.key('D'), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), "four-code");
}

TEST(CustomPhrases, editing_cancel_and_english_input_keep_their_existing_meaning) {
    PhraseFixture fixture;
    ASSERT_TRUE(fixture.phrases->add("jtdz", "Address 1"));
    fixture.initialize();
    fixture.type("jtdzx");
    for (const auto& entry : fixture.engine.context().translation().entries) {
        ASSERT_TRUE(entry.candidate.source != cxxime::CandidateSource::kCustomPhrase);
    }
    ASSERT_EQ(fixture.key(VK_BACK), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(fixture.engine.context().candidate(0) != nullptr);
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "Address 1");
    ASSERT_EQ(fixture.key(VK_ESCAPE), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!fixture.engine.context().is_composing());
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
    fixture.engine.ascii_composer().set_ascii_mode(true);
    std::string literal;
    for (char key : std::string("JTDZ")) {
        ASSERT_EQ(fixture.key(key), cxxime::ProcessResult::COMMITTED);
        literal += fixture.engine.get_commit_text();
    }
    ASSERT_EQ(literal, "jtdz");
}

TEST(CustomPhrases, segmented_prefix_filters_uncommittable_phrases_before_paging) {
    PhraseFixture fixture;
    const std::string too_long(253, 'a');
    const std::string boundary(252, 'b');
    ASSERT_TRUE(fixture.phrases->add("ni", too_long));
    ASSERT_TRUE(fixture.phrases->add("ni", boundary));
    ASSERT_TRUE(fixture.phrases->add("ni", "Room 10"));
    fixture.initialize();
    fixture.engine.set_partial_selection_enabled(true);

    // The same phrase is valid alone and must be preserved without truncation.
    fixture.type("ni");
    ASSERT_TRUE(fixture.engine.select_candidate(0));
    ASSERT_EQ(fixture.engine.get_commit_text(), too_long);

    auto confirm_prefix = [&] {
        fixture.type("haoni");
        for (int page = 0; page < 8; ++page) {
            const auto& entries = fixture.engine.context().translation().entries;
            for (std::size_t index = 0; index < entries.size(); ++index) {
                const auto* action =
                    std::get_if<cxxime::TextSelectionAction>(&entries[index].selection);
                if (entries[index].candidate.text == u8"好" && action &&
                    action->consumed_input_bytes == 3) {
                    ASSERT_TRUE(fixture.engine.select_candidate(static_cast<int>(index)));
                    ASSERT_EQ(fixture.engine.context().active_input(), "ni");
                    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
                    ASSERT_EQ(fixture.engine.context().composition().converted_segments().size(),
                              std::size_t{1});
                    return;
                }
            }
            ASSERT_EQ(fixture.key(VK_NEXT), cxxime::ProcessResult::ACCEPTED);
        }
        ASSERT_TRUE(false);
    };
    confirm_prefix();
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, boundary);
    ASSERT_EQ(fixture.engine.context().candidate(1)->text, "Room 10");
    ASSERT_EQ(fixture.key(VK_NEXT), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "duplicate");
    ASSERT_EQ(fixture.key(VK_PRIOR), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.key(VK_SPACE), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), std::string(u8"好") + boundary);

    confirm_prefix();
    ASSERT_EQ(fixture.key('2'), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(fixture.engine.get_commit_text(), u8"好Room 10");

    ASSERT_TRUE(fixture.phrases->erase({{boundary, "ni"}, {"Room 10", "ni"}}));
    confirm_prefix();
    // If no custom phrase fits, ordinary selection must still work.
    ASSERT_EQ(fixture.engine.context().candidate(0)->text, "duplicate");
    ASSERT_TRUE(fixture.engine.select_candidate(0));
    ASSERT_EQ(fixture.engine.get_commit_text(), u8"好duplicate");
}

RUN_ALL_TESTS()
