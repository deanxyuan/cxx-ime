// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

// SessionManager integration tests: verify ProcessKeyResult, select_candidate,
// commit_composition, clear_composition, focus_out, and GET_STATUS with
// the OutputComposer pipeline.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <windows.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/key_event.h>
#include "../server/src/session_manager.h"

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

static std::string setup_test_dict() {
    std::string dict_path = make_temp_path("test_integration_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"ni", "你", 1000},
        {"hao", "好", 800},
        {"nihao", "你好", 900},
        {"de", "的", 700},
        {"di", "地", 600},
    });
    return dict_path;
}

static cxxime::KeyEvent make_key(uint32_t vk, bool shift = false, bool caps = false) {
    cxxime::KeyEvent e;
    e.keycode = vk;
    e.is_key_up = false;
    if (shift) e.set_shift();
    if (caps) e.set_caps_lock();
    return e;
}

// ============================================================
// process_key tests
// ============================================================

TEST(SessionIntegration, process_key_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    // Type a letter in Chinese mode
    auto r = mgr.process_key(id, make_key('N'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(r.composing);
}

TEST(SessionIntegration, process_key_invalid_session) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto r = mgr.process_key(999, make_key('N'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionIntegration, process_key_committed_has_ime_status) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    // Toggle to English mode
    mgr.toggle_chinese(id);

    // Type a letter in English mode → should be committed
    auto r = mgr.process_key(id, make_key('A'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    // ime_status should be filled
    ASSERT_EQ(r.ime_status.chinese_mode, false);
}

TEST(SessionIntegration, english_capslock_letter_preserves_engine_case) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.toggle_chinese(id);

    auto upper = mgr.process_key(id, make_key('N', false, true));
    ASSERT_EQ(upper.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(upper.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(upper.commit_text, "N");

    auto lower = mgr.process_key(id, make_key('N', true, true));
    ASSERT_EQ(lower.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(lower.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(lower.commit_text, "n");
}

TEST(SessionIntegration, english_capslock_keeps_english_and_outputs_uppercase) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.toggle_chinese(id);

    std::string text;
    for (char ch : std::string("NIHAO")) {
        auto r = mgr.process_key(id, make_key(ch, false, true));
        ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
        ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
        ASSERT_TRUE(!r.composing);
        text += r.commit_text;
    }

    ASSERT_EQ(text, "NIHAO");
}

TEST(SessionIntegration, append_enter_preserves_case_through_output_composer) {
    std::string cfg_path = make_temp_path("test_append_config.json");
    {
        std::ofstream f(cfg_path);
        f << R"({"ascii_composer":{"switch_key":{"Caps_Lock":"append"},"good_old_caps_lock":false}})";
    }

    SessionManager mgr;
    mgr.initialize(setup_test_dict(), cfg_path);
    uint32_t id = mgr.create_session();

    mgr.process_key(id, make_key('N'));
    mgr.process_key(id, make_key('I'));

    cxxime::KeyEvent caps;
    caps.keycode = 0x14;
    caps.is_key_up = false;
    caps.set_caps_lock();
    mgr.process_key(id, caps);

    mgr.process_key(id, make_key('D', false, true));
    mgr.process_key(id, make_key('D', false, true));

    cxxime::KeyEvent enter;
    enter.keycode = 0x0D;
    enter.is_key_up = false;
    enter.set_caps_lock();
    auto r = mgr.process_key(id, enter);

    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "niDD");

    DeleteFileA(cfg_path.c_str());
}

// ============================================================
// select_candidate tests
// ============================================================

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
    auto st = mgr.clear_composition(id);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);
}

TEST(SessionIntegration, clear_composition_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto st = mgr.clear_composition(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
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

    auto st = mgr.focus_out(id);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);

    // revision should increment
    auto [st1, s1] = mgr.get_ime_status(id);
    ASSERT_EQ(s1.revision, rev_before + 1);
}

TEST(SessionIntegration, focus_out_invalid) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());

    auto st = mgr.focus_out(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
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
    ASSERT_EQ(s.chinese_mode, true);
    ASSERT_EQ(s.full_shape, false);
    ASSERT_EQ(s.chinese_punct, true);
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
// Multiple sessions independence
// ============================================================

TEST(SessionIntegration, independent_sessions_output_composer) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();

    // Session 1: English + full_shape
    mgr.toggle_chinese(id1);
    mgr.toggle_shape(id1);

    // Session 2: Chinese mode (default)
    // Type "ni" in session 2
    mgr.process_key(id2, make_key('N'));
    auto r2 = mgr.process_key(id2, make_key('I'));
    ASSERT_TRUE(r2.composing);

    // Session 1: press digit → intercepted
    auto r1 = mgr.process_key(id1, make_key('5'));
    ASSERT_EQ(r1.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r1.commit_text, "５");
}

// ============================================================
// Punctuation IPC integration
// ============================================================

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

// Initialize temp_path before tests run
static bool _integration_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

RUN_ALL_TESTS()
