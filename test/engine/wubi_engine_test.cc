// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "wubi_engine_test_support.h"
TEST(WubiEngine, processor_letter_input) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    // 输入字母 a
    auto r = proc.process_key(make_key('A'), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.active_input(), "a");

    // 输入字母 b
    r = proc.process_key(make_key('B'), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.active_input(), "ab");

    // Escape 清空
    r = proc.process_key(make_key(VK_ESCAPE), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.active_input().empty());
}

TEST(WubiEngine, processor_backspace) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    proc.process_key(make_key('A'), ctx);
    proc.process_key(make_key('B'), ctx);
    ASSERT_EQ(ctx.active_input(), "ab");

    auto r = proc.process_key(make_key(VK_BACK), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(ctx.active_input(), "a");

    r = proc.process_key(make_key(VK_BACK), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.active_input().empty());

    // 空输入时按 Backspace，应 REJECTED
    r = proc.process_key(make_key(VK_BACK), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::REJECTED);
}

TEST(WubiEngine, processor_number_select) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    // 设置候选
    ASSERT_TRUE(ctx.set_preedit("a"));
    cxxime::CandidatePage page;
    cxxime::Candidate c1; c1.text = "工"; c1.frequency = 300;
    cxxime::Candidate c2; c2.text = "式"; c2.frequency = 200;
    page.candidates = {c1, c2};
    page.highlighted = 0;
    ctx.update_candidates(std::move(page));

    // 按数字键 2 选中 "式"
    auto r = proc.process_key(make_key('2'), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::CANDIDATE_SELECTED);
    ASSERT_EQ(ctx.take_requested_candidate_selection().value_or(-1), 1);
}

TEST(WubiEngine, processor_space_select_first) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    // 设置候选
    ASSERT_TRUE(ctx.set_preedit("a"));
    cxxime::CandidatePage page;
    cxxime::Candidate c1; c1.text = "工"; c1.frequency = 300;
    cxxime::Candidate c2; c2.text = "式"; c2.frequency = 200;
    page.candidates = {c1, c2};
    page.highlighted = 0;
    ctx.update_candidates(std::move(page));

    // Space 选中第一候选
    auto r = proc.process_key(make_key(VK_SPACE), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::CANDIDATE_SELECTED);
    ASSERT_EQ(ctx.take_requested_candidate_selection().value_or(-1), 0);
}

TEST(WubiEngine, processor_space_without_candidates_clears) {
    cxxime::WubiProcessor proc;
    cxxime::Context ctx;

    ASSERT_TRUE(ctx.set_preedit("niwe"));

    auto r = proc.process_key(make_key(VK_SPACE), ctx);
    ASSERT_EQ(r, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(ctx.active_input().empty());
    ASSERT_TRUE(ctx.committed_text.empty());
    ASSERT_TRUE(ctx.candidate_page().candidates.empty());
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
    auto page = trans.translate_page("a", 0, 9);
    ASSERT_GE(page.candidates.size(), 1u);

    // 查询 "aa"，应返回 "式"
    page = trans.translate_page("aa", 0, 9);
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
    auto first = translator.translate_page("a", 0, 2);
    auto second = translator.translate_page("a", 1, 2, nullptr, nullptr, nullptr, 2);

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
    std::string index_path = dict_path + ".idx";
    std::string user_dict_path = make_temp_path("test_wubi_stable_pages_user.tsv");
    DeleteFileA(user_dict_path.c_str());
    const std::vector<std::tuple<std::string, std::string, int>> entries = {
        {"a", "exact", 500},   {"aa", "early", 1},    {"ab", "rank-1", 500},
        {"ac", "rank-2", 600}, {"ad", "rank-3", 700}, {"ae", "rank-4", 800},
    };
    cxxime::Dict::create_test_dict(dict_path, entries);
    ASSERT_TRUE(cxxime::test::create_test_wubi_index(index_path, entries));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_wubi_bundle(dict_path, user_dict_path, index_path));

    cxxime::WubiTranslator translator;
    translator.set_dict(&dict);
    auto first = translator.translate_page("a", 0, 2);
    auto second = translator.translate_page("a", 1, 2, nullptr, nullptr, nullptr, 2);

    cxxime::WubiTranslator wide_translator;
    wide_translator.set_dict(&dict);
    auto wide = wide_translator.translate_page("a", 0, 5);

    ASSERT_EQ(first.candidates.size(), 2u);
    ASSERT_EQ(second.candidates.size(), 2u);
    ASSERT_GE(wide.candidates.size(), 4u);
    ASSERT_EQ(first.candidates[0].text, "exact");
    ASSERT_EQ(first.candidates[1].text, "rank-4");
    ASSERT_EQ(second.candidates[0].text, "rank-3");
    ASSERT_EQ(second.candidates[1].text, "rank-2");
    for (size_t index = 0; index < 2; ++index) {
        ASSERT_EQ(first.candidates[index].text, wide.candidates[index].text);
        ASSERT_EQ(second.candidates[index].text, wide.candidates[index + 2].text);
    }

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(index_path.c_str());
    DeleteFileA(user_dict_path.c_str());
}

TEST(WubiEngine, clear_query_cache_discards_candidate_snapshot) {
    const std::string dict_path = make_temp_path("test_wubi_clear_query_cache.bin");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(
        dict_path, {{"a", "exact", 500}, {"aa", "prefix", 400}}));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    cxxime::WubiTranslator translator;
    translator.set_dict(&dict);
    cxxime::QueryBudget budget;

    cxxime::QueryTrace initial_trace = {};
    const auto initial = translator.translate_page("a", 0, 9, &initial_trace, &budget);
    ASSERT_EQ(initial.candidates.size(), 2u);
    ASSERT_TRUE(initial_trace.exact_scan_count + initial_trace.prefix_scan_count > 0);

    cxxime::QueryTrace cached_trace = {};
    const auto cached = translator.translate_page("a", 0, 9, &cached_trace, &budget);
    ASSERT_EQ(cached.candidates.size(), initial.candidates.size());
    ASSERT_EQ(cached_trace.exact_scan_count + cached_trace.prefix_scan_count, 0u);

    translator.clear_query_cache();

    cxxime::QueryTrace cleared_trace = {};
    const auto cleared = translator.translate_page("a", 0, 9, &cleared_trace, &budget);
    ASSERT_EQ(cleared.candidates.size(), initial.candidates.size());
    ASSERT_TRUE(cleared_trace.exact_scan_count + cleared_trace.prefix_scan_count > 0);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(WubiEngine, mixed_order_is_independent_of_page_query_limit) {
    const std::string pinyin_path = make_temp_path("test_mixed_stable_pinyin.bin");
    const std::string wubi_path = make_temp_path("test_mixed_stable_wubi.bin");
    const std::string wubi_index_path = wubi_path + ".idx";
    const std::string pinyin_user_path = pinyin_path + ".user.tsv";
    const std::string wubi_user_path = wubi_path + ".user.tsv";
    const std::vector<std::tuple<std::string, std::string, int>> pinyin_entries = {
        {"a", "pinyin-one", 900},
        {"a", "pinyin-two", 800},
    };
    const std::vector<std::tuple<std::string, std::string, int>> wubi_entries = {
        {"a", "wubi-exact", 500},
        {"aa", "wubi-low", 1},
        {"ab", "wubi-rank-1", 500},
        {"ac", "wubi-rank-2", 600},
        {"ad", "wubi-rank-3", 700},
        {"ae", "wubi-rank-4", 800},
    };
    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, pinyin_entries));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_entries));
    ASSERT_TRUE(cxxime::test::create_test_wubi_index(wubi_index_path, wubi_entries));
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());

    cxxime::Dict pinyin_dict;
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(pinyin_dict.open(pinyin_path, pinyin_user_path));
    ASSERT_TRUE(wubi_dict.open_wubi_bundle(wubi_path, wubi_user_path, wubi_index_path));

    cxxime::MixedTranslator narrow_translator;
    narrow_translator.set_pinyin_dict(&pinyin_dict);
    narrow_translator.set_wubi_dict(&wubi_dict);
    narrow_translator.set_candidate_preference(cxxime::MixedCandidatePreference::kWubi);
    const auto narrow = narrow_translator.translate_page("a", 0, 3);

    cxxime::MixedTranslator wide_translator;
    wide_translator.set_pinyin_dict(&pinyin_dict);
    wide_translator.set_wubi_dict(&wubi_dict);
    wide_translator.set_candidate_preference(cxxime::MixedCandidatePreference::kWubi);
    const auto wide = wide_translator.translate_page("a", 0, 10);

    ASSERT_EQ(narrow.candidates.size(), 3u);
    ASSERT_GE(wide.candidates.size(), narrow.candidates.size());
    for (size_t index = 0; index < narrow.candidates.size(); ++index) {
        ASSERT_EQ(narrow.candidates[index].text, wide.candidates[index].text);
        ASSERT_EQ(narrow.candidates[index].source, wide.candidates[index].source);
    }
    ASSERT_EQ(narrow.candidates[0].text, "wubi-exact");
    ASSERT_EQ(narrow.candidates[1].text, "pinyin-one");
    ASSERT_EQ(narrow.candidates[2].text, "wubi-rank-4");

    pinyin_dict.close();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(wubi_index_path.c_str());
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
}

TEST(WubiEngine, mixed_order_places_manually_pinned_candidate_first) {
    const std::string pinyin_path = make_temp_path("test_mixed_manual_pinyin.bin");
    const std::string wubi_path = make_temp_path("test_mixed_manual_wubi.bin");
    const std::string wubi_index_path = wubi_path + ".idx";
    const std::string pinyin_user_path = pinyin_path + ".user.tsv";
    const std::string wubi_user_path = wubi_path + ".user.tsv";
    const std::string pinyin_order_path = pinyin_path + ".order.tsv";
    const std::vector<std::tuple<std::string, std::string, int>> pinyin_entries = {
        {"a", "pinyin-one", 900},
        {"a", "pinyin-two", 800},
    };
    const std::vector<std::tuple<std::string, std::string, int>> wubi_entries = {
        {"a", "wubi-one", 1000},
        {"aa", "wubi-two", 700},
    };
    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, pinyin_entries));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_entries));
    ASSERT_TRUE(cxxime::test::create_test_wubi_index(wubi_index_path, wubi_entries));
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
    DeleteFileA(pinyin_order_path.c_str());

    cxxime::Dict pinyin_dict;
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(pinyin_dict.open(pinyin_path, pinyin_user_path));
    ASSERT_TRUE(wubi_dict.open_wubi_bundle(wubi_path, wubi_user_path, wubi_index_path));
    ASSERT_TRUE(
        pinyin_dict.load_manual_candidate_order(pinyin_order_path, cxxime::kMaxInputCodeLength));
    ASSERT_TRUE(pinyin_dict.replace_manual_candidate_order_and_save(
        "a", {{"pinyin-two", "a", "a"}}));

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&pinyin_dict);
    translator.set_wubi_dict(&wubi_dict);
    translator.set_candidate_preference(cxxime::MixedCandidatePreference::kWubi);
    const auto page = translator.translate_page("a", 0, 4);

    ASSERT_EQ(page.candidates.size(), 4u);
    ASSERT_EQ(page.candidates[0].text, "pinyin-two");
    ASSERT_EQ(page.candidates[1].text, "wubi-one");

    pinyin_dict.close();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(wubi_index_path.c_str());
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
    DeleteFileA(pinyin_order_path.c_str());
}

TEST(WubiEngine, mixed_order_preserves_pinned_identity_across_text_deduplication) {
    const std::string pinyin_path = make_temp_path("test_mixed_dedup_pinyin.bin");
    const std::string wubi_path = make_temp_path("test_mixed_dedup_wubi.bin");
    const std::string wubi_index_path = wubi_path + ".idx";
    const std::string pinyin_user_path = pinyin_path + ".user.tsv";
    const std::string wubi_user_path = wubi_path + ".user.tsv";
    const std::string pinyin_order_path = pinyin_path + ".order.tsv";
    const std::string wubi_order_path = wubi_path + ".order.tsv";
    const std::vector<std::tuple<std::string, std::string, int>> pinyin_entries = {
        {"a", "shared-text", 900},
        {"a", "shared-second", 800},
    };
    const std::vector<std::tuple<std::string, std::string, int>> wubi_entries = {
        {"a", "shared-second", 1000},
        {"a", "shared-text", 700},
    };
    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, pinyin_entries));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_entries));
    ASSERT_TRUE(cxxime::test::create_test_wubi_index(wubi_index_path, wubi_entries));
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
    DeleteFileA(pinyin_order_path.c_str());
    DeleteFileA(wubi_order_path.c_str());

    cxxime::Dict pinyin_dict;
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(pinyin_dict.open(pinyin_path, pinyin_user_path));
    ASSERT_TRUE(wubi_dict.open_wubi_bundle(wubi_path, wubi_user_path, wubi_index_path));
    ASSERT_TRUE(
        pinyin_dict.load_manual_candidate_order(pinyin_order_path, cxxime::kMaxInputCodeLength));
    ASSERT_TRUE(wubi_dict.load_manual_candidate_order(wubi_order_path, cxxime::kMaxWubiCodeLength));
    ASSERT_TRUE(pinyin_dict.replace_manual_candidate_order_and_save(
        "a", {{"shared-text", "a", "a"}, {"shared-second", "a", "a"}}));

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&pinyin_dict);
    translator.set_wubi_dict(&wubi_dict);
    translator.set_candidate_preference(cxxime::MixedCandidatePreference::kWubi);
    const auto page = translator.translate_page("a", 0, 4);

    ASSERT_GE(page.candidates.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(page.candidates[0].text, "shared-text");
    ASSERT_EQ(page.candidates[0].source, cxxime::CandidateSource::kPinyin);
    ASSERT_EQ(page.candidates[1].text, "shared-second");
    ASSERT_EQ(page.candidates[1].source, cxxime::CandidateSource::kPinyin);

    ASSERT_TRUE(
        wubi_dict.replace_manual_candidate_order_and_save("a", {{"shared-text", "a", "a"}}));
    translator.set_candidate_preference(cxxime::MixedCandidatePreference::kAuto);
    const auto both_pinned = translator.translate_page("a", 0, 4);
    ASSERT_TRUE(!both_pinned.candidates.empty());
    ASSERT_EQ(both_pinned.candidates[0].text, "shared-text");
    ASSERT_EQ(both_pinned.candidates[0].source, cxxime::CandidateSource::kWubi);

    pinyin_dict.close();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(wubi_index_path.c_str());
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
    DeleteFileA(pinyin_order_path.c_str());
    DeleteFileA(wubi_order_path.c_str());
}

TEST(WubiEngine, mixed_order_ignores_stale_manual_entry_from_other_profile) {
    const std::string pinyin_path = make_temp_path("test_mixed_stale_pinyin.bin");
    const std::string wubi_path = make_temp_path("test_mixed_stale_wubi.bin");
    const std::string wubi_index_path = wubi_path + ".idx";
    const std::string pinyin_user_path = pinyin_path + ".user.tsv";
    const std::string wubi_user_path = wubi_path + ".user.tsv";
    const std::string pinyin_order_path = pinyin_path + ".order.tsv";
    const std::vector<std::tuple<std::string, std::string, int>> pinyin_entries = {
        {"a", "pinyin-first", 900},
    };
    const std::vector<std::tuple<std::string, std::string, int>> wubi_entries = {
        {"a", "shared-text", 800},
    };
    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, pinyin_entries));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, wubi_entries));
    ASSERT_TRUE(cxxime::test::create_test_wubi_index(wubi_index_path, wubi_entries));
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
    DeleteFileA(pinyin_order_path.c_str());

    cxxime::Dict pinyin_dict;
    cxxime::Dict wubi_dict;
    ASSERT_TRUE(pinyin_dict.open(pinyin_path, pinyin_user_path));
    ASSERT_TRUE(wubi_dict.open_wubi_bundle(wubi_path, wubi_user_path, wubi_index_path));
    ASSERT_TRUE(
        pinyin_dict.load_manual_candidate_order(pinyin_order_path, cxxime::kMaxInputCodeLength));
    ASSERT_TRUE(
        pinyin_dict.replace_manual_candidate_order_and_save("a", {{"shared-text", "a", "a"}}));

    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(&pinyin_dict);
    translator.set_wubi_dict(&wubi_dict);
    const auto page = translator.translate_page("a", 0, 2);

    ASSERT_EQ(page.candidates.size(), 2u);
    ASSERT_EQ(page.candidates[0].text, "pinyin-first");
    ASSERT_EQ(page.candidates[0].source, cxxime::CandidateSource::kPinyin);
    ASSERT_EQ(page.candidates[1].text, "shared-text");
    ASSERT_EQ(page.candidates[1].source, cxxime::CandidateSource::kWubi);

    pinyin_dict.close();
    wubi_dict.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
    DeleteFileA(wubi_index_path.c_str());
    DeleteFileA(pinyin_user_path.c_str());
    DeleteFileA(wubi_user_path.c_str());
    DeleteFileA(pinyin_order_path.c_str());
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
    auto page = trans.translate_page("", 0, 9);
    ASSERT_EQ(page.candidates.size(), 0u);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

int main() {
    GetTempPathA(MAX_PATH, wubi_engine_test_temp_path);
    return test::RunAllTests();
}
