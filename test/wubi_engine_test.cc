// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/engine.h>
#include <cxxime/syllabifier.h>
#include <cxxime/wubi_processor.h>
#include <cxxime/wubi_translator.h>

#include "util/testutil.h"

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

// Helper: press a key
static cxxime::KeyEvent make_key(uint32_t vk, bool shift = false) {
    cxxime::KeyEvent e;
    e.keycode = vk;
    e.is_key_up = false;
    if (shift) e.set_shift();
    return e;
}

// --- WubiProcessor tests ---

TEST(WubiEngine, processor_letter_input) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    // 输入字母 a
    auto r = proc.process_key(make_key('A'), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.pinyin_buffer, "a");

    // 输入字母 b
    r = proc.process_key(make_key('B'), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.pinyin_buffer, "ab");

    // Escape 清空
    r = proc.process_key(make_key(VK_ESCAPE), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
}

TEST(WubiEngine, processor_backspace) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    proc.process_key(make_key('A'), ctx);
    proc.process_key(make_key('B'), ctx);
    ASSERT_EQ(ctx.pinyin_buffer, "ab");

    auto r = proc.process_key(make_key(VK_BACK), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.pinyin_buffer, "a");

    r = proc.process_key(make_key(VK_BACK), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.pinyin_buffer.empty());

    // 空输入时按 Backspace，应 REJECTED
    r = proc.process_key(make_key(VK_BACK), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::REJECTED);
}

TEST(WubiEngine, processor_number_select) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    // 设置候选
    ctx.pinyin_buffer = "a";
    cxxime::CandidatePage page;
    cxxime::Candidate c1; c1.text = "工"; c1.frequency = 300;
    cxxime::Candidate c2; c2.text = "式"; c2.frequency = 200;
    page.candidates = {c1, c2};
    page.highlighted = 0;
    ctx.candidates = page;

    // 按数字键 2 选中 "式"
    auto r = proc.process_key(make_key('2'), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "式");
}

TEST(WubiEngine, processor_space_select_first) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    // 设置候选
    ctx.pinyin_buffer = "a";
    cxxime::CandidatePage page;
    cxxime::Candidate c1; c1.text = "工"; c1.frequency = 300;
    cxxime::Candidate c2; c2.text = "式"; c2.frequency = 200;
    page.candidates = {c1, c2};
    page.highlighted = 0;
    ctx.candidates = page;

    // Space 选中第一候选
    auto r = proc.process_key(make_key(VK_SPACE), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(ctx.committed_text, "工");
}

TEST(WubiEngine, processor_space_without_candidates_clears) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    ctx.pinyin_buffer = "niwe";

    auto r = proc.process_key(make_key(VK_SPACE), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.pinyin_buffer.empty());
    ASSERT_TRUE(ctx.committed_text.empty());
    ASSERT_TRUE(ctx.candidates.candidates.empty());
}

// --- WubiTranslator tests ---

TEST(WubiEngine, translator_basic_lookup) {
    std::string dict_path = make_temp_path("test_wubi_trans.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"a", "工", 300},
        {"aaaa", "工", 300},
        {"aa", "式", 200},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    cxxime::WubiTranslator trans;
    trans.set_dict(&dict);

    // 查询 "a"，应返回 "工" 和/或 "式"
    auto page = trans.translate("a", 0, 9);
    ASSERT_GE(page.candidates.size(), 1u);

    // 查询 "aa"，应返回 "式"
    page = trans.translate("aa", 0, 9);
    ASSERT_GE(page.candidates.size(), 1u);
    ASSERT_EQ(page.candidates[0].text, "式");

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(WubiEngine, translator_uses_explicit_candidate_offset) {
    std::string dict_path = make_temp_path("test_wubi_offset.bin");
    const std::vector<std::tuple<std::string, std::string, int>> entries = {
        {"a", "一", 500},  {"aa", "二", 400}, {"ab", "三", 300},
        {"ac", "四", 200}, {"ad", "五", 100},
    };
    cxxime::Dict::create_test_dict(dict_path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    cxxime::WubiTranslator translator;
    translator.set_dict(&dict);
    auto first = translator.translate("a", 0, 2);
    auto second = translator.translate("a", 1, 2, nullptr, nullptr, nullptr, 2);

    ASSERT_EQ(first.page_offset, 0);
    ASSERT_EQ(second.page_index, 1);
    ASSERT_EQ(second.page_offset, 2);
    ASSERT_EQ(first.candidates.size(), 2u);
    ASSERT_EQ(second.candidates.size(), 2u);
    for (const auto& first_candidate : first.candidates) {
        for (const auto& second_candidate : second.candidates) {
            ASSERT_TRUE(first_candidate.text != second_candidate.text);
        }
    }

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(WubiEngine, translator_keeps_candidate_order_stable_when_page_query_expands) {
    std::string dict_path = make_temp_path("test_wubi_stable_pages.bin");
    std::string user_dict_path = make_temp_path("test_wubi_stable_pages_user.tsv");
    DeleteFileA(user_dict_path.c_str());
    const std::vector<std::tuple<std::string, std::string, int>> entries = {
        {"a", "exact", 500},   {"aa", "early", 1},    {"ab", "rank-1", 500},
        {"ac", "rank-2", 600}, {"ad", "rank-3", 700}, {"ae", "rank-4", 800},
    };
    cxxime::Dict::create_test_dict(dict_path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path, user_dict_path));

    cxxime::WubiTranslator translator;
    translator.set_dict(&dict);
    auto first = translator.translate("a", 0, 2);
    auto second = translator.translate("a", 1, 2, nullptr, nullptr, nullptr, 2);

    ASSERT_EQ(first.candidates.size(), 2u);
    ASSERT_EQ(second.candidates.size(), 2u);
    ASSERT_EQ(second.candidates[0].text, "early");
    for (const auto& first_candidate : first.candidates) {
        for (const auto& second_candidate : second.candidates) {
            ASSERT_TRUE(first_candidate.text != second_candidate.text);
        }
    }

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(user_dict_path.c_str());
}

TEST(WubiEngine, translator_empty_code) {
    std::string dict_path = make_temp_path("test_wubi_empty.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"a", "工", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    cxxime::WubiTranslator trans;
    trans.set_dict(&dict);

    // 空输入应返回空结果
    auto page = trans.translate("", 0, 9);
    ASSERT_EQ(page.candidates.size(), 0u);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

// --- Engine integration tests ---

TEST(WubiEngine, engine_switch_mode) {
    std::string pinyin_path = make_temp_path("test_wubi_engine_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_engine_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {
        {"a", "工", 300},
        {"aaaa", "工", 300},
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    // 初始应为拼音模式
    engine.switch_mode(cxxime::InputMode::PINYIN);

    // 切换到五笔模式
    engine.switch_mode(cxxime::InputMode::WUBI);

    // 切换回拼音模式
    engine.switch_mode(cxxime::InputMode::PINYIN);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_wubi_input_flow) {
    std::string pinyin_path = make_temp_path("test_wubi_flow_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_flow_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {
        {"a", "工", 300},
        {"aaaa", "工", 300},
        {"aa", "式", 200},
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    // 切换到五笔模式
    engine.switch_mode(cxxime::InputMode::WUBI);

    // 输入字母 a
    auto r = engine.process_key(make_key('A'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);

    // 应有候选（"工" 或 "式"）
    auto& ctx = engine.context();
    ASSERT_GE(ctx.candidates.candidates.size(), 1u);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_paginates_from_visible_candidate_count_without_skipping) {
    std::string pinyin_path = make_temp_path("test_wubi_page_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_page_wubi.bin");
    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    const std::vector<std::tuple<std::string, std::string, int>> entries = {
        {"a", "一", 1200}, {"aa", "二", 1100}, {"ab", "三", 1000},
        {"ac", "四", 900}, {"ad", "五", 800},  {"ae", "六", 700},
        {"af", "七", 600}, {"ag", "八", 500},  {"ah", "九", 400},
    };
    cxxime::Dict::create_test_dict(wubi_path, entries);

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));
    engine.set_config_page_size(7);
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::WUBI);

    ASSERT_EQ(engine.process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().candidates.candidates.size(), 7u);
    std::string expected_second_page_first = engine.context().candidates.candidates[2].text;

    cxxime::OutputOptions options;
    ASSERT_EQ(engine.process_key(make_key(VK_NEXT), options, 2), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().page_index, 1);
    ASSERT_EQ(engine.context().page_offset, 2);
    ASSERT_EQ(engine.context().candidates.candidates[0].text, expected_second_page_first);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_wubi_auto_commit) {
    std::string pinyin_path = make_temp_path("test_wubi_auto_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_auto_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {
        {"abcd", "中", 300},  // 唯一四码候选
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    // 切换到五笔模式
    engine.switch_mode(cxxime::InputMode::WUBI);

    // 输入 abcd（四码）
    engine.process_key(make_key('A'));
    engine.process_key(make_key('B'));
    engine.process_key(make_key('C'));
    auto r = engine.process_key(make_key('D'));

    // 四码唯一候选应自动上屏
    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "中");

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_wubi_fifth_key_commits_first_and_starts_next_code) {
    std::string pinyin_path = make_temp_path("test_wubi_fifth_key_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_fifth_key_wubi.bin");
    std::string wubi_user_path = make_temp_path("test_wubi_fifth_key_user.tsv");
    DeleteFileA(wubi_user_path.c_str());

    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, {{"a", "拼", 100}}));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {
                                                    {"abcd", "首选", 300},
                                                    {"abcd", "次选", 200},
                                                    {"e", "下一项", 100},
    }));

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path, wubi_user_path));
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::WUBI);

    ASSERT_EQ(engine.process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('B')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('C')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('D')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_GE(engine.context().candidates.candidates.size(), 2u);

    ASSERT_EQ(engine.process_key(make_key('E')), cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "首选");
    ASSERT_EQ(engine.context().pinyin_buffer, "e");
    ASSERT_TRUE(engine.context().is_composing());
    ASSERT_EQ(engine.context().candidates.candidates[0].text, "下一项");

    const auto committed = engine.take_commit_text_with_source();
    ASSERT_EQ(committed.first, "首选");
    ASSERT_EQ(committed.second, cxxime::CommitSource::kCandidate);
    ASSERT_EQ(engine.context().pinyin_buffer, "e");
    ASSERT_TRUE(engine.context().is_composing());

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
}

TEST(WubiEngine, engine_wubi_fifth_key_commit_can_be_disabled) {
    std::string pinyin_path = make_temp_path("test_wubi_fifth_key_off_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_fifth_key_off_wubi.bin");
    std::string wubi_user_path = make_temp_path("test_wubi_fifth_key_off_user.tsv");
    DeleteFileA(wubi_user_path.c_str());

    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, {{"a", "拼", 100}}));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {
                                                {"abcd", "首选", 300},
                                                {"abcd", "次选", 200},
    }));

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path, wubi_user_path));
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::WUBI);
    cxxime::Config config;
    config.wubi_commit_first_on_fifth_key = false;
    engine.reload_config(config);

    ASSERT_EQ(engine.process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('B')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('C')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('D')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.process_key(make_key('E')), cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(engine.context().committed_text.empty());
    ASSERT_EQ(engine.context().pinyin_buffer, "abcde");

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
}

TEST(WubiEngine, engine_wubi_space_without_candidates_clears) {
    std::string pinyin_path = make_temp_path("test_wubi_space_clear_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_space_clear_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"ni:hao", "你好", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {{"a", "工", 300}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::WUBI);

    engine.process_key(make_key('N'));
    engine.process_key(make_key('I'));
    engine.process_key(make_key('W'));
    auto r = engine.process_key(make_key('E'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(engine.context().is_composing());
    ASSERT_TRUE(engine.context().candidates.candidates.empty());

    r = engine.process_key(make_key(VK_SPACE));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(!engine.context().is_composing());
    ASSERT_TRUE(engine.context().committed_text.empty());
    ASSERT_TRUE(engine.context().candidates.candidates.empty());

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_no_wubi_dict_fallback) {
    std::string pinyin_path = make_temp_path("test_wubi_fallback.bin");
    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    // 不设置五笔词典，尝试切换到五笔模式
    engine.set_wubi_dict(nullptr);
    engine.switch_mode(cxxime::InputMode::WUBI);

    // 应自动回退到拼音模式
    auto r = engine.process_key(make_key('A'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);

    // 应有拼音候选
    auto& ctx = engine.context();
    ASSERT_GE(ctx.candidates.candidates.size(), 1u);

    engine.finalize();
    DeleteFileA(pinyin_path.c_str());
}

// --- Mixed mode tests ---

TEST(WubiEngine, engine_mixed_switch) {
    std::string pinyin_path = make_temp_path("test_mixed_pinyin.bin");
    std::string wubi_path = make_temp_path("test_mixed_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {{"a", "工", 300}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    // 切换到混输模式
    engine.switch_mode(cxxime::InputMode::MIXED);

    // 输入字母 a，应有候选
    auto r = engine.process_key(make_key('A'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_GE(engine.context().candidates.candidates.size(), 1u);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_mixed_returns_candidates) {
    std::string pinyin_path = make_temp_path("test_mixed_merge_pinyin.bin");
    std::string wubi_path = make_temp_path("test_mixed_merge_wubi.bin");

    // 拼音词典（PinyinTranslator 需要音节 ID，简化 dict 无法测拼音部分）
    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    // 五笔词典有 "a" → "工"
    cxxime::Dict::create_test_dict(wubi_path, {{"a", "工", 300}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    engine.switch_mode(cxxime::InputMode::MIXED);

    // 输入 a，混输模式应返回候选（至少五笔的 "工"）
    engine.process_key(make_key('A'));
    auto& candidates = engine.context().candidates.candidates;
    ASSERT_GE(candidates.size(), 1u);

    // 验证五笔候选在结果中
    bool has_wubi = false;
    for (auto& c : candidates) {
        if (c.text == "工") has_wubi = true;
    }
    ASSERT_TRUE(has_wubi);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, mixed_wubi_preference_interleaves_candidate_sources) {
    std::string pinyin_path = make_temp_path("test_mixed_preference_pinyin.bin");
    std::string wubi_path = make_temp_path("test_mixed_preference_wubi.bin");
    std::string pinyin_user_path = make_temp_path("test_mixed_preference_pinyin.tsv");
    std::string wubi_user_path = make_temp_path("test_mixed_preference_wubi.tsv");
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());

    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, {
                                                {"a", "pinyin-one", 400},
                                                {"a", "pinyin-two", 300},
    }));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {
                                                {"a", "wubi-one", 400},
                                                {"a", "wubi-two", 300},
    }));

    cxxime::Dict pinyin_dict;
    cxxime::Dict wubi_dict;
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(pinyin_dict.open(pinyin_path, pinyin_user_path));
    ASSERT_TRUE(wubi_dict.open(wubi_path, wubi_user_path));

    cxxime::Config config;
    config.page_size = 10;
    config.mixed_candidate_preference = cxxime::MixedCandidatePreference::kWubi;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_dict, spellings, nullptr, config));
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::MIXED);

    ASSERT_EQ(engine.process_key(make_key('A')), cxxime::ProcessResult::ACCEPTED);
    const auto& candidates = engine.context().candidates.candidates;
    ASSERT_GE(candidates.size(), 4u);
    ASSERT_EQ(candidates[0].source, cxxime::CandidateSource::kWubi);
    ASSERT_EQ(candidates[1].source, cxxime::CandidateSource::kPinyin);
    ASSERT_EQ(candidates[2].source, cxxime::CandidateSource::kWubi);
    ASSERT_EQ(candidates[3].source, cxxime::CandidateSource::kPinyin);

    engine.finalize();
    pinyin_dict.close();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
}

TEST(WubiEngine, engine_mixed_wubi_auto_commit) {
    std::string pinyin_path = make_temp_path("test_mixed_auto_pinyin.bin");
    std::string wubi_path = make_temp_path("test_mixed_auto_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    // 五笔四码唯一候选
    cxxime::Dict::create_test_dict(wubi_path, {{"abcd", "中", 300}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    engine.switch_mode(cxxime::InputMode::MIXED);

    // 输入 abcd（四码），五笔唯一候选应自动上屏
    engine.process_key(make_key('A'));
    engine.process_key(make_key('B'));
    engine.process_key(make_key('C'));
    auto r = engine.process_key(make_key('D'));

    ASSERT_EQ(r, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(engine.context().committed_text, "中");

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_mixed_does_not_auto_commit_unique_pinyin_candidate) {
    std::string pinyin_path = make_temp_path("test_mixed_auto_pinyin_only.bin");
    std::string pinyin_user_path = make_temp_path("test_mixed_auto_pinyin_only.tsv");
    std::string spellings_path = make_temp_path("test_mixed_auto_pinyin_spellings.bin");
    std::string wubi_path = make_temp_path("test_mixed_auto_unrelated_wubi.bin");
    std::string wubi_user_path = make_temp_path("test_mixed_auto_unrelated_wubi.tsv");
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());

    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, {{"ni:ha", "拼", 100}}));
    using TestSpelling = std::tuple<std::string, std::string, int, float>;
    const std::vector<TestSpelling> test_spellings = {
        {"ni", "ni", 0, 0.0f},
        {"ha", "ha", 0, 0.0f},
    };
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, test_spellings));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {{"zzzz", "五", 300}}));

    cxxime::Dict pinyin_dict;
    ASSERT_TRUE(pinyin_dict.open(pinyin_path, pinyin_user_path));
    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::Config config;
    config.wubi_auto_commit = true;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_dict, spellings, &syllabifier, config));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path, wubi_user_path));
    ASSERT_TRUE(wubi_dict.lookup("niha", 10).empty());
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::MIXED);

    engine.process_key(make_key('N'));
    engine.process_key(make_key('I'));
    engine.process_key(make_key('H'));
    auto result = engine.process_key(make_key('A'));
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().candidates.candidates.size(), 1u);
    ASSERT_EQ(engine.context().candidates.candidates[0].source, cxxime::CandidateSource::kPinyin);

    engine.finalize();
    pinyin_dict.close();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(spellings_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
}

TEST(WubiEngine, engine_mixed_select_candidate) {
    std::string pinyin_path = make_temp_path("test_mixed_sel_pinyin.bin");
    std::string wubi_path = make_temp_path("test_mixed_sel_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {{"a", "工", 300}, {"aa", "式", 200}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    engine.switch_mode(cxxime::InputMode::MIXED);

    // 输入字母 a，获取候选列表
    engine.process_key(make_key('A'));
    auto& ctx = engine.context();
    ASSERT_GE(ctx.candidates.candidates.size(), 2u);

    // 选中第二个候选
    std::string expected = ctx.candidates.candidates[1].text;
    bool ok = engine.select_candidate(1);
    ASSERT_TRUE(ok);
    ASSERT_EQ(engine.context().committed_text, expected);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_mixed_candidate_source_tagging) {
    std::string pinyin_path = make_temp_path("test_mixed_src_pinyin.bin");
    std::string wubi_path = make_temp_path("test_mixed_src_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {{"a", "工", 300}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    engine.switch_mode(cxxime::InputMode::MIXED);

    // 输入字母 a，获取候选
    auto r = engine.process_key(make_key('A'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    auto& candidates = engine.context().candidates.candidates;
    ASSERT_GE(candidates.size(), 1u);

    // 验证候选来源标签
    bool has_wubi = false;
    bool has_pinyin = false;
    for (auto& c : candidates) {
        if (c.text == "工") {
            ASSERT_EQ(c.source, cxxime::CandidateSource::kWubi);
            has_wubi = true;
        }
        if (c.text == "啊") {
            ASSERT_EQ(c.source, cxxime::CandidateSource::kPinyin);
            has_pinyin = true;
        }
    }
    ASSERT_TRUE(has_wubi);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_wubi_candidate_source) {
    std::string pinyin_path = make_temp_path("test_wubi_src_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_src_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {
        {"a", "工", 300},
        {"aa", "式", 200},
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    engine.switch_mode(cxxime::InputMode::WUBI);

    // 输入字母 a，获取候选
    auto r = engine.process_key(make_key('A'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    auto& candidates = engine.context().candidates.candidates;
    ASSERT_GE(candidates.size(), 1u);

    // 纯五笔模式：所有候选来源应为 kWubi
    for (auto& c : candidates) {
        ASSERT_EQ(c.source, cxxime::CandidateSource::kWubi);
    }

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, translator_recent_does_not_override_prefix_protection) {
    std::string wubi_path = make_temp_path("test_wubi_recent_prefix.bin");
    cxxime::Dict::create_test_dict(wubi_path, {
        {"aaa", "工", 300},
        {"aaaa", "自定义词", 10},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(wubi_path));

    cxxime::WubiTranslator translator;
    translator.set_dict(&dict);

    cxxime::Candidate long_word;
    long_word.text = "自定义词";
    long_word.code = "aaaa";
    long_word.frequency = 999999;
    long_word.source = cxxime::CandidateSource::kWubi;
    translator.update_recent("aaa", long_word);

    auto page = translator.translate("aaa", 0, 9);
    ASSERT_GE(page.candidates.size(), 1u);
    ASSERT_EQ(page.candidates[0].text, "工");

    dict.close();
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_wubi_auto_commit_disabled) {
    std::string pinyin_path = make_temp_path("test_wubi_nocommit_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_nocommit_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {{"abcd", "中", 300}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    engine.switch_mode(cxxime::InputMode::WUBI);

    // 关闭四码自动上屏
    cxxime::Config cfg;
    cfg.wubi_auto_commit = false;
    engine.reload_config(cfg);

    // 输入 abcd（四码），不应自动上屏
    engine.process_key(make_key('A'));
    engine.process_key(make_key('B'));
    engine.process_key(make_key('C'));
    auto r = engine.process_key(make_key('D'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);

    // 候选应仍然可见
    auto& ctx = engine.context();
    ASSERT_EQ(ctx.candidates.candidates.size(), 1u);
    ASSERT_EQ(ctx.candidates.candidates[0].text, "中");

    // 手动选中第一候选，应上屏
    bool ok = engine.select_candidate(0);
    ASSERT_TRUE(ok);
    ASSERT_EQ(engine.context().committed_text, "中");

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_wubi_code_hint_is_optional_and_does_not_change_commit_text) {
    std::string pinyin_path = make_temp_path("test_wubi_hint_pinyin.bin");
    std::string wubi_path = make_temp_path("test_wubi_hint_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {
        {"wq", "你", 300},
        {"wqi", "你", 300},
        {"wqa", "低", 300},
        {"wqay", "低", 300},
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);
    engine.switch_mode(cxxime::InputMode::WUBI);

    engine.process_key(make_key('W'));
    engine.process_key(make_key('Q'));
    for (const auto& candidate : engine.context().candidates.candidates) {
        ASSERT_TRUE(candidate.comment.empty());
    }
    const cxxime::QueryTrace trace_without_hint = engine.last_trace();

    engine.clear();
    cxxime::Config cfg;
    cfg.wubi_code_hint = true;
    engine.reload_config(cfg);
    engine.process_key(make_key('W'));
    engine.process_key(make_key('Q'));
    const cxxime::QueryTrace trace_with_hint = engine.last_trace();

    ASSERT_EQ(trace_with_hint.exact_scan_count, trace_without_hint.exact_scan_count);
    ASSERT_EQ(trace_with_hint.prefix_scan_count, trace_without_hint.prefix_scan_count);
    ASSERT_EQ(trace_with_hint.user_scan_count, trace_without_hint.user_scan_count);
    ASSERT_EQ(trace_with_hint.mixed_scan_count, trace_without_hint.mixed_scan_count);

    int low_index = -1;
    for (int i = 0; i < static_cast<int>(engine.context().candidates.candidates.size()); ++i) {
        const auto& candidate = engine.context().candidates.candidates[i];
        if (candidate.text == "你") {
            ASSERT_TRUE(candidate.comment.empty());
        } else if (candidate.text == "低") {
            ASSERT_EQ(candidate.comment, "a");
            low_index = i;
        }
    }
    ASSERT_GE(low_index, 0);
    ASSERT_TRUE(engine.select_candidate(low_index));
    ASSERT_EQ(engine.context().committed_text, "低");

    engine.clear();
    engine.switch_mode(cxxime::InputMode::MIXED);
    engine.process_key(make_key('W'));
    engine.process_key(make_key('Q'));
    bool found_mixed_wubi_hint = false;
    for (const auto& candidate : engine.context().candidates.candidates) {
        if (candidate.source == cxxime::CandidateSource::kWubi && candidate.text == "低") {
            ASSERT_EQ(candidate.comment, "a");
            found_mixed_wubi_hint = true;
        } else if (candidate.source != cxxime::CandidateSource::kWubi) {
            ASSERT_TRUE(candidate.comment.empty());
        }
    }
    ASSERT_TRUE(found_mixed_wubi_hint);

    engine.clear();
    cfg.wubi_code_hint = false;
    engine.reload_config(cfg);
    engine.process_key(make_key('W'));
    engine.process_key(make_key('Q'));
    for (const auto& candidate : engine.context().candidates.candidates) {
        ASSERT_TRUE(candidate.comment.empty());
    }

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_mixed_select_candidate_updates_correct_dict) {
    std::string pinyin_path = make_temp_path("test_mixed_upd_pinyin.bin");
    std::string wubi_path = make_temp_path("test_mixed_upd_wubi.bin");

    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});
    cxxime::Dict::create_test_dict(wubi_path, {
        {"a", "工", 300},
        {"aa", "式", 200},
    });

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    cxxime::Dict wubi_dict;
    ASSERT_TRUE(wubi_dict.open(wubi_path));
    engine.set_wubi_dict(&wubi_dict);

    engine.switch_mode(cxxime::InputMode::MIXED);

    // 输入字母 a，获取候选列表
    engine.process_key(make_key('A'));
    auto& ctx = engine.context();
    ASSERT_GE(ctx.candidates.candidates.size(), 1u);

    // 找到五笔候选的索引
    int wubi_idx = -1;
    for (int i = 0; i < (int)ctx.candidates.candidates.size(); i++) {
        if (ctx.candidates.candidates[i].source == cxxime::CandidateSource::kWubi) {
            wubi_idx = i;
            break;
        }
    }
    ASSERT_GE(wubi_idx, 0);

    // 选中五笔候选，验证上屏文本正确
    std::string expected = ctx.candidates.candidates[wubi_idx].text;
    bool ok = engine.select_candidate(wubi_idx);
    ASSERT_TRUE(ok);
    ASSERT_EQ(engine.context().committed_text, expected);

    engine.finalize();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

TEST(WubiEngine, engine_mixed_no_wubi_dict_fallback) {
    std::string pinyin_path = make_temp_path("test_mixed_fallback.bin");
    cxxime::Dict::create_test_dict(pinyin_path, {{"a", "啊", 100}});

    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_path));

    // 不设置五笔词典，尝试混输模式
    engine.set_wubi_dict(nullptr);
    engine.switch_mode(cxxime::InputMode::MIXED);

    // 应自动回退到拼音模式
    auto r = engine.process_key(make_key('A'));
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_GE(engine.context().candidates.candidates.size(), 1u);

    engine.finalize();
    DeleteFileA(pinyin_path.c_str());
}

// Initialize temp_path before tests run
static bool _wubi_engine_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

RUN_ALL_TESTS()
