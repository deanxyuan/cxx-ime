// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager_integration_test_support.h"

TEST(SessionLanguage, changing_base_language_under_caps_preserves_other_sessions) {
    enum class Change { Toggle, SetChinese, SetEnglish };
    for (auto change : {Change::Toggle, Change::SetChinese, Change::SetEnglish}) {
        for (bool initial : {false, true}) {
            for (bool other : {false, true}) {
                SessionManager manager;
                ASSERT_TRUE(manager.initialize(setup_test_dict()));
                const auto a = manager.create_session();
                const auto b = manager.create_session();
                ASSERT_EQ(manager.set_chinese_mode(a, initial).status, cxxime::IPCStatus::OK);
                ASSERT_EQ(manager.set_chinese_mode(b, other).status, cxxime::IPCStatus::OK);
                ASSERT_EQ(manager.sync_caps_lock(b, true).first, cxxime::IPCStatus::OK);
                bool expected = initial;
                if (change == Change::Toggle) {
                    ASSERT_EQ(manager.toggle_chinese(a).first, cxxime::IPCStatus::OK);
                    expected = !initial;
                } else {
                    expected = change == Change::SetChinese;
                    // Explicit setting is idempotent, including underneath the overlay.
                    for (int repeat = 0; repeat < 2; ++repeat) {
                        ASSERT_EQ(manager.set_chinese_mode(a, expected).status,
                                  cxxime::IPCStatus::OK);
                    }
                }
                for (auto id : {a, b}) {
                    const auto status = manager.get_ime_status(id);
                    ASSERT_EQ(status.first, cxxime::IPCStatus::OK);
                    ASSERT_TRUE(status.second.caps_lock());
                    ASSERT_TRUE(!status.second.chinese_mode());
                }
                ASSERT_EQ(manager.sync_caps_lock(a, false).first, cxxime::IPCStatus::OK);
                ASSERT_EQ(manager.get_ime_status(a).second.chinese_mode(), expected);
                ASSERT_EQ(manager.get_ime_status(b).second.chinese_mode(), other);
                ASSERT_TRUE(!manager.get_ime_status(b).second.caps_lock());
            }
        }
    }
}
