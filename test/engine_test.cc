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

// Initialize temp_path before tests run
static bool _engine_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

RUN_ALL_TESTS()
