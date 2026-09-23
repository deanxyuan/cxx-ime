// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <windows.h>

#include <cxxime/composition_presentation.h>
#include <cxxime/config.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/input_limits.h>
#include <cxxime/key_event.h>
#include <cxxime/pinyin_scheme.h>
#include <cxxime/pinyin_user_code.h>
#include <cxxime/query_trace.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/translator.h>

#include "support/testutil.h"
#include "support/topn_test_data.h"

namespace {

std::string shuangpin_temp_path(const char* prefix) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, prefix, 0, path) == 0) {
        return {};
    }
    return path;
}

cxxime::KeyEvent shuangpin_key(uint32_t keycode) {
    cxxime::KeyEvent event;
    event.keycode = keycode;
    return event;
}

struct ShuangpinFixture {
    std::string dict_path = shuangpin_temp_path("spd");
    std::string spellings_path = shuangpin_temp_path("sps");
    std::string topn_path = shuangpin_temp_path("spt");
    std::string user_path = shuangpin_temp_path("spu");
    std::string manual_order_path = shuangpin_temp_path("spo");
    cxxime::Dict dict;
    cxxime::SpellingsIndex spellings;
    std::unique_ptr<cxxime::Syllabifier> syllabifier;
    cxxime::Config config;
    cxxime::Engine engine;

    bool initialize(bool with_topn = false) {
        DeleteFileA(user_path.c_str());
        DeleteFileA(manual_order_path.c_str());
        DeleteFileA(topn_path.c_str());
        if (!cxxime::Dict::create_test_dict(dict_path, {{"ni:hao", "你好", 1000},
                                                        {"ni", "你", 500},
                                                        {"hao", "好", 600},
                                                        {"hao", "号", 100},
                                                        {"ying", "应", 800},
                                                        {"lue", "略", 700},
                                                        {"lve", "率", 900},
                                                        {"lve", "绿", 850},
                                                        {"lve", "律", 800},
                                                        {"lve", "掠", 100}}) ||
            !cxxime::SpellingsIndex::create_test_trie(
                spellings_path, {{"ni", "ni", cxxime::kNormalSpelling, 0.0f},
                                 {"hk", "hao", cxxime::kNormalSpelling, 0.0f},
                                 {"y;", "ying", cxxime::kNormalSpelling, 0.0f},
                                 {"lt", "lue", cxxime::kNormalSpelling, 0.0f},
                                 {"lt", "lve", cxxime::kNormalSpelling, 0.0f}})) {
            return false;
        }
        if (with_topn) {
            std::vector<cxxime::Candidate> candidates = {{"好", "", 600}, {"号", "", 100}};
            for (auto& candidate : candidates) {
                candidate.syllables = "hao";
            }
            if (!cxxime::test::create_test_topn(topn_path, dict_path, {{"hao", candidates}})) {
                return false;
            }
        }
        const bool opened = with_topn
                                ? dict.open_bundle(dict_path, user_path, std::string{}, topn_path)
                                : dict.open(dict_path, user_path);
        if (!opened ||
            !dict.load_manual_candidate_order(manual_order_path, cxxime::kMaxInputCodeLength) ||
            !spellings.load(spellings_path)) {
            return false;
        }
        syllabifier = std::make_unique<cxxime::Syllabifier>(spellings);
        config.page_size = 10;
        config.pinyin_scheme = "microsoft_shuangpin";
        if (!engine.initialize(dict, spellings, syllabifier.get(), config)) {
            return false;
        }
        engine.set_query_deadline_ms(0);
        engine.set_partial_selection_enabled(true);
        return true;
    }

    void type(const std::string& code) {
        for (char character : code) {
            const uint32_t keycode =
                character == ';' ? VK_OEM_1 : static_cast<uint32_t>(character - 'a' + 'A');
            ASSERT_EQ(engine.process_key(shuangpin_key(keycode)), cxxime::ProcessResult::ACCEPTED);
        }
    }

    const cxxime::CandidateEntry* find(const std::string& text, std::size_t consumed) const {
        const auto& entries = engine.context().translation().entries;
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
            return entry.candidate.text == text && action &&
                   action->consumed_input_bytes == consumed;
        });
        return found == entries.end() ? nullptr : &*found;
    }

    ~ShuangpinFixture() {
        engine.finalize();
        dict.close();
        spellings.unload();
        DeleteFileA(dict_path.c_str());
        DeleteFileA(spellings_path.c_str());
        DeleteFileA(topn_path.c_str());
        DeleteFileA(user_path.c_str());
        DeleteFileA(manual_order_path.c_str());
    }
};

} // namespace

TEST(Shuangpin, built_in_scheme_descriptors_are_stable) {
    struct ExpectedScheme {
        const char* id;
        cxxime::PinyinSchemeKind kind;
        const char* role;
        const char* filename;
        const char* input_example;
    };
    const ExpectedScheme expected[] = {
        {"full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, "pinyin_spellings",
         "pinyin.spellings.bin", "ni'hao"},
        {"microsoft_shuangpin", cxxime::PinyinSchemeKind::kShuangpin,
         "pinyin_spellings_microsoft_shuangpin", "pinyin.microsoft-shuangpin.spellings.bin",
         "ni'hk"},
        {"xiaohe_shuangpin", cxxime::PinyinSchemeKind::kShuangpin,
         "pinyin_spellings_xiaohe_shuangpin", "pinyin.xiaohe-shuangpin.spellings.bin", "ni'hc"},
        {"ziranma_shuangpin", cxxime::PinyinSchemeKind::kShuangpin,
         "pinyin_spellings_ziranma_shuangpin", "pinyin.ziranma-shuangpin.spellings.bin", "ni'hk"},
        {"sogou_shuangpin", cxxime::PinyinSchemeKind::kShuangpin,
         "pinyin_spellings_sogou_shuangpin", "pinyin.sogou-shuangpin.spellings.bin", "ni'hk"},
    };

    const auto& schemes = cxxime::built_in_pinyin_schemes();
    ASSERT_EQ(schemes.size(), sizeof(expected) / sizeof(expected[0]));
    for (std::size_t i = 0; i < schemes.size(); ++i) {
        ASSERT_EQ(std::string(schemes[i].id), expected[i].id);
        ASSERT_EQ(schemes[i].kind, expected[i].kind);
        ASSERT_EQ(std::string(schemes[i].manifest_role), expected[i].role);
        ASSERT_EQ(std::string(schemes[i].spelling_filename), expected[i].filename);
        ASSERT_EQ(std::string(schemes[i].input_example), expected[i].input_example);
        ASSERT_TRUE(std::string(schemes[i].input_example).find('\'') != std::string::npos);
    }
    ASSERT_EQ(cxxime::normalize_pinyin_scheme_id("unknown"), "full_pinyin");
}

TEST(Shuangpin, built_in_scheme_descriptors_are_complete_and_unique) {
    std::vector<std::string> ids;
    std::vector<std::string> roles;
    std::vector<std::string> filenames;
    for (const auto& scheme : cxxime::built_in_pinyin_schemes()) {
        ASSERT_TRUE(scheme.id && *scheme.id);
        ASSERT_TRUE(scheme.display_name && *scheme.display_name);
        ASSERT_TRUE(scheme.manifest_role && *scheme.manifest_role);
        ASSERT_TRUE(scheme.spelling_filename && *scheme.spelling_filename);
        ASSERT_TRUE(scheme.input_example && *scheme.input_example);
        ASSERT_TRUE(std::find(ids.begin(), ids.end(), scheme.id) == ids.end());
        ASSERT_TRUE(std::find(roles.begin(), roles.end(), scheme.manifest_role) == roles.end());
        ASSERT_TRUE(std::find(filenames.begin(), filenames.end(), scheme.spelling_filename) ==
                    filenames.end());
        ids.emplace_back(scheme.id);
        roles.emplace_back(scheme.manifest_role);
        filenames.emplace_back(scheme.spelling_filename);
    }
}

TEST(Shuangpin, full_and_partial_candidates_keep_canonical_keys_and_raw_spans) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("nihk");

    const cxxime::CandidateEntry* full = fixture.find("你好", 4);
    const cxxime::CandidateEntry* partial = fixture.find("你", 2);
    ASSERT_TRUE(full != nullptr);
    ASSERT_TRUE(partial != nullptr);
    const auto& full_action = std::get<cxxime::TextSelectionAction>(full->selection);
    const auto& partial_action = std::get<cxxime::TextSelectionAction>(partial->selection);
    ASSERT_EQ(full_action.variants[0].input_code, "nihao");
    ASSERT_EQ(partial_action.variants[0].input_code, "ni");

    const cxxime::CompositionPresentation presentation = cxxime::derive_composition_presentation(
        fixture.engine.context().composition(), fixture.syllabifier.get(), 4, true, {}, true);
    ASSERT_EQ(presentation.logical_preedit, "nihk");
    ASSERT_EQ(presentation.display_preedit, "ni'hk");
}

TEST(Shuangpin, partial_selection_consumes_two_raw_keys_and_learning_uses_full_pinyin) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.config.candidate_learning = true;
    fixture.engine.reload_config(fixture.config);
    fixture.type("nihk");

    const cxxime::CandidateEntry* partial = fixture.find("你", 2);
    ASSERT_TRUE(partial != nullptr);
    const auto& entries = fixture.engine.context().translation().entries;
    const int index = static_cast<int>(partial - entries.data());
    ASSERT_TRUE(fixture.engine.select_candidate(index));
    ASSERT_EQ(fixture.engine.context().active_input(), "hk");

    const cxxime::CandidateEntry* suffix = fixture.find("好", 2);
    ASSERT_TRUE(suffix != nullptr);
    const auto& suffix_entries = fixture.engine.context().translation().entries;
    const int suffix_index = static_cast<int>(suffix - suffix_entries.data());
    ASSERT_TRUE(fixture.engine.select_candidate(suffix_index));
    ASSERT_EQ(fixture.engine.get_commit_text(), "你好");
    ASSERT_TRUE(fixture.dict.has_candidate_preference("你", "ni"));
    ASSERT_TRUE(fixture.dict.has_candidate_preference("好", "hao"));
}

TEST(Shuangpin, odd_terminal_key_completes_without_changing_raw_preedit) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("nih");

    ASSERT_TRUE(fixture.find("你好", 3) != nullptr);
    const cxxime::CompositionPresentation presentation = cxxime::derive_composition_presentation(
        fixture.engine.context().composition(), fixture.syllabifier.get(), 3, true, {}, true);
    ASSERT_EQ(presentation.logical_preedit, "nih");
    ASSERT_EQ(presentation.display_preedit, "ni'h");
}

TEST(Shuangpin, middle_edit_redecodes_from_raw_key_positions) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.type("nihk");

    ASSERT_EQ(fixture.engine.process_key(shuangpin_key(VK_LEFT)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.process_key(shuangpin_key(VK_LEFT)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.process_key(shuangpin_key(VK_DELETE)),
              cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fixture.engine.context().active_input(), "nik");
    fixture.type("h");

    ASSERT_EQ(fixture.engine.context().active_input(), "nihk");
    ASSERT_TRUE(fixture.find("你好", 4) != nullptr);
}

TEST(Shuangpin, user_lexicon_uses_the_shared_full_pinyin_key) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    ASSERT_TRUE(fixture.dict.add_user_entry("拟好", "nihao", "ni:hao"));
    ASSERT_TRUE(!fixture.dict.add_user_entry("不可达", "nihk"));

    fixture.type("nihk");
    ASSERT_TRUE(fixture.find("拟好", 4) != nullptr);
}

TEST(Shuangpin, user_code_normalization_accepts_canonical_pinyin_and_complete_shuangpin) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    ASSERT_TRUE(cxxime::is_canonical_pinyin_user_code("nihao", "ni:hao"));
    ASSERT_TRUE(!cxxime::is_canonical_pinyin_user_code("nihk"));

    std::string code;
    std::string syllables;
    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("nihao", cxxime::PinyinSchemeKind::kShuangpin,
                                                      fixture.syllabifier.get(), &code,
                                                      &syllables));
    ASSERT_EQ(code, "nihao");
    ASSERT_TRUE(syllables.empty());

    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("nihk", cxxime::PinyinSchemeKind::kShuangpin,
                                                      fixture.syllabifier.get(), &code,
                                                      &syllables));
    ASSERT_EQ(code, "nihao");
    ASSERT_EQ(syllables, "ni:hao");

    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code(
        "y;", cxxime::PinyinSchemeKind::kShuangpin, fixture.syllabifier.get(), &code, &syllables));
    ASSERT_EQ(code, "ying");
    ASSERT_EQ(syllables, "ying");
    ASSERT_TRUE(!cxxime::canonicalize_pinyin_user_code(
        "nih", cxxime::PinyinSchemeKind::kShuangpin, fixture.syllabifier.get(), &code, &syllables));
}

TEST(Shuangpin, mixed_mode_reuses_the_pinyin_decoder) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    fixture.engine.set_wubi_dict(&fixture.dict);
    fixture.engine.set_partial_selection_enabled(false);
    fixture.engine.switch_mode(cxxime::InputMode::MIXED);

    fixture.type("nihk");
    const cxxime::CandidateEntry* candidate = fixture.find("你好", 4);
    ASSERT_TRUE(candidate != nullptr);
    const auto& action = std::get<cxxime::TextSelectionAction>(candidate->selection);
    ASSERT_EQ(action.variants[0].input_code, "nihao");
}

TEST(Shuangpin, semicolon_is_an_input_key_only_after_composition_starts) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    ASSERT_EQ(fixture.engine.process_key(shuangpin_key(VK_OEM_1)), cxxime::ProcessResult::REJECTED);
    fixture.type("y;");
    ASSERT_TRUE(fixture.find("应", 2) != nullptr);

    cxxime::KeyEvent semicolon_up = shuangpin_key(VK_OEM_1);
    semicolon_up.is_key_up = true;
    ASSERT_EQ(fixture.engine.process_key(semicolon_up), cxxime::ProcessResult::REJECTED);
    ASSERT_EQ(fixture.engine.context().active_input(), "y;");
    ASSERT_TRUE(fixture.engine.get_commit_text().empty());
}

TEST(Shuangpin, invalid_tail_does_not_create_or_consume_a_full_span_candidate) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    fixture.type("ni;");
    ASSERT_TRUE(fixture.engine.context().translation().entries.empty());
    ASSERT_TRUE(fixture.engine.translate_for_search("ni;", 10).candidates.empty());

    fixture.engine.clear();
    fixture.type("ni;hk");
    ASSERT_TRUE(fixture.engine.context().translation().entries.empty());
    ASSERT_TRUE(fixture.engine.translate_for_search("ni;hk", 10).candidates.empty());
}

TEST(Shuangpin, search_learning_uses_each_candidates_canonical_key) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());

    const cxxime::CandidatePage collision = fixture.engine.translate_for_search("lt", 10);
    const auto lue = std::find_if(collision.candidates.begin(), collision.candidates.end(),
                                  [](const auto& candidate) { return candidate.text == "略"; });
    const auto lve = std::find_if(collision.candidates.begin(), collision.candidates.end(),
                                  [](const auto& candidate) { return candidate.text == "掠"; });
    ASSERT_TRUE(lue != collision.candidates.end());
    ASSERT_TRUE(lve != collision.candidates.end());
    ASSERT_EQ(lue->input_code, "lue");
    ASSERT_EQ(lve->input_code, "lve");

    ASSERT_TRUE(fixture.engine.record_search_result("lt", "掠"));
    ASSERT_TRUE(fixture.dict.has_candidate_preference("掠", "lve"));
    ASSERT_TRUE(!fixture.dict.has_candidate_preference("掠", "lue"));
    ASSERT_TRUE(fixture.engine.record_search_result("y;", "应"));
    ASSERT_TRUE(fixture.dict.has_candidate_preference("应", "ying"));
}

TEST(Shuangpin, manual_collision_order_preserves_canonical_key_across_query_paths) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize());
    ASSERT_TRUE(
        fixture.dict.replace_manual_candidate_order_and_save("lve", {{"掠", "lve", "lve"}}));

    const auto assert_lve_candidate = [](const cxxime::CandidatePage& page) {
        ASSERT_EQ(page.candidates.size(), static_cast<std::size_t>(1));
        ASSERT_EQ(page.candidates[0].text, "掠");
        ASSERT_EQ(page.candidates[0].input_code, "lve");
    };
    assert_lve_candidate(fixture.engine.translate_for_search("lt", 1));
    assert_lve_candidate(fixture.engine.translate_for_search("lt", 1));

    fixture.engine.set_wubi_dict(&fixture.dict);
    fixture.engine.switch_mode(cxxime::InputMode::MIXED);
    assert_lve_candidate(fixture.engine.translate_for_search("lt", 1));

    fixture.config.candidate_learning = true;
    fixture.engine.reload_config(fixture.config);
    ASSERT_TRUE(fixture.engine.record_search_result("lt", "掠"));
    ASSERT_TRUE(fixture.dict.has_candidate_preference("掠", "lve"));
    ASSERT_TRUE(!fixture.dict.has_candidate_preference("掠", "lt"));
}

TEST(Shuangpin, complete_topn_fast_path_preserves_manual_order) {
    ShuangpinFixture fixture;
    ASSERT_TRUE(fixture.initialize(true));
    ASSERT_TRUE(
        fixture.dict.replace_manual_candidate_order_and_save("hao", {{"号", "hao", "hao"}}));

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dict);
    translator.set_syllabifier(fixture.syllabifier.get());
    translator.set_short_cache(&fixture.dict.short_cache());
    translator.set_pinyin_scheme(cxxime::PinyinSchemeKind::kShuangpin);
    translator.set_sentence_composition_enabled(false);
    cxxime::QueryTrace trace;
    cxxime::TranslationRequest request;
    request.input = "hk";
    request.page_size = 1;
    request.trace = &trace;

    const cxxime::TranslationResult result = translator.translate(request);
    ASSERT_TRUE(trace.cache_hit);
    ASSERT_EQ(result.entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(result.entries[0].candidate.text, "号");
    ASSERT_EQ(result.entries[0].candidate.input_code, "hao");
}
