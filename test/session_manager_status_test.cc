// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
#include <cxxime/ipc_protocol.h>
#include "../server/src/session_manager.h"

static char temp_path[MAX_PATH] = {};

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

// ============================================================
// Toggle Tests
// ============================================================

TEST(SessionStatus, toggle_chinese) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto s = mgr.get_ime_status(id);
    ASSERT_EQ(s.chinese_mode, true);
    ASSERT_EQ(s.revision, (uint64_t)0);

    s = mgr.toggle_chinese(id);
    ASSERT_EQ(s.chinese_mode, false);
    ASSERT_EQ(s.revision, (uint64_t)1);

    s = mgr.toggle_chinese(id);
    ASSERT_EQ(s.chinese_mode, true);
    ASSERT_EQ(s.revision, (uint64_t)2);
}

TEST(SessionStatus, toggle_shape) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto s = mgr.get_ime_status(id);
    ASSERT_EQ(s.full_shape, false);

    s = mgr.toggle_shape(id);
    ASSERT_EQ(s.full_shape, true);
    ASSERT_EQ(s.revision, (uint64_t)1);

    s = mgr.toggle_shape(id);
    ASSERT_EQ(s.full_shape, false);
    ASSERT_EQ(s.revision, (uint64_t)2);
}

TEST(SessionStatus, toggle_punct) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto s = mgr.get_ime_status(id);
    ASSERT_EQ(s.chinese_punct, true);

    s = mgr.toggle_punct(id);
    ASSERT_EQ(s.chinese_punct, false);
    ASSERT_EQ(s.revision, (uint64_t)1);

    s = mgr.toggle_punct(id);
    ASSERT_EQ(s.chinese_punct, true);
    ASSERT_EQ(s.revision, (uint64_t)2);
}

TEST(SessionStatus, switch_input_mode) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto s = mgr.get_ime_status(id);
    ASSERT_EQ(s.input_mode, cxxime::InputMode::PINYIN);

    s = mgr.switch_input_mode(id);
    ASSERT_EQ(s.input_mode, cxxime::InputMode::WUBI);
    ASSERT_EQ(s.revision, (uint64_t)1);

    s = mgr.switch_input_mode(id);
    ASSERT_EQ(s.input_mode, cxxime::InputMode::PINYIN);
    ASSERT_EQ(s.revision, (uint64_t)2);
}

TEST(SessionStatus, sync_caps_lock_sets_current_state) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    auto s = mgr.get_ime_status(id);
    ASSERT_EQ(s.caps_lock, false);

    mgr.sync_caps_lock(id, true);
    s = mgr.get_ime_status(id);
    ASSERT_EQ(s.caps_lock, true);

    mgr.sync_caps_lock(id, true);
    s = mgr.get_ime_status(id);
    ASSERT_EQ(s.caps_lock, true);

    mgr.sync_caps_lock(id, false);
    s = mgr.get_ime_status(id);
    ASSERT_EQ(s.caps_lock, false);
}

TEST(SessionStatus, get_ime_status) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    mgr.toggle_chinese(id);
    mgr.toggle_shape(id);

    auto s = mgr.get_ime_status(id);
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
    auto s = mgr.toggle_chinese(999);
    ASSERT_EQ(s.revision, (uint64_t)0);
}

TEST(SessionStatus, invalid_session_toggle_shape) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto s = mgr.toggle_shape(999);
    ASSERT_EQ(s.revision, (uint64_t)0);
}

TEST(SessionStatus, invalid_session_toggle_punct) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto s = mgr.toggle_punct(999);
    ASSERT_EQ(s.revision, (uint64_t)0);
}

TEST(SessionStatus, invalid_session_switch_input_mode) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto s = mgr.switch_input_mode(999);
    ASSERT_EQ(s.revision, (uint64_t)0);
}

TEST(SessionStatus, invalid_session_get_ime_status) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    auto s = mgr.get_ime_status(999);
    ASSERT_EQ(s.revision, (uint64_t)0);
    ASSERT_EQ(s.chinese_mode, true);
    ASSERT_EQ(s.full_shape, false);
    ASSERT_EQ(s.chinese_punct, true);
    ASSERT_EQ(s.input_mode, cxxime::InputMode::PINYIN);
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

    auto s1 = mgr.get_ime_status(id1);
    auto s2 = mgr.get_ime_status(id2);
    ASSERT_EQ(s1.chinese_mode, false);
    ASSERT_EQ(s1.full_shape, false);
    ASSERT_EQ(s2.chinese_mode, true);
    ASSERT_EQ(s2.full_shape, true);
}

RUN_ALL_TESTS()
