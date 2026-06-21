// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <windows.h>
#include <cxxime/ipc_protocol.h>
#include "../server/src/session_manager.h"

static char temp_path[MAX_PATH] = {};

static bool _status_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

static std::string setup_test_dict() {
    std::string dict_path = make_temp_path("test_status_dict.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"ni", "你", 1000},
        {"hao", "好", 800},
    });
    return dict_path;
}

static std::string setup_capslock_config() {
    std::string config_path = make_temp_path("test_capslock_config.json");
    std::ofstream out(config_path);
    out << R"({
        "ascii_composer": {
            "switch_key": {
                "Caps_Lock": "clear"
            }
        }
    })";
    return config_path;
}

// ============================================================
// Toggle Tests
// ============================================================

TEST(SessionStatus, toggle_chinese) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto [st0, s0] = mgr.get_ime_status(id);
    ASSERT_EQ(st0, cxxime::IPCStatus::OK);
    ASSERT_EQ(s0.chinese_mode, true);
    ASSERT_EQ(s0.revision, (uint64_t)0);

    auto [st1, s1] = mgr.toggle_chinese(id);
    ASSERT_EQ(st1, cxxime::IPCStatus::OK);
    ASSERT_EQ(s1.chinese_mode, false);
    ASSERT_EQ(s1.revision, (uint64_t)1);

    auto [st2, s2] = mgr.toggle_chinese(id);
    ASSERT_EQ(s2.chinese_mode, true);
    ASSERT_EQ(s2.revision, (uint64_t)2);
}

TEST(SessionStatus, toggle_shape) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto [st0, s0] = mgr.get_ime_status(id);
    ASSERT_EQ(s0.full_shape, false);

    auto [st1, s1] = mgr.toggle_shape(id);
    ASSERT_EQ(st1, cxxime::IPCStatus::OK);
    ASSERT_EQ(s1.full_shape, true);
    ASSERT_EQ(s1.revision, (uint64_t)1);

    auto [st2, s2] = mgr.toggle_shape(id);
    ASSERT_EQ(s2.full_shape, false);
    ASSERT_EQ(s2.revision, (uint64_t)2);
}

TEST(SessionStatus, toggle_punct) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto [st0, s0] = mgr.get_ime_status(id);
    ASSERT_EQ(s0.chinese_punct, true);

    auto [st1, s1] = mgr.toggle_punct(id);
    ASSERT_EQ(st1, cxxime::IPCStatus::OK);
    ASSERT_EQ(s1.chinese_punct, false);
    ASSERT_EQ(s1.revision, (uint64_t)1);

    auto [st2, s2] = mgr.toggle_punct(id);
    ASSERT_EQ(s2.chinese_punct, true);
    ASSERT_EQ(s2.revision, (uint64_t)2);
}

TEST(SessionStatus, switch_input_mode) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto [st0, s0] = mgr.get_ime_status(id);
    ASSERT_EQ(s0.input_mode, cxxime::InputMode::PINYIN);

    auto [st1, s1] = mgr.switch_input_mode(id);
    ASSERT_EQ(st1, cxxime::IPCStatus::OK);
    ASSERT_EQ(s1.input_mode, cxxime::InputMode::WUBI);
    ASSERT_EQ(s1.revision, (uint64_t)1);

    auto [st2, s2] = mgr.switch_input_mode(id);
    ASSERT_EQ(s2.input_mode, cxxime::InputMode::PINYIN);
    ASSERT_EQ(s2.revision, (uint64_t)2);
}

TEST(SessionStatus, sync_caps_lock_sets_current_state) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto [st0, s0] = mgr.get_ime_status(id);
    ASSERT_EQ(s0.caps_lock, false);

    auto [st1, caps_on] = mgr.sync_caps_lock(id, true);
    ASSERT_EQ(st1, cxxime::IPCStatus::OK);
    ASSERT_EQ(caps_on.caps_lock, true);
    auto [st2, s1] = mgr.get_ime_status(id);
    ASSERT_EQ(s1.caps_lock, true);

    mgr.sync_caps_lock(id, true);
    auto [st3, s2] = mgr.get_ime_status(id);
    ASSERT_EQ(s2.caps_lock, true);

    mgr.sync_caps_lock(id, false);
    auto [st4, s3] = mgr.get_ime_status(id);
    ASSERT_EQ(s3.caps_lock, false);
}

TEST(SessionStatus, sync_caps_lock_enables_ascii_overlay) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict(), setup_capslock_config());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto [st1, s1] = mgr.sync_caps_lock(id, true);
    ASSERT_EQ(st1, cxxime::IPCStatus::OK);
    ASSERT_EQ(s1.caps_lock, true);
    ASSERT_EQ(s1.chinese_mode, false);

    cxxime::KeyEvent letter;
    letter.keycode = 'N';
    letter.set_caps_lock();
    auto r = mgr.process_key(id, letter);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "N");
    ASSERT_TRUE(!r.composing);

    auto [st2, s2] = mgr.sync_caps_lock(id, false);
    ASSERT_EQ(st2, cxxime::IPCStatus::OK);
    ASSERT_EQ(s2.caps_lock, false);
    ASSERT_EQ(s2.chinese_mode, true);
}

TEST(SessionStatus, first_key_with_caps_lock_on_enables_ascii_overlay) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict(), setup_capslock_config());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    cxxime::KeyEvent letter;
    letter.keycode = 'N';
    letter.set_caps_lock();

    auto r = mgr.process_key(id, letter);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(r.commit_text, "N");
    ASSERT_TRUE(!r.composing);
    ASSERT_EQ(r.ime_status.caps_lock, true);
    ASSERT_EQ(r.ime_status.chinese_mode, false);
}

TEST(SessionStatus, caps_lock_key_off_restores_chinese_overlay) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict(), setup_capslock_config());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto [st1, s1] = mgr.sync_caps_lock(id, true);
    ASSERT_EQ(st1, cxxime::IPCStatus::OK);
    ASSERT_EQ(s1.caps_lock, true);
    ASSERT_EQ(s1.chinese_mode, false);

    cxxime::KeyEvent caps_off;
    caps_off.keycode = VK_CAPITAL;
    caps_off.is_key_up = false;
    auto r = mgr.process_key(id, caps_off);
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.ime_status.caps_lock, false);
    ASSERT_EQ(r.ime_status.chinese_mode, true);
}

TEST(SessionStatus, get_ime_status) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    mgr.toggle_chinese(id);
    mgr.toggle_shape(id);

    auto [st, s] = mgr.get_ime_status(id);
    ASSERT_EQ(st, cxxime::IPCStatus::OK);
    ASSERT_EQ(s.chinese_mode, false);
    ASSERT_EQ(s.full_shape, true);
    ASSERT_EQ(s.chinese_punct, true);
    ASSERT_EQ(s.input_mode, cxxime::InputMode::PINYIN);
    ASSERT_EQ(s.revision, (uint64_t)2);
}

// ============================================================
// Invalid Session Tests
// ============================================================

TEST(SessionStatus, invalid_session_toggle_chinese) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto [st, s] = mgr.toggle_chinese(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionStatus, invalid_session_toggle_shape) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto [st, s] = mgr.toggle_shape(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionStatus, invalid_session_toggle_punct) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto [st, s] = mgr.toggle_punct(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionStatus, invalid_session_switch_input_mode) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto [st, s] = mgr.switch_input_mode(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

TEST(SessionStatus, invalid_session_get_ime_status) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto [st, s] = mgr.get_ime_status(999);
    ASSERT_EQ(st, cxxime::IPCStatus::ERR_INVALID_SESSION);
}

// ============================================================
// Multiple Sessions
// ============================================================

TEST(SessionStatus, independent_sessions) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id1 = mgr.create_session();
    uint32_t id2 = mgr.create_session();
    ASSERT_GT(id1, (uint32_t)0);
    ASSERT_GT(id2, (uint32_t)0);

    mgr.toggle_chinese(id1);
    mgr.toggle_shape(id2);

    auto [st1, s1] = mgr.get_ime_status(id1);
    auto [st2, s2] = mgr.get_ime_status(id2);
    ASSERT_EQ(s1.chinese_mode, false);
    ASSERT_EQ(s1.full_shape, false);
    ASSERT_EQ(s2.chinese_mode, true);
    ASSERT_EQ(s2.full_shape, true);
}

RUN_ALL_TESTS()
