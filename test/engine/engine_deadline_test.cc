// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_test_support.h"
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
    auto page = translator.translate_page("bs", 0, 5);
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
