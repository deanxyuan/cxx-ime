// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <tuple>
#include <algorithm>
#include <windows.h>
#include <cxxime/engine.h>
#include <cxxime/translator.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
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
        {"di:di", "\xe5\xbc\x9f\xe5\xbc\x9f", 500},
        {"da:da", "\xe5\xa4\xa7\xe5\xa4\xa7", 400},
        {"de:dao", "\xe5\xbe\x97\xe5\x88\xb0", 300},
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
        if (c.text == "\xe5\xbc\x9f\xe5\xbc\x9f") found_didi = true;
    }
    ASSERT_TRUE(found_didi);
    ASSERT_EQ(page.highlighted, 0);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_valid_pinyin) {
    std::string dict_path = make_temp_path("test_engine_valid.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"de", "\xe7\x9a\x84", 1000},
        {"de:dao", "\xe5\xbe\x97\xe5\x88\xb0", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(dict_path));

    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);

    auto page = translator.translate("de", 0, 10);
    ASSERT_GE(page.candidates.size(), 1u);

    bool found_de = false;
    for (const auto& c : page.candidates) {
        if (c.text == "\xe7\x9a\x84") found_de = true;
    }
    ASSERT_TRUE(found_de);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Engine, space_with_candidates_commits) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "de";
    ctx.candidates.candidates.push_back({"\xe7\x9a\x84", "", 100});
    ctx.candidates.highlighted = 0;

    cxxime::KeyEvent event;
    event.keycode = VK_SPACE;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "\xe7\x9a\x84");
}

TEST(Engine, space_no_candidates_rejects) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "zzz";

    cxxime::KeyEvent event;
    event.keycode = VK_SPACE;
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::REJECTED);
}

TEST(Engine, number_selects_candidate) {
    cxxime::Context ctx;
    ctx.pinyin_buffer = "de";
    ctx.candidates.candidates.push_back({"\xe7\x9a\x84", "", 100});
    ctx.candidates.candidates.push_back({"\xe5\x9c\xb0", "", 80});
    ctx.candidates.highlighted = 0;

    cxxime::KeyEvent event;
    event.keycode = '2';
    event.is_key_up = false;

    cxxime::PinyinProcessor processor;
    auto result = processor.process_key(event, ctx);
    ASSERT_EQ(result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "\xe5\x9c\xb0");
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
        {"shu:ru:fa", "\xe8\xbe\x93\xe5\x85\xa5\xe6\xb3\x95", 1000}, // 输入法
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
        if (c.text == "\xe8\xbe\x93\xe5\x85\xa5\xe6\xb3\x95") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_nihao) {
    std::string dict_path = make_temp_path("test_nihao_dict.bin");
    std::string spellings_path = make_temp_path("test_nihao_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao", "\xe4\xbd\xa0\xe5\xa5\xbd", 1000}, // 你好
        {"ni",     "\xe4\xbd\xa0", 500},                // 你
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
    ASSERT_EQ(page.candidates[0].text, "\xe4\xbd\xa0\xe5\xa5\xbd");

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

// --- First-letter abbreviation matching (全简拼) ---

TEST(Engine, translate_abbrev_bj) {
    std::string dict_path = make_temp_path("test_abbrev_bj_dict.bin");
    std::string spellings_path = make_temp_path("test_abbrev_bj_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"bei:jing", "\xe5\x8c\x97\xe4\xba\xac", 1000}, // 北京
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
        if (c.text == "\xe5\x8c\x97\xe4\xba\xac") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_abbrev_srf) {
    std::string dict_path = make_temp_path("test_abbrev_srf_dict.bin");
    std::string spellings_path = make_temp_path("test_abbrev_srf_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"shu:ru:fa", "\xe8\xbe\x93\xe5\x85\xa5\xe6\xb3\x95", 1000}, // 输入法
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
        if (c.text == "\xe8\xbe\x93\xe5\x85\xa5\xe6\xb3\x95") found = true;
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
        {"zhong:guo", "\xe4\xb8\xad\xe5\x9b\xbd", 1000}, // 中国
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
        if (c.text == "\xe4\xb8\xad\xe5\x9b\xbd") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_mixed_zguo) {
    std::string dict_path = make_temp_path("test_mixed_zguo_dict.bin");
    std::string spellings_path = make_temp_path("test_mixed_zguo_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"zhong:guo", "\xe4\xb8\xad\xe5\x9b\xbd", 1000}, // 中国
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
        if (c.text == "\xe4\xb8\xad\xe5\x9b\xbd") found = true;
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
        {"zhong:guo", "\xe4\xb8\xad\xe5\x9b\xbd", 1000}, // 中国
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
        if (c.text == "\xe4\xb8\xad\xe5\x9b\xbd") found = true;
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(Engine, translate_fuzzy_cifan) {
    std::string dict_path = make_temp_path("test_fuzzy_cifan_dict.bin");
    std::string spellings_path = make_temp_path("test_fuzzy_cifan_spellings.bin");

    cxxime::Dict::create_test_dict(dict_path, {
        {"chi:fan", "\xe5\x90\x83\xe9\xa5\xad", 1000}, // 吃饭
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
        if (c.text == "\xe5\x90\x83\xe9\xa5\xad") found = true;
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

TEST(AsciiComposer, shift_l_commit_text_toggles_and_commits) {
    cxxime::Config config;
    config.ascii_switch_key["Shift_L"] = "commit_text";
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
    config.ascii_switch_key["Shift_L"] = "commit_text";
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
    config.ascii_switch_key["Shift_L"] = "commit_text";

    cxxime::AsciiComposer ac;
    ac.load_config(config);
    cxxime::Context ctx;

    ASSERT_TRUE(!ac.is_ascii_mode());

    // First Shift: Chinese -> English (commit_text toggles)
    ac.process_key(0xA0, false, ctx);
    ac.process_key(0xA0, true, ctx);
    ASSERT_TRUE(ac.is_ascii_mode());

    // Second Shift: English -> Chinese (commit_text toggles back)
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

    // CapsLock should work as toggle (downgraded from inline_ascii to clear)
    ASSERT_TRUE(!ac.is_ascii_mode());
    ac.process_key(0x14, false, ctx);  // VK_CAPITAL down
    ASSERT_TRUE(ac.is_ascii_mode());
    ac.process_key(0x14, false, ctx);  // VK_CAPITAL down again
    ASSERT_TRUE(!ac.is_ascii_mode());
}

// Initialize temp_path before tests run
static bool _engine_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

RUN_ALL_TESTS()
