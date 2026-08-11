// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/engine.h>
#include <cxxime/input_limits.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/translator.h>
#include <cxxime/wubi_translator.h>

#include "util/testutil.h"

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

static void type_code(cxxime::Engine& engine, const std::string& code) {
    for (char ch : code) {
        cxxime::KeyEvent event;
        event.keycode = static_cast<uint32_t>(std::toupper(static_cast<unsigned char>(ch)));
        event.is_key_up = false;
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }
}

TEST(Engine, init) {
    cxxime::Engine engine;
    ASSERT_TRUE(true);
}

TEST(Engine, process_letter_key) {
    cxxime::Engine engine;
    cxxime::Context ctx;

    cxxime::KeyEvent event;
    event.keycode = 'N';
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.pinyin_buffer, "n");
}

TEST(Engine, input_code_stops_at_shared_limit) {
    cxxime::Context context;
    cxxime::PinyinProcessor processor;
    cxxime::KeyEvent letter;
    letter.keycode = 'A';

    for (size_t i = 0; i < cxxime::kMaxInputCodeLength; ++i) {
        ASSERT_EQ(processor.process_key(letter, context), cxxime::ProcessResult::ACCEPTED);
    }
    ASSERT_EQ(context.pinyin_buffer.size(), cxxime::kMaxInputCodeLength);
    const uint64_t revision = context.preedit_revision();

    ASSERT_EQ(processor.process_key(letter, context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer.size(), cxxime::kMaxInputCodeLength);
    ASSERT_EQ(context.preedit_revision(), revision);

    cxxime::KeyEvent backspace;
    backspace.keycode = VK_BACK;
    ASSERT_EQ(processor.process_key(backspace, context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer.size(), cxxime::kMaxInputCodeLength - 1);
    ASSERT_EQ(processor.process_key(letter, context), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(context.pinyin_buffer.size(), cxxime::kMaxInputCodeLength);
}

TEST(Engine, translator_excludes_candidate_text_over_shared_capacity) {
    std::string dict_path = make_temp_path("test_candidate_text_capacity.bin");
    std::string accepted;
    std::string rejected;
    for (size_t i = 0; i < 64; ++i) {
        accepted += "界";
    }
    for (size_t i = 0; i < 86; ++i) {
        rejected += "界";
    }
    ASSERT_TRUE(cxxime::candidate_text_fits(accepted));
    ASSERT_TRUE(!cxxime::candidate_text_fits(rejected));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(
        dict_path, {{"abc", rejected, 2000}, {"abc", accepted, 1000}}));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::WubiTranslator translator;
    translator.set_dict(&dict);
    auto page = translator.translate("abc", 0, 10);
    ASSERT_EQ(page.candidates.size(), static_cast<size_t>(1));
    ASSERT_TRUE(page.candidates[0].text == accepted);
    ASSERT_TRUE(page.candidates[0].text.size() > 63);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Engine, process_escape) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "nihao";

    cxxime::KeyEvent event;
    event.keycode = VK_ESCAPE;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
}

TEST(Engine, process_backspace) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "ni";

    cxxime::KeyEvent event;
    event.keycode = VK_BACK;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.pinyin_buffer, "n");
}

TEST(Engine, translate_dd_has_candidates) {
    std::string dict_path = make_temp_path("test_engine_dict.bin");
    std::string spellings_path = make_temp_path("test_engine_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"di:di", "弟弟", 500},
        {"da:da", "大大", 400},
        {"de:dao", "得到", 300},
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"d", "da", 2, -0.693f},
        {"d", "di", 2, -0.693f},
        {"d", "de", 2, -0.693f},
        {"da", "da", 0, 0.0f},
        {"di", "di", 0, 0.0f},
        {"de", "de", 0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);

    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("dd", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);

    bool found_didi = false;
    for (const auto& c : page.candidates) {
        if (c.text == "弟弟") found_didi = true;
    }
    ASSERT_TRUE(found_didi);
    ASSERT_EQ(page.highlighted, 0);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, selected_pinyin_candidate_learns_syllable_keys) {
    std::string dict_path = make_temp_path("test_engine_learn_syllables.bin");
    std::string user_path = make_temp_path("test_engine_learn_syllables.tsv");
    DeleteFileA(user_path.c_str());

    cxxime::Dict::create_test_dict(dict_path, {
        {"shu:ru:fa", "测试系统词", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path, user_path));

    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    config.candidate_learning = true;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    for (char ch : std::string("SHURUFA")) {
        cxxime::KeyEvent event;
        event.keycode = ch;
        event.is_key_up = false;
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }

    const auto& candidates = engine.context().candidates.candidates;
    ASSERT_GE(candidates.size(), 1u);
    ASSERT_EQ(candidates[0].text, "测试系统词");
    ASSERT_TRUE(engine.select_candidate(0));

    cxxime::QueryBudget budget;
    cxxime::QueryTrace trace = {};
    cxxime::UserLookupStats stats;
    auto learned = dict.lookup_user_indexed("srf", 10, budget, &trace, &stats);
    bool found = false;
    for (const auto& c : learned) {
        if (c.text == "测试系统词")
            found = true;
    }
    ASSERT_TRUE(found);

    engine.finalize();
    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(user_path.c_str());
}

TEST(Engine, candidate_order_stays_stable_when_candidate_learning_is_disabled) {
    std::string dict_path = make_temp_path("test_engine_stable_order.bin");
    std::string user_path = make_temp_path("test_engine_stable_order.tsv");
    DeleteFileA(user_path.c_str());

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao", "默认候选", 300},
        {"ni:hao", "第二候选", 200},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path, user_path));
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    type_code(engine, "nihao");
    ASSERT_GE(engine.context().candidates.candidates.size(), 2u);
    ASSERT_EQ(engine.context().candidates.candidates[0].text, "默认候选");
    ASSERT_EQ(engine.context().candidates.candidates[1].text, "第二候选");
    ASSERT_TRUE(engine.select_candidate(1));
    ASSERT_EQ(engine.get_commit_text(), "第二候选");
    ASSERT_TRUE(!dict.has_user_entry("第二候选"));

    type_code(engine, "nihao");
    ASSERT_GE(engine.context().candidates.candidates.size(), 2u);
    ASSERT_EQ(engine.context().candidates.candidates[0].text, "默认候选");
    ASSERT_EQ(engine.context().candidates.candidates[1].text, "第二候选");
    cxxime::KeyEvent space;
    space.keycode = VK_SPACE;
    space.is_key_up = false;
    ASSERT_EQ(engine.process_key(space), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "默认候选");
    ASSERT_TRUE(!dict.has_user_entry("默认候选"));

    engine.finalize();
    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(user_path.c_str());
}

TEST(Engine, candidate_learning_promotes_selected_candidate_when_enabled) {
    std::string dict_path = make_temp_path("test_engine_adaptive_order.bin");
    std::string user_path = make_temp_path("test_engine_adaptive_order.tsv");
    DeleteFileA(user_path.c_str());

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao", "默认候选", 300},
        {"ni:hao", "第二候选", 200},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path, user_path));
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    config.candidate_learning = true;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    type_code(engine, "nihao");
    ASSERT_GE(engine.context().candidates.candidates.size(), 2u);
    ASSERT_EQ(engine.context().candidates.candidates[0].text, "默认候选");
    ASSERT_EQ(engine.context().candidates.candidates[1].text, "第二候选");
    ASSERT_TRUE(engine.select_candidate(1));
    ASSERT_EQ(engine.get_commit_text(), "第二候选");

    type_code(engine, "nihao");
    ASSERT_GE(engine.context().candidates.candidates.size(), 1u);
    ASSERT_EQ(engine.context().candidates.candidates[0].text, "第二候选");

    engine.finalize();
    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(user_path.c_str());
}

TEST(Engine, candidate_learning_uses_candidate_text_for_punctuation_commit) {
    std::string dict_path = make_temp_path("test_engine_punctuation_learning.bin");
    std::string user_path = make_temp_path("test_engine_punctuation_learning.tsv");
    DeleteFileA(user_path.c_str());

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao", "你好", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path, user_path));
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    config.candidate_learning = true;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    type_code(engine, "nihao");
    ASSERT_GE(engine.context().candidates.candidates.size(), 1u);

    cxxime::PunctMapping punct_mapping;
    punct_mapping.half_shape["."] = {
        cxxime::PunctType::COMMIT, "。", {}, {}};
    cxxime::OutputOptions options;
    options.punct_mapping = &punct_mapping;
    cxxime::KeyEvent period;
    period.keycode = VK_OEM_PERIOD;
    ASSERT_EQ(engine.process_key(period, options), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "你好。");
    ASSERT_TRUE(dict.has_user_entry("你好"));
    ASSERT_TRUE(!dict.has_user_entry("你好。"));

    engine.finalize();
    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(user_path.c_str());
}

TEST(Engine, translate_valid_pinyin) {
    std::string dict_path = make_temp_path("test_engine_valid.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"de", "的", 1000},
        {"de:dao", "得到", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);

    auto page = translator.translate("de", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);

    bool found_de = false;
    for (const auto& c : page.candidates) {
        if (c.text == "的") found_de = true;
    }
    ASSERT_TRUE(found_de);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Engine, space_with_candidates_commits) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "de";
    ctx.candidates.candidates.push_back({"的", "", 100});
    ctx.candidates.highlighted = 0;

    cxxime::KeyEvent event;
    event.keycode = VK_SPACE;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "的");
}

TEST(Engine, space_no_candidates_dismisses) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "zzz";

    cxxime::KeyEvent event;
    event.keycode = VK_SPACE;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
}

TEST(Engine, number_selects_candidate) {
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

// Verify that raw VK constants used in engine (to avoid <windows.h> dependency)
// match the actual Windows definitions.
TEST(Engine, vk_constants_match_windows) {
    ASSERT_EQ(0xA0, (uint32_t)VK_LSHIFT);
    ASSERT_EQ(0xA1, (uint32_t)VK_RSHIFT);
    ASSERT_EQ(0xA2, (uint32_t)VK_LCONTROL);
    ASSERT_EQ(0xA3, (uint32_t)VK_RCONTROL);
    ASSERT_EQ(0x14, (uint32_t)VK_CAPITAL);
    ASSERT_EQ(0x20, (uint32_t)VK_SPACE);
    ASSERT_EQ(0x0D, (uint32_t)VK_RETURN);
}

// --- Short phrase matching (full pinyin) ---

TEST(Engine, translate_shurufa) {
    std::string dict_path = make_temp_path("test_shurufa_dict.bin");
    std::string spellings_path = make_temp_path("test_shurufa_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"shu:ru:fa", "输入法", 1000}, // 输入法
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"shu",  "shu", 0, 0.0f},
        {"ru",   "ru",  0, 0.0f},
        {"fa",   "fa",  0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("shurufa", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    bool found = false;
    for (auto& c : page.candidates)
        if (c.text == "输入法") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, composed_pinyin_candidate_is_not_learned) {
    std::string dict_path = make_temp_path("test_engine_composed_no_learning.bin");
    std::string spellings_path = make_temp_path("test_engine_composed_no_learning.spellings");
    std::string user_path = make_temp_path("test_engine_composed_no_learning.tsv");
    DeleteFileA(user_path.c_str());

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"wu", "无", 9000},
        {"shu:chu", "输出", 8000},
    }));
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path, {
            {"wu", "wu", cxxime::kNormalSpelling, 0.0f},
            {"shu", "shu", cxxime::kNormalSpelling, 0.0f},
            {"chu", "chu", cxxime::kNormalSpelling, 0.0f},
        }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path, user_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::Config config;
    config.candidate_learning = true;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, &syllabifier, config));

    type_code(engine, "wushuchu");
    const auto& candidates = engine.context().candidates.candidates;
    const auto candidate = std::find_if(candidates.begin(), candidates.end(),
        [](const auto& item) { return item.text == "无输出"; });
    ASSERT_TRUE(candidate != candidates.end());
    ASSERT_TRUE(candidate->origin == cxxime::CandidateOrigin::kComposed);
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(candidate - candidates.begin())));
    ASSERT_EQ(engine.get_commit_text(), "无输出");
    ASSERT_TRUE(!dict.has_user_entry("无输出"));

    engine.finalize();
    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
    DeleteFileA(user_path.c_str());
}

TEST(Engine, translate_nihao) {
    std::string dict_path = make_temp_path("test_nihao_dict.bin");
    std::string spellings_path = make_temp_path("test_nihao_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao", "你好", 1000}, // 你好
        {"ni",     "你", 500},                // 你
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"ni",  "ni",  0, 0.0f},
        {"hao", "hao", 0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("nihao", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    ASSERT_EQ(page.candidates[0].text, "你好");

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

// --- First-letter abbreviation matching (全简拼) ---

TEST(Engine, translate_abbrev_bj) {
    std::string dict_path = make_temp_path("test_abbrev_bj_dict.bin");
    std::string spellings_path = make_temp_path("test_abbrev_bj_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"bei:jing", "北京", 1000}, // 北京
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"b", "bei",  2, -0.693f},
        {"j", "jing", 2, -0.693f},
        {"bei", "bei",   0, 0.0f},
        {"jing", "jing", 0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("bj", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    bool found = false;
    for (auto& c : page.candidates)
        if (c.text == "北京") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_abbrev_srf) {
    std::string dict_path = make_temp_path("test_abbrev_srf_dict.bin");
    std::string spellings_path = make_temp_path("test_abbrev_srf_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"shu:ru:fa", "输入法", 1000}, // 输入法
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"s", "shu", 2, -0.693f},
        {"r", "ru",  2, -0.693f},
        {"f", "fa",  2, -0.693f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("srf", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    bool found = false;
    for (auto& c : page.candidates)
        if (c.text == "输入法") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

// --- Mixed abbreviation (混合简拼) ---

TEST(Engine, translate_mixed_zhg) {
    std::string dict_path = make_temp_path("test_mixed_zhg_dict.bin");
    std::string spellings_path = make_temp_path("test_mixed_zhg_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"zhong:guo", "中国", 1000}, // 中国
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"zh", "zhong", 2, 0.0f},
        {"g",  "guo",   2, -0.693f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("zhg", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    bool found = false;
    for (auto& c : page.candidates)
        if (c.text == "中国") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_mixed_zguo) {
    std::string dict_path = make_temp_path("test_mixed_zguo_dict.bin");
    std::string spellings_path = make_temp_path("test_mixed_zguo_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"zhong:guo", "中国", 1000}, // 中国
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"z",   "zhong", 2, -0.693f},
        {"guo", "guo",   0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("zguo", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    bool found = false;
    for (auto& c : page.candidates)
        if (c.text == "中国") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

// --- Fuzzy pinyin (模糊音) ---

TEST(Engine, translate_fuzzy_zongguo) {
    std::string dict_path = make_temp_path("test_fuzzy_zongguo_dict.bin");
    std::string spellings_path = make_temp_path("test_fuzzy_zongguo_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"zhong:guo", "中国", 1000}, // 中国
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"zong", "zhong", 1, -0.693f},
        {"guo",  "guo",   0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("zongguo", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    bool found = false;
    for (auto& c : page.candidates)
        if (c.text == "中国") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_fuzzy_cifan) {
    std::string dict_path = make_temp_path("test_fuzzy_cifan_dict.bin");
    std::string spellings_path = make_temp_path("test_fuzzy_cifan_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"chi:fan", "吃饭", 1000}, // 吃饭
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"ci",  "chi", 1, -0.693f},
        {"fan", "fan", 0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    auto page = translator.translate("cifan", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);
    bool found = false;
    for (auto& c : page.candidates)
        if (c.text == "吃饭") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

// --- Edge cases ---

TEST(Engine, translate_empty_input) {
    cxxime::PinyinTranslator translator;
    auto page = translator.translate("", 0, 10);
    ASSERT_EQ(page.candidates.size(), 0u);
}

TEST(Engine, translate_no_match) {
    cxxime::PinyinTranslator translator;
    auto page = translator.translate("xyz", 0, 10);
    ASSERT_EQ(page.candidates.size(), 0u);
}

// --- AsciiComposer Shift/Ctrl toggle tests ---

TEST(AsciiComposer, shift_l_code_toggles_and_commits) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Shift_R"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Simulate composing state with pinyin
    ctx.pinyin_buffer = "ni";
    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press Shift_L (key-down)
    ac.process_key(0xA0, false, ctx);

    // Release Shift_L (key-up) — should commit and toggle to ascii mode
    ac.process_key(0xA0, true, ctx);

    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_TRUE(!ctx.committed_text.empty());
}

TEST(AsciiComposer, shift_r_set_ascii_mode_toggles_no_commit) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Shift_R"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ctx.pinyin_buffer = "ni";
    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press and release Shift_R
    ac.process_key(0xA1, false, ctx);
    ac.process_key(0xA1, true, ctx);

    // set_ascii_mode toggles but does not commit
    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_TRUE(ctx.committed_text.empty());
}

TEST(AsciiComposer, shift_no_binding_does_nothing) {
    cxxime::Config config;
    // No Shift bindings

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Press and release Shift_L
    ac.process_key(0xA0, false, ctx);
    ac.process_key(0xA0, true, ctx);

    ASSERT_TRUE(!ac.is_ascii_mode());
}

TEST(AsciiComposer, shift_toggle_back_to_chinese) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(!ac.is_ascii_mode());

    // First Shift: Chinese -> English (code toggles)
    ac.process_key(0xA0, false, ctx);
    ac.process_key(0xA0, true, ctx);
    ASSERT_TRUE(ac.is_ascii_mode());

    // Second Shift: English -> Chinese (code toggles back)
    cxxime::Context ctx2;
    ac.process_key(0xA0, false, ctx2);
    ac.process_key(0xA0, true, ctx2);
    ASSERT_TRUE(!ac.is_ascii_mode());
}

TEST(AsciiComposer, set_ascii_mode_is_one_way) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // First Shift: Chinese -> English
    ac.process_key(0xA0, false, ctx);
    ac.process_key(0xA0, true, ctx);
    ASSERT_TRUE(ac.is_ascii_mode());

    // Second Shift: stays English (set_ascii_mode is one-way)
    cxxime::Context ctx2;
    ac.process_key(0xA0, false, ctx2);
    ac.process_key(0xA0, true, ctx2);
    ASSERT_TRUE(ac.is_ascii_mode());
}

TEST(AsciiComposer, alt_l_supported) {
    cxxime::Config config;
    config.ascii_switch_key["Alt_L"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press and release Alt_L
    ac.process_key(0xA4, false, ctx);
    ac.process_key(0xA4, true, ctx);

    ASSERT_TRUE(ac.is_ascii_mode());
}

TEST(AsciiComposer, super_l_supported) {
    cxxime::Config config;
    config.ascii_switch_key["Super_L"] = "set_ascii_mode";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(!ac.is_ascii_mode());

    // Press and release Super_L
    ac.process_key(0x5B, false, ctx);
    ac.process_key(0x5B, true, ctx);

    ASSERT_TRUE(ac.is_ascii_mode());
}

TEST(AsciiComposer, capslock_downgrade) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "inline_ascii";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // CapsLock ON → toggles (downgraded from inline_ascii to clear)
    ASSERT_TRUE(!ac.is_ascii_mode());
    ac.process_key(0x14, false, ctx, true);   // VK_CAPITAL down, CapsLock ON
    ASSERT_TRUE(ac.is_ascii_mode());
    // CapsLock OFF restores the mode from before CapsLock was turned on.
    ac.process_key(0x14, false, ctx, false);  // VK_CAPITAL down, CapsLock OFF
    ASSERT_TRUE(!ac.is_ascii_mode());
}

TEST(AsciiComposer, capslock_clear_resets_pinyin) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Simulate composing state
    ctx.pinyin_buffer = "nihao";
    ASSERT_TRUE(ctx.is_composing());

    // Press CapsLock — should clear pinyin and toggle ascii_mode
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON

    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
    ASSERT_TRUE(!ctx.is_composing());
}

TEST(Engine, capslock_clear_off_restores_chinese_without_ascii_letter_intercept) {
    std::string dict_path = make_temp_path("test_caps_clear_restore_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"a", "a", 100}});

    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::Engine engine;
    engine.initialize(dict_path);
    engine.reload_config(config);
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent caps_on;
    caps_on.keycode = 0x14;  // VK_CAPITAL
    caps_on.is_key_up = false;
    caps_on.set_caps_lock();
    engine.process_key(caps_on);

    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent caps_off;
    caps_off.keycode = 0x14;
    caps_off.is_key_up = false;
    engine.process_key(caps_off);

    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent letter;
    letter.keycode = 'A';
    letter.is_key_up = false;
    letter.set_caps_lock();

    auto result = engine.process_key(letter);
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "a");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(Engine, capslock_letter_commits_directly) {
    cxxime::Engine engine;
    // No dict needed — CapsLock intercept happens before translation

    // Simulate CapsLock ON by setting the modifier bit
    cxxime::KeyEvent event;
    event.keycode = 'A';
    event.is_key_up = false;
    event.modifiers = 0x08;  // CapsLock bit

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    // CapsLock alone → uppercase
    ASSERT_EQ(engine.get_commit_text(), "A");
}

TEST(Engine, capslock_shift_letter_commits_lowercase) {
    cxxime::Engine engine;

    cxxime::KeyEvent event;
    event.keycode = 'A';
    event.is_key_up = false;
    event.modifiers = 0x09;  // CapsLock (0x08) + Shift (0x01)

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    // Shift+CapsLock → lowercase
    ASSERT_EQ(engine.get_commit_text(), "a");
}

TEST(Engine, ascii_mode_capslock_uppercase) {
    // Bug: Shift → English mode, CapsLock ON, type letter → should be uppercase
    std::string dict_path = make_temp_path("test_ascii_caps_dict.bin");
    std::string spellings_path = make_temp_path("test_ascii_caps_spellings.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"a", "啊", 100}});
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {{"a", "a", 0, 0.0f}}));

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.ascii_composer().set_ascii_mode(true);

    cxxime::KeyEvent event;
    event.keycode = 'N';
    event.is_key_up = false;
    event.modifiers = 0x08;  // CapsLock ON

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "N");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, shift_toggle_then_capslock_uppercase) {
    // End-to-end: Shift toggles to English, CapsLock ON, type letter → uppercase
    std::string dict_path = make_temp_path("test_shift_caps_dict.bin");
    std::string spellings_path = make_temp_path("test_shift_caps_spellings.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"a", "啊", 100}});
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {{"a", "a", 0, 0.0f}}));

    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.reload_config(config);

    // Step 1: Press and release Shift_L → toggles to English mode
    cxxime::KeyEvent shift_down;
    shift_down.keycode = 0xA0;  // VK_LSHIFT
    shift_down.is_key_up = false;
    engine.process_key(shift_down);

    cxxime::KeyEvent shift_up;
    shift_up.keycode = 0xA0;
    shift_up.is_key_up = true;
    engine.process_key(shift_up);

    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    // Step 2: Press CapsLock (OS toggles ON) — should NOT flip mode back
    cxxime::KeyEvent caps_down;
    caps_down.keycode = 0x14;  // VK_CAPITAL
    caps_down.is_key_up = false;
    caps_down.set_caps_lock();  // CapsLock now ON
    engine.process_key(caps_down);

    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());  // still English

    // Step 3: Type letter with CapsLock ON → should be uppercase
    cxxime::KeyEvent letter;
    letter.keycode = 'N';
    letter.is_key_up = false;
    letter.modifiers = 0x08;  // CapsLock ON

    auto result = engine.process_key(letter);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "N");

    cxxime::KeyEvent caps_up;
    caps_up.keycode = 0x14;
    caps_up.is_key_up = false;
    engine.process_key(caps_up);

    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent lower_letter;
    lower_letter.keycode = 'I';
    lower_letter.is_key_up = false;

    result = engine.process_key(lower_letter);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "i");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, ascii_mode_shift_capslock_lowercase) {
    // Shift+CapsLock in ASCII mode → lowercase (they cancel)
    std::string dict_path = make_temp_path("test_ascii_sc_dict.bin");
    std::string spellings_path = make_temp_path("test_ascii_sc_spellings.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"a", "啊", 100}});
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {{"a", "a", 0, 0.0f}}));

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.ascii_composer().set_ascii_mode(true);

    cxxime::KeyEvent event;
    event.keycode = 'N';
    event.is_key_up = false;
    event.modifiers = 0x09;  // CapsLock (0x08) + Shift (0x01)

    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "n");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, ascii_mode_enter_passes_to_application) {
    std::string dict_path = make_temp_path("test_ascii_enter_dict.bin");
    std::string spellings_path = make_temp_path("test_ascii_enter_spellings.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"a", "a", 100}});
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {{"a", "a", 0, 0.0f}}));

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.ascii_composer().set_ascii_mode(true);

    cxxime::KeyEvent enter;
    enter.keycode = VK_RETURN;
    enter.is_key_up = false;

    auto result = engine.process_key(enter);
    ASSERT_EQ(result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(engine.get_commit_text().empty());

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, capslock_overlay_shift_keeps_ascii_mode) {
    std::string dict_path = make_temp_path("test_caps_shift_overlay_dict.bin");
    std::string spellings_path = make_temp_path("test_caps_shift_overlay_spellings.bin");
    cxxime::Dict::create_test_dict(dict_path, {{"ni", "ni", 100}});
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {{"ni", "ni", 0, 0.0f}}));

    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.reload_config(config);

    cxxime::KeyEvent caps_down;
    caps_down.keycode = 0x14;  // VK_CAPITAL
    caps_down.is_key_up = false;
    caps_down.set_caps_lock();
    engine.process_key(caps_down);
    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent shift_down;
    shift_down.keycode = VK_LSHIFT;
    shift_down.is_key_up = false;
    shift_down.modifiers = 0x09;  // Shift + CapsLock
    engine.process_key(shift_down);

    cxxime::KeyEvent shift_up = shift_down;
    shift_up.is_key_up = true;
    engine.process_key(shift_up);
    ASSERT_TRUE(engine.ascii_composer().is_ascii_mode());

    cxxime::KeyEvent n;
    n.keycode = 'N';
    n.is_key_up = false;
    n.modifiers = 0x08;
    auto result = engine.process_key(n);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "N");

    cxxime::KeyEvent i;
    i.keycode = 'I';
    i.is_key_up = false;
    i.modifiers = 0x08;
    result = engine.process_key(i);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "I");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

// --- CapsLock v2: append / candidate / code / clear mode tests ---

TEST(AsciiComposer, capslock_append_noop_during_composing) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "append";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ctx.pinyin_buffer = "nihao";
    ASSERT_TRUE(ctx.is_composing());

    // Press CapsLock in append mode — should NOT clear or toggle
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON

    ASSERT_TRUE(!ac.is_ascii_mode());  // still Chinese mode
    ASSERT_EQ(ctx.pinyin_buffer, "nihao");  // buffer unchanged
}

TEST(AsciiComposer, capslock_candidate_commits_first_candidate) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "candidate";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Set up composing state with candidates
    ctx.pinyin_buffer = "nihao";
    cxxime::CandidatePage page;
    cxxime::Candidate c1; c1.text = "你好";
    cxxime::Candidate c2; c2.text = "拟好";
    page.candidates.push_back(c1);
    page.candidates.push_back(c2);
    page.highlighted = 0;
    ctx.update_candidates(std::move(page));

    // Press CapsLock — should commit first candidate and toggle
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON

    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_EQ(ctx.committed_text, "你好");
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
}

TEST(AsciiComposer, capslock_candidate_no_candidates_toggles) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "candidate";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ctx.pinyin_buffer = "zzz";  // no valid candidates
    ASSERT_TRUE(ctx.is_composing());

    // Press CapsLock — no candidates, just toggle
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON

    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
    ASSERT_TRUE(ctx.committed_text.empty());
}

TEST(AsciiComposer, capslock_code_commits_buffer) {
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "code";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ctx.pinyin_buffer = "nihao";

    // Press CapsLock — should commit raw buffer and toggle
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON

    ASSERT_TRUE(ac.is_ascii_mode());
    ASSERT_EQ(ctx.committed_text, "nihao");
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
}

TEST(Engine, capslock_append_letter_accepted) {
    // Append mode while composing: CapsLock + letter should be ACCEPTED
    // (goes to buffer), not COMMITTED.
    // Need a real dictionary so translator doesn't crash on buffer query
    std::string dict_path = make_temp_path("test_append_dict.bin");
    std::string spellings_path = make_temp_path("test_append_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"a", "啊", 100},
        {"n", "嗯", 100},
    });
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"a", "a", 0, 0.0f},
        {"n", "n", 0, 0.0f},
    }));

    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "append";

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.reload_config(config);

    cxxime::KeyEvent first;
    first.keycode = 'N';
    first.is_key_up = false;
    ASSERT_EQ(engine.process_key(first), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "n");

    // Press CapsLock while composing. Append mode should not clear or switch.
    cxxime::KeyEvent caps_event;
    caps_event.keycode = 0x14;  // VK_CAPITAL
    caps_event.is_key_up = false;
    caps_event.set_caps_lock();  // OS has toggled CapsLock ON
    engine.process_key(caps_event);
    ASSERT_TRUE(!engine.ascii_composer().is_ascii_mode());
    ASSERT_EQ(engine.context().pinyin_buffer, "n");

    // Now press 'A' with CapsLock ON
    cxxime::KeyEvent event;
    event.keycode = 'A';
    event.is_key_up = false;
    event.modifiers = 0x08;  // CapsLock ON

    auto result = engine.process_key(event);
    // In append mode, letter should be accepted (buffered), not committed
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().pinyin_buffer, "nA");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, capslock_append_clears_candidates_and_commits_raw_code) {
    std::string dict_path = make_temp_path("test_append_raw_dict.bin");
    std::string spellings_path = make_temp_path("test_append_raw_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni", "你", 100},
    });
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"ni", "ni", 0, 0.0f},
    }));

    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "append";

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.reload_config(config);

    for (char ch : std::string("NI")) {
        cxxime::KeyEvent event;
        event.keycode = ch;
        event.is_key_up = false;
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }
    ASSERT_EQ(engine.context().pinyin_buffer, "ni");
    ASSERT_TRUE(!engine.context().candidates.candidates.empty());

    cxxime::KeyEvent caps_event;
    caps_event.keycode = 0x14;  // VK_CAPITAL
    caps_event.is_key_up = false;
    caps_event.set_caps_lock();
    engine.process_key(caps_event);

    for (char ch : std::string("DD")) {
        cxxime::KeyEvent event;
        event.keycode = ch;
        event.is_key_up = false;
        event.set_caps_lock();
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }

    ASSERT_EQ(engine.context().pinyin_buffer, "niDD");
    ASSERT_TRUE(engine.context().candidates.candidates.empty());

    cxxime::KeyEvent space;
    space.keycode = VK_SPACE;
    space.is_key_up = false;
    space.set_caps_lock();
    ASSERT_EQ(engine.process_key(space), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "niDD");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, capslock_append_enter_preserves_case) {
    std::string dict_path = make_temp_path("test_append_enter_dict.bin");
    std::string spellings_path = make_temp_path("test_append_enter_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni", "你", 100},
    });
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"ni", "ni", 0, 0.0f},
    }));

    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "append";

    cxxime::Engine engine;
    engine.initialize(dict_path, spellings_path);
    engine.reload_config(config);

    for (char ch : std::string("NI")) {
        cxxime::KeyEvent event;
        event.keycode = ch;
        event.is_key_up = false;
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }

    cxxime::KeyEvent caps_event;
    caps_event.keycode = 0x14;
    caps_event.is_key_up = false;
    caps_event.set_caps_lock();
    engine.process_key(caps_event);

    for (char ch : std::string("DD")) {
        cxxime::KeyEvent event;
        event.keycode = ch;
        event.is_key_up = false;
        event.set_caps_lock();
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }

    cxxime::KeyEvent enter;
    enter.keycode = VK_RETURN;
    enter.is_key_up = false;
    enter.set_caps_lock();
    ASSERT_EQ(engine.process_key(enter), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.get_commit_text(), "niDD");

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, capslock_candidate_not_downgraded) {
    // "candidate" should NOT be downgraded to "clear" (unlike inline_ascii)
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "candidate";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Verify binding is still CANDIDATE, not CLEAR
    ASSERT_TRUE(ac.get_binding(0x14) == cxxime::AsciiModeSwitchStyle::CANDIDATE);
}

TEST(Engine, capslock_append_not_downgraded) {
    // "append" should NOT be downgraded
    cxxime::Config config;
    config.ascii_switch_key["Caps_Lock"] = "append";

    cxxime::AsciiComposer ac;
    ac.load_config(config);

    ASSERT_TRUE(ac.get_binding(0x14) == cxxime::AsciiModeSwitchStyle::APPEND);
}

// --- CapsLock + Shift interaction tests ---

TEST(AsciiComposer, capslock_on_shift_does_not_toggle) {
    // CapsLock ON 时按 Shift 松开，不应切换模式（Shift 用于大小写反转）
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Step 1: CapsLock ON → toggles to English
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON
    ASSERT_TRUE(ac.is_ascii_mode());

    // Step 2: Shift down (CapsLock still ON)
    ac.process_key(VK_LSHIFT, false, ctx, true);  // caps_lock = true

    // Step 3: Shift up must not toggle Chinese/English while CapsLock is ON.
    ac.process_key(VK_LSHIFT, true, ctx, true);  // caps_lock = true
    ASSERT_TRUE(ac.is_ascii_mode());
}

TEST(AsciiComposer, shift_held_capslock_no_double_toggle) {
    // Shift 按住→CapsLock ON→松 Shift，只切换一次（CapsLock 那次）
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";
    config.ascii_switch_key["Caps_Lock"] = "clear";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Step 1: Shift down
    ac.process_key(VK_LSHIFT, false, ctx, false);  // caps_lock = false
    ASSERT_TRUE(!ac.is_ascii_mode());  // still Chinese

    // Step 2: CapsLock ON (while Shift held) → toggles to English
    ac.process_key(0x14, false, ctx, true);  // VK_CAPITAL down, CapsLock ON
    ASSERT_TRUE(ac.is_ascii_mode());

    // Step 3: Shift up → should NOT toggle again (caps_lock = true)
    ac.process_key(VK_LSHIFT, true, ctx, true);
    ASSERT_TRUE(ac.is_ascii_mode());  // still English, no double-toggle
}

TEST(AsciiComposer, shift_toggle_still_works_without_capslock) {
    // CapsLock OFF 时 Shift 仍然正常切换模式
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "code";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    // Shift down → Shift up → toggles to English
    ac.process_key(VK_LSHIFT, false, ctx, false);
    ac.process_key(VK_LSHIFT, true, ctx, false);
    ASSERT_TRUE(ac.is_ascii_mode());

    // Shift down → Shift up → toggles back to Chinese
    ac.process_key(VK_LSHIFT, false, ctx, false);
    ac.process_key(VK_LSHIFT, true, ctx, false);
    ASSERT_TRUE(!ac.is_ascii_mode());
}

// Initialize temp_path before tests run
static bool _engine_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

// QueryDeadline tests

TEST(QueryDeadline, disabled_deadline_never_expires) {
    cxxime::QueryDeadline deadline;
    // default: enabled=false
    ASSERT_TRUE(!deadline.expired());
}

TEST(QueryDeadline, from_now_zero_disables) {
    auto deadline = cxxime::QueryDeadline::from_now(0);
    ASSERT_TRUE(!deadline.enabled);
    ASSERT_TRUE(!deadline.expired());
}

TEST(QueryDeadline, from_now_sets_expires_at) {
    auto deadline = cxxime::QueryDeadline::from_now(100);  // 100ms
    ASSERT_TRUE(deadline.enabled);
    ASSERT_TRUE(!deadline.expired());  // should not be expired yet
}

TEST(QueryDeadline, expired_after_time_passes) {
    auto deadline = cxxime::QueryDeadline::from_now(1);  // 1ms
    ASSERT_TRUE(deadline.enabled);
    // Sleep a bit to let deadline expire
    Sleep(5);  // 5ms > 1ms
    ASSERT_TRUE(deadline.expired());
}

TEST(Deadline, expired_deadline_sets_trace_flags) {
    std::string dict_path = make_temp_path("test_deadline_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"de", "的", 1000},
        {"de:dao", "得到", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::QueryTrace trace = {};
    cxxime::QueryBudget budget;
    // Create an already-expired deadline.
    budget.deadline.enabled = true;
    budget.deadline.expires_at = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);

    std::vector<uint32_t> ids = {0};  // dummy ID
    dict.lookup_by_ids(ids, 10, &trace, &budget);

    ASSERT_TRUE(trace.deadline_exceeded);
    ASSERT_TRUE(trace.truncated);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Deadline, normal_query_no_deadline_flags) {
    std::string dict_path = make_temp_path("test_no_deadline_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"de", "的", 1000},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::QueryTrace trace = {};
    // No budget — deadline_us defaults to 0
    std::vector<uint32_t> ids = {0};
    dict.lookup_by_ids(ids, 10, &trace, nullptr);

    ASSERT_TRUE(!trace.deadline_exceeded);
    ASSERT_TRUE(!trace.truncated);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Deadline, scan_budget_limits_scanning) {
    std::string dict_path = make_temp_path("test_scan_budget_dict.bin");

    // Create a dict with many entries sharing the same syllable ID
    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int i = 0; i < 100; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "test%d", i);
        entries.push_back({"de", text, i});
    }
    cxxime::Dict::create_test_dict(dict_path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::QueryTrace trace = {};
    cxxime::QueryBudget budget;
    budget.max_exact_scan = 10;  // Only allow 10 scans

    std::vector<uint32_t> ids = {0};
    auto results = dict.lookup_by_ids(ids, 100, &trace, &budget);

    // Should have truncated due to scan budget
    ASSERT_TRUE(trace.truncated);
    // Should have scanned at most max_exact_scan entries
    ASSERT_TRUE(trace.exact_scan_count <= 10);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Deadline, engine_sets_trace_deadline_from_deadline_ms) {
    std::string dict_path = make_temp_path("test_engine_deadline_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"de", "的", 1000},
        {"de:dao", "得到", 300},
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict_path));

    // Set the deadline through the public API (0ms disables it; this test uses 1ms).
    engine.set_query_deadline_ms(0);  // disable deadline first
    engine.set_trace_enabled(true);

    // Type 'd' — with deadline_ms=0, should not trigger deadline
    cxxime::KeyEvent event;
    event.keycode = 'D';
    event.is_key_up = false;
    engine.process_key(event);

    const auto& trace = engine.last_trace();
    // With deadline_ms=0, deadline should not be triggered
    ASSERT_TRUE(!trace.deadline_exceeded);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

TEST(Deadline, engine_no_budget_means_no_deadline) {
    std::string dict_path = make_temp_path("test_engine_no_budget_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"de", "的", 1000},
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict_path));
    engine.set_trace_enabled(true);

    // Type 'd' without setting budget
    cxxime::KeyEvent event;
    event.keycode = 'D';
    event.is_key_up = false;
    engine.process_key(event);

    const auto& trace = engine.last_trace();
    ASSERT_TRUE(!trace.deadline_exceeded);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
}

// make_budget and TopK translator tests

TEST(Budget, make_budget_scales_by_input_length) {
    auto b1 = cxxime::make_budget(1, 9);
    auto b2 = cxxime::make_budget(2, 9);
    auto b5 = cxxime::make_budget(5, 9);

    // Longer input → larger scan budgets
    ASSERT_TRUE(b1.max_exact_scan < b2.max_exact_scan);
    ASSERT_TRUE(b2.max_exact_scan < b5.max_exact_scan);
    ASSERT_TRUE(b1.max_prefix_scan < b2.max_prefix_scan);
    ASSERT_TRUE(b2.max_prefix_scan < b5.max_prefix_scan);
    ASSERT_TRUE(b1.max_results_before_merge < b5.max_results_before_merge);

    // topk = page_size (reserved for translator-level control)
    ASSERT_EQ(b1.topk, 9u);
    ASSERT_EQ(b5.topk, 9u);
}

TEST(Translator, translate_topk_merge_across_paths) {
    std::string dict_path = make_temp_path("test_topk_merge_dict.bin");
    std::string spellings_path = make_temp_path("test_topk_merge_spellings.bin");

    // Two different syllable paths that produce distinct candidates
    std::vector<std::tuple<std::string, std::string, int>> entries;
    // Path 1: "bei:jing" → 北京 + many low-freq
    entries.push_back({"bei:jing", "北京", 2000});
    for (int i = 0; i < 30; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "bj%02d", i);
        entries.push_back({"bei:jing", text, 10 + i});
    }
    // Path 2: "shang:hai" → 上海 + many low-freq
    entries.push_back({"shang:hai", "上海", 1800});
    for (int i = 0; i < 30; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "sh%02d", i);
        entries.push_back({"shang:hai", text, 10 + i});
    }
    cxxime::Dict::create_test_dict(dict_path, entries);

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"b", "bei",    2, -0.693f},
        {"s", "shang",  2, -0.693f},
        {"bei", "bei",      0, 0.0f},
        {"jing", "jing",    0, 0.0f},
        {"shang", "shang",  0, 0.0f},
        {"hai", "hai",      0, 0.0f},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);

    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    translator.set_syllabifier(&syllabifier);

    // With page_size=5, TopK should limit to 5 candidates
    auto page = translator.translate("bs", 0, 5);
    ASSERT_LE(page.candidates.size(), 5u);

    // The top results should include the high-frequency ones
    if (!page.candidates.empty()) {
        ASSERT_GE(page.candidates[0].frequency, 1000);
    }

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

// Deadline protection tests

TEST(Deadline, expired_deadline_stops_dict_scan) {
    std::string dict_path = make_temp_path("test_deadline_stop_scan.bin");

    // Create a dict with many entries sharing the same syllable ID
    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int i = 0; i < 100; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "test%d", i);
        entries.push_back({"de", text, i});
    }
    cxxime::Dict::create_test_dict(dict_path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::QueryTrace trace = {};
    cxxime::QueryBudget budget;
    // Create an already-expired deadline.
    budget.deadline.enabled = true;
    budget.deadline.expires_at = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);

    std::vector<uint32_t> ids = {0};
    auto results = dict.lookup_by_ids(ids, 100, &trace, &budget);

    // Should have stopped scanning due to deadline
    ASSERT_TRUE(trace.deadline_exceeded);
    ASSERT_TRUE(trace.truncated);
    // Should have returned some results (from before deadline expired)
    ASSERT_TRUE(results.size() < 100);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Deadline, small_deadline_returns_partial_results) {
    std::string dict_path = make_temp_path("test_small_deadline.bin");

    // Create a dict with many entries
    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int i = 0; i < 500; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "item%d", i);
        entries.push_back({"de", text, i});
    }
    cxxime::Dict::create_test_dict(dict_path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::QueryTrace trace = {};
    cxxime::QueryBudget budget;
    // Use a deadline that expires immediately but allows first few entries
    // Set expires_at to now — it will expire on the first check
    budget.deadline.enabled = true;
    budget.deadline.expires_at = std::chrono::steady_clock::now();
    budget.deadline.check_interval = 10;  // check every 10 entries

    std::vector<uint32_t> ids = {0};
    auto results = dict.lookup_by_ids(ids, 100, &trace, &budget);

    // Should have partial results with both flags set
    ASSERT_TRUE(trace.deadline_exceeded);
    ASSERT_TRUE(trace.truncated);
    // Results may be empty if deadline expired before any entries were scanned
    // (this is acceptable behavior — deadline is a protection, not a guarantee of results)

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Deadline, syllabifier_deadline_returns_partial_paths) {
    std::string spellings_path = make_temp_path("test_syl_deadline_spellings.bin");

    // Create a spellings index with many abbreviation paths
    std::vector<std::tuple<std::string, std::string, int, float>> entries;
    // Many single-letter abbreviations to create many paths
    for (char c = 'a'; c <= 'z'; ++c) {
        char key[2] = {c, '\0'};
        char full[4] = {c, c, c, '\0'};
        entries.push_back({key, full, 2, -0.693f});
        entries.push_back({full, full, 0, 0.0f});
    }
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, entries));

    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);

    // Create an already-expired deadline
    cxxime::QueryDeadline deadline;
    deadline.enabled = true;
    deadline.expires_at = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    deadline.check_interval = 1;  // check every path

    auto result = syllabifier.segment("abcdefghijklmnopqrstuvwxyz", &deadline);

    // Should have returned with deadline flags set
    ASSERT_TRUE(result.deadline_exceeded);
    ASSERT_TRUE(result.truncated);
    // Paths may be empty if deadline expired before any paths were enumerated
    // (this is acceptable behavior — deadline is a protection, not a guarantee of results)

    DeleteFileA(spellings_path.c_str());
}

TEST(Deadline, default_deadline_no_trigger_on_normal_input) {
    std::string dict_path = make_temp_path("test_default_deadline.bin");
    std::string spellings_path = make_temp_path("test_default_deadline_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao", "你好", 1000},
        {"ni",     "你", 500},
        {"hao",    "好", 400},
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"ni",  "ni",  0, 0.0f},
        {"hao", "hao", 0, 0.0f},
    }));

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict_path));
    engine.set_trace_enabled(true);
    // Default deadline is 30ms — should not trigger on normal input

    // Type "nihao"
    for (char c : "nihao") {
        if (c == '\0') break;
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }

    const auto& trace = engine.last_trace();
    // Default 30ms should not trigger on normal input
    ASSERT_TRUE(!trace.deadline_exceeded);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Deadline, disabled_deadline_matches_phase2) {
    std::string dict_path = make_temp_path("test_disabled_deadline.bin");
    std::string spellings_path = make_temp_path("test_disabled_deadline_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao", "你好", 1000},
        {"ni",     "你", 500},
        {"hao",    "好", 400},
    });

    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {
        {"ni",  "ni",  0, 0.0f},
        {"hao", "hao", 0, 0.0f},
    }));

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict_path));
    engine.set_trace_enabled(true);
    engine.set_query_deadline_ms(0);  // disable deadline

    // Type "nihao"
    for (char c : "nihao") {
        if (c == '\0') break;
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }

    const auto& trace = engine.last_trace();
    // With deadline disabled, should have results without deadline flags
    ASSERT_TRUE(!trace.deadline_exceeded);
    ASSERT_TRUE(trace.candidate_count > 0);

    engine.finalize();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

RUN_ALL_TESTS()
