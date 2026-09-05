// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager_integration_test_support.h"
TEST(SessionIntegration, select_candidate_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    // Type "ni" to get candidates
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Select first candidate
    auto r = mgr.select_candidate(id, 0);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_TRUE(!r.commit_text.empty());
}

TEST(SessionIntegration, select_candidate_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto r = mgr.select_candidate(999, 0);
    ASSERT_EQ(r.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionIntegration, select_candidate_candidate_no_conversion) {
    // kCandidate source should not apply CapsLock/full-width conversion
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Type "ni" to get candidates
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Enable full_shape and caps_lock before selection.
    mgr.toggle_shape(id);
    mgr.sync_caps_lock(id, true);

    // Select first candidate — kCandidate source, no conversion
    auto r = mgr.select_candidate(id, 0);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    // Candidate text should be preserved as-is (no full-width, no case inversion)
    ASSERT_TRUE(!r.commit_text.empty());
    // With kCandidate, transform returns text unchanged — verify it's a valid Chinese string
    // (not full-width ASCII, which would start with 0xEF byte)
    ASSERT_TRUE(r.commit_text[0] != '\xEF');
}

// ============================================================
// commit_composition tests
// ============================================================

TEST(SessionIntegration, commit_composition_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Type "ni" to start composing
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Commit composition
    auto r = mgr.commit_composition(id);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.composing, false);
}

TEST(SessionIntegration, commit_composition_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto r = mgr.commit_composition(999);
    ASSERT_EQ(r.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionIntegration, set_chinese_mode_commits_raw_composition_once) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    auto unchanged = mgr.set_chinese_mode(id, true);
    ASSERT_EQ(unchanged.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(unchanged.commit_text.empty());
    ASSERT_EQ(unchanged.composing, true);
    ASSERT_EQ(unchanged.ime_status.chinese_mode(), true);

    auto first = mgr.set_chinese_mode(id, false);
    ASSERT_EQ(first.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(first.commit_text, "ni");
    ASSERT_EQ(first.composing, false);
    ASSERT_EQ(first.ime_status.chinese_mode(), false);

    auto second = mgr.set_chinese_mode(id, false);
    ASSERT_EQ(second.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(second.commit_text.empty());
    ASSERT_EQ(second.composing, false);
    ASSERT_EQ(second.ime_status.revision, first.ime_status.revision);
}

TEST(SessionIntegration, set_chinese_mode_invalid_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto result = mgr.set_chinese_mode(999, false);
    ASSERT_EQ(result.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// clear_composition tests
// ============================================================

TEST(SessionIntegration, clear_composition_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Type something
    mgr.process_key(id, make_key('N'));

    // Clear
    auto result = mgr.clear_composition(id);
    ASSERT_EQ(result.status, cxxime::IPCStatus::OK);
}

TEST(SessionIntegration, clear_composition_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto result = mgr.clear_composition(999);
    ASSERT_EQ(result.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// focus_out tests
// ============================================================

TEST(SessionIntegration, focus_out_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    auto [st0, s0] = mgr.get_ime_status(id);
    uint64_t rev_before = s0.revision;

    auto result = mgr.focus_out(id);
    ASSERT_EQ(result.status, cxxime::IPCStatus::OK);

    // focus_out only clears composition; it does not change this session's visible state.
    auto [st1, s1] = mgr.get_ime_status(id);
    ASSERT_EQ(s1.revision, rev_before);
}

TEST(SessionIntegration, focus_out_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto result = mgr.focus_out(999);
    ASSERT_EQ(result.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// GET_STATUS tests
// ============================================================

TEST(SessionIntegration, get_status_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    auto [st, s] = mgr.get_ime_status(id);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);
    ASSERT_EQ(s.chinese_mode(), true);
    ASSERT_EQ(s.full_shape(), false);
    ASSERT_EQ(s.chinese_punct(), true);
}

TEST(SessionIntegration, get_status_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto [st, s] = mgr.get_ime_status(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// OutputComposer integration (full_shape intercept_key path)
// ============================================================

TEST(SessionIntegration, english_fullwidth_digit_intercepted) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Switch to English mode
    mgr.toggle_chinese(id);
    // Enable full_shape
    mgr.toggle_shape(id);

    // Press digit key '1' → should be intercepted by OutputComposer
    auto r = mgr.process_key(id, make_key('1'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "１");
    ASSERT_EQ(r.composing, false);
}

TEST(SessionIntegration, chinese_mode_digit_not_intercepted) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Chinese mode + full_shape → digit should NOT be intercepted
    mgr.toggle_shape(id);

    // Type "ni" first to get candidates
    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    // Press digit '1' → selects candidate, not intercepted
    auto r = mgr.process_key(id, make_key('1'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    // Should be a candidate, not "１"
    ASSERT_TRUE(!r.commit_text.empty());
}

// ============================================================
// Multiple sessions visible state
// ============================================================

TEST(SessionIntegration, applied_initial_state_applies_only_to_new_sessions) {
    std::string config_path = make_temp_path("test_initial_state_config.json");
    {
        std::ofstream config(config_path);
        config << R"({"initial_state":{"full_shape":false,"chinese_punct":true}})";
    }

    auto initial_config = std::make_shared<cxxime::Config>();
    ASSERT_TRUE(initial_config->load(config_path));
    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(setup_test_dict(), initial_config));
    uint32_t existing = mgr.create_session();
    ASSERT_GT(existing, (uint32_t)0);

    {
        std::ofstream config(config_path);
        config << R"({"initial_state":{"full_shape":true,"chinese_punct":false}})";
    }
    auto updated_config = std::make_shared<cxxime::Config>();
    ASSERT_TRUE(updated_config->load(config_path));
    mgr.apply_config(updated_config);

    auto [existing_status_result, existing_status] = mgr.get_ime_status(existing);
    ASSERT_EQ(existing_status_result, cxxime::IPCStatus::OK);
    ASSERT_EQ(existing_status.full_shape(), false);
    ASSERT_EQ(existing_status.chinese_punct(), true);

    uint32_t created_after_reload = mgr.create_session();
    ASSERT_GT(created_after_reload, (uint32_t)0);
    auto [new_status_result, new_status] = mgr.get_ime_status(created_after_reload);
    ASSERT_EQ(new_status_result, cxxime::IPCStatus::OK);
    ASSERT_EQ(new_status.full_shape(), true);
    ASSERT_EQ(new_status.chinese_punct(), false);

    DeleteFileA(config_path.c_str());
}

TEST(SessionIntegration, session_shape_change_does_not_affect_other_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();

    // Session 1 changes its own language and full-shape modes.
    mgr.toggle_chinese(id1);
    mgr.toggle_shape(id1);

    auto [st, status] = mgr.get_ime_status(id2);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);
    ASSERT_EQ(status.chinese_mode(), true);
    ASSERT_EQ(status.full_shape(), false);

    auto full_width = mgr.process_key(id1, make_key('5'));
    ASSERT_EQ(full_width.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(full_width.commit_text, cxxime::OutputComposer::to_full_width('5'));

    auto half_width = mgr.process_key(id2, make_key('5'));
    ASSERT_EQ(half_width.result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(half_width.commit_text.empty());
    ASSERT_EQ(half_width.ime_status.chinese_mode(), true);
    ASSERT_EQ(half_width.ime_status.full_shape(), false);
}

TEST(SessionIntegration, session_punctuation_change_does_not_affect_other_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();

    mgr.toggle_punct(id1);

    auto english_punct = mgr.process_key(id1, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(english_punct.result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(english_punct.commit_text.empty());

    auto chinese_punct = mgr.process_key(id2, make_key(VK_OEM_PERIOD));
    ASSERT_EQ(chinese_punct.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(chinese_punct.commit_text, "。");
}
