// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "wubi_engine_test_support.h"
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

    ASSERT_EQ(engine.process_key(make_key(VK_OEM_PLUS, true)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kInlineAscii);
    ASSERT_EQ(engine.process_key(make_key(VK_BACK)), cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(engine.context().composition_kind(), cxxime::CompositionKind::kIme);
    ASSERT_EQ(engine.context().candidates.candidates[low_index].comment, "a");

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
    GetTempPathA(MAX_PATH, wubi_engine_test_temp_path);
    return true;
}();
