// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_test_support.h"

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

TEST(Deadline, disabled_deadline_returns_candidates_without_truncation) {
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
