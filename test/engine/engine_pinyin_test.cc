// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_test_support.h"
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

    auto page = translator.translate_page("shurufa", 0, 10);
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
    const auto candidates = engine.context().candidate_page().candidates;
    const auto candidate = std::find_if(candidates.begin(), candidates.end(),
        [](const auto& item) { return item.text == "无输出"; });
    ASSERT_TRUE(candidate != candidates.end());
    ASSERT_TRUE(candidate->origin == cxxime::CandidateOrigin::kComposed);
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(candidate - candidates.begin())));
    ASSERT_EQ(engine.get_commit_text(), "无输出");
    ASSERT_TRUE(!dict.has_user_entry("无输出"));
    ASSERT_EQ(dict.candidate_preference_count(), static_cast<size_t>(0));

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

    auto page = translator.translate_page("nihao", 0, 10);
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

    auto page = translator.translate_page("bj", 0, 10);
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

    auto page = translator.translate_page("srf", 0, 10);
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

    auto page = translator.translate_page("zhg", 0, 10);
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

    auto page = translator.translate_page("zguo", 0, 10);
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

    auto page = translator.translate_page("zongguo", 0, 10);
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

    auto page = translator.translate_page("cifan", 0, 10);
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
    auto page = translator.translate_page("", 0, 10);
    ASSERT_EQ(page.candidates.size(), 0u);
}

TEST(Engine, translate_no_match) {
    cxxime::PinyinTranslator translator;
    auto page = translator.translate_page("xyz", 0, 10);
    ASSERT_EQ(page.candidates.size(), 0u);
}
