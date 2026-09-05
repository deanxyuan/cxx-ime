// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager_integration_test_support.h"
TEST(SessionIntegration, reload_dictionaries_updates_active_session) {
    std::string dict_path = make_temp_path("test_hot_reload_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"kao", "reload-old", 100},
    });

    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path));
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto old_result = type_kao(mgr, id);
    ASSERT_EQ(old_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(old_result.composing);
    ASSERT_TRUE(candidate_contains(old_result.presentation, "reload-old"));

    ASSERT_EQ(mgr.clear_composition(id).status, cxxime::IPCStatus::OK);
    create_test_dictionary_bundle(dict_path, {
        {"kao", "reload-new", 100},
    });
    ASSERT_EQ(mgr.reload_dictionaries(), cxxime::IPCStatus::OK);

    auto new_result = type_kao(mgr, id);
    ASSERT_EQ(new_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(new_result.composing);
    ASSERT_TRUE(candidate_contains(new_result.presentation, "reload-new"));
    ASSERT_TRUE(!candidate_contains(new_result.presentation, "reload-old"));

    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, dictionary_monitor_reload_updates_active_session) {
    std::string dict_path = make_temp_path("test_auto_hot_reload_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"kao", "auto-hot-reload-old", 100},
    });

    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path));
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto old_result = type_kao(mgr, id);
    ASSERT_EQ(old_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(old_result.composing);
    ASSERT_TRUE(candidate_contains(old_result.presentation, "auto-hot-reload-old"));
    ASSERT_EQ(mgr.clear_composition(id).status, cxxime::IPCStatus::OK);

    std::atomic<int> reload_count{0};
    cxxime::DictionaryMonitorOptions options;
    options.debounce_ms = 20;
    options.poll_ms = 100;
    options.retry_ms = 50;
    options.max_retries = 5;

    cxxime::DictionaryMonitor monitor;
    std::string manifest_path = cxxime::dictionary_manifest_path_for_dict(dict_path);
    ASSERT_TRUE(monitor.start({manifest_path}, [&] {
        auto status = mgr.reload_dictionaries();
        if (status == cxxime::IPCStatus::OK)
            reload_count.fetch_add(1);
        return status == cxxime::IPCStatus::OK;
    }, options));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    create_test_dictionary_bundle(dict_path, {
        {"kao", "auto-hot-reload-new-value", 100},
    });

    ASSERT_TRUE(wait_for_count(reload_count, 1, 3000));
    monitor.stop();

    auto new_result = type_kao(mgr, id);
    ASSERT_EQ(new_result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(new_result.composing);
    ASSERT_TRUE(candidate_contains(new_result.presentation, "auto-hot-reload-new-value"));
    ASSERT_TRUE(!candidate_contains(new_result.presentation, "auto-hot-reload-old"));

    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, reload_dictionaries_failure_keeps_active_session_resources) {
    std::string dict_path = make_temp_path("test_hot_reload_failure_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"kao", "reload-still-live", 100},
    });

    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path));
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    ASSERT_EQ(mgr.clear_composition(id).status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(DeleteFileA(dict_path.c_str()));
    ASSERT_EQ(mgr.reload_dictionaries(), cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED);

    auto result = type_kao(mgr, id);
    ASSERT_EQ(result.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(result.composing);
    ASSERT_TRUE(candidate_contains(result.presentation, "reload-still-live"));
}

static std::string write_temp_punct_json(const char* name, const char* content) {
    std::string path = make_temp_path(name);
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

// Test 1: Hot-reload punctuation mapping
TEST(SessionIntegration, punctuation_hot_reload) {
    // Write initial punctuation.json: "." → "。"
    std::string punct_path = write_temp_punct_json("punct_hot.json",
        R"({"half_shape": {".": {"commit": "。"}}})");

    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    // Load custom punctuation file
    ASSERT_TRUE(mgr.reload_punctuation(punct_path));

    uint32_t id = mgr.create_session();
    // Chinese mode + chinese_punct=true (default)
    // Press '.' → should map to "。"
    auto r = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "。");

    // Write modified punctuation.json: "." → "！"
    write_temp_punct_json("punct_hot.json",
        R"({"half_shape": {".": {"commit": "！"}}})");

    // Reload — new mapping should take effect
    ASSERT_TRUE(mgr.reload_punctuation(punct_path));

    auto r2 = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r2.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r2.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r2.commit_text, "！");
}

// Test 2: Multi-session quote pair_open state isolation
TEST(SessionIntegration, punctuation_pair_state_isolation) {
    std::string punct_path = write_temp_punct_json("punct_pair.json",
        R"({"half_shape": {"'": {"pair": ["'", "'"]}}})");

    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    ASSERT_TRUE(mgr.reload_punctuation(punct_path));

    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();

    // Session 1: first ' → left quote "'"
    auto r1a = mgr.process_key(id1, make_key(VK_OEM_7));
    ASSERT_EQ(r1a.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r1a.commit_text, "'");

    // Session 1: second ' → right quote "'"
    auto r1b = mgr.process_key(id1, make_key(VK_OEM_7));
    ASSERT_EQ(r1b.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r1b.commit_text, "'");

    // Session 2: first ' → left quote "'" (independent state, not affected by session 1)
    auto r2a = mgr.process_key(id2, make_key(VK_OEM_7));
    ASSERT_EQ(r2a.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r2a.commit_text, "'");

    // Session 1: third ' → left quote "'" again (alternation continues)
    auto r1c = mgr.process_key(id1, make_key(VK_OEM_7));
    ASSERT_EQ(r1c.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r1c.commit_text, "'");
}

// Test 3: Punctuation committed via IPC process_key
TEST(SessionIntegration, punctuation_commit_via_ipc) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Chinese mode (default): chinese_punct=true
    // Press '.' → should commit "。"
    auto r = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "。");
    ASSERT_EQ(r.composing, false);

    // Press ',' → should commit "，"
    auto r2 = mgr.process_key(id, make_key(VK_OEM_COMMA));
    ASSERT_EQ(r2.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r2.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r2.commit_text, "，");

    // Toggle chinese_punct off → punctuation should be rejected (pass-through)
    mgr.toggle_punct(id);
    auto r3 = mgr.process_key(id, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(r3.status, cxxime::IPCStatus::OK);
    // With chinese_punct=false, punctuation is not mapped → REJECTED
    ASSERT_EQ(r3.result, cxxime::ProcessResult::REJECTED);
}
