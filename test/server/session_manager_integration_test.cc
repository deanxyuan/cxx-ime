// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <map>

#include "config_store.h"
#include "config_write_coordinator.h"
#include "session_manager_integration_test_support.h"

TEST(SessionIntegration, shape_and_punctuation_toggles_preserve_composition) {
    SessionManager manager;
    manager.initialize(setup_test_dict());
    const uint32_t id = manager.create_session();

    ASSERT_EQ(manager.process_key(id, make_key('N')).result, cxxime::ProcessResult::ACCEPTED);
    const ProcessKeyResult initial = manager.process_key(id, make_key('I'));
    ASSERT_TRUE(initial.composing);
    ASSERT_EQ(initial.preedit, "ni");
    ASSERT_TRUE(!initial.presentation.items.empty());

    const ProcessKeyResult shape = manager.process_key(id, make_key(VK_SPACE, true));
    ASSERT_EQ(shape.result, cxxime::ProcessResult::TOGGLE_SHAPE);
    ASSERT_TRUE(shape.composing);
    ASSERT_EQ(shape.preedit, initial.preedit);
    ASSERT_EQ(shape.preedit_cursor, initial.preedit_cursor);
    ASSERT_EQ(shape.presentation.items.size(), initial.presentation.items.size());
    ASSERT_TRUE(shape.ime_status.full_shape());

    cxxime::KeyEvent punctuation = make_key(VK_OEM_PERIOD);
    punctuation.set_ctrl();
    const ProcessKeyResult punct = manager.process_key(id, punctuation);
    ASSERT_EQ(punct.result, cxxime::ProcessResult::TOGGLE_PUNCT);
    ASSERT_TRUE(punct.composing);
    ASSERT_EQ(punct.preedit, initial.preedit);
    ASSERT_EQ(punct.preedit_cursor, initial.preedit_cursor);
    ASSERT_EQ(punct.presentation.items.size(), initial.presentation.items.size());
    ASSERT_TRUE(!punct.ime_status.chinese_punct());
}

TEST(SessionIntegration, inline_ascii_is_returned_as_uncommitted_preedit) {
    SessionManager manager;
    manager.initialize(setup_test_dict());
    const uint32_t id = manager.create_session();

    ASSERT_EQ(manager.process_key(id, make_key('N')).result, cxxime::ProcessResult::ACCEPTED);
    const ProcessKeyResult initial = manager.process_key(id, make_key('I'));
    ASSERT_TRUE(initial.composing);
    ASSERT_TRUE(!initial.presentation.items.empty());

    const ProcessKeyResult plus = manager.process_key(id, make_key(VK_OEM_PLUS, true));
    ASSERT_EQ(plus.result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(plus.composing);
    ASSERT_EQ(plus.preedit, "ni+");
    ASSERT_EQ(plus.preedit_cursor, static_cast<size_t>(3));
    ASSERT_TRUE(plus.presentation.items.empty());
    ASSERT_TRUE(plus.commit_text.empty());
}

TEST(SessionIntegration, inline_ascii_binding_restores_chinese_mode_after_commit) {
    auto config = std::make_shared<cxxime::Config>();
    config->ascii_switch_key["Shift_L"] = "inline_ascii";
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
    const uint32_t id = manager.create_session();

    ASSERT_EQ(manager.process_key(id, make_key('N')).result, cxxime::ProcessResult::ACCEPTED);
    const ProcessKeyResult initial = manager.process_key(id, make_key('I'));
    ASSERT_TRUE(initial.composing);
    ASSERT_TRUE(initial.ime_status.chinese_mode());

    ASSERT_EQ(manager.process_key(id, make_key(VK_LSHIFT, true)).result,
              cxxime::ProcessResult::REJECTED);
    cxxime::KeyEvent shift_up = make_key(VK_LSHIFT);
    shift_up.is_key_up = true;
    const ProcessKeyResult converted = manager.process_key(id, shift_up);
    ASSERT_EQ(converted.result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_TRUE(converted.composing);
    ASSERT_EQ(converted.preedit, "ni");
    ASSERT_TRUE(converted.presentation.items.empty());
    ASSERT_TRUE(converted.ime_status.chinese_mode());

    const ProcessKeyResult committed = manager.process_key(id, make_key(VK_RETURN));
    ASSERT_EQ(committed.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(committed.commit_text, "ni");
    ASSERT_TRUE(committed.ime_status.chinese_mode());

    const ProcessKeyResult resumed = manager.process_key(id, make_key('N'));
    ASSERT_TRUE(resumed.composing);
    ASSERT_TRUE(!resumed.presentation.items.empty());
    ASSERT_TRUE(resumed.ime_status.chinese_mode());
}

TEST(SessionIntegration, inline_ascii_binding_restores_chinese_mode_after_clear) {
    auto config = std::make_shared<cxxime::Config>();
    config->ascii_switch_key["Shift_L"] = "inline_ascii";
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
    const uint32_t id = manager.create_session();

    ASSERT_EQ(manager.process_key(id, make_key('N')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(manager.process_key(id, make_key('I')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(manager.process_key(id, make_key(VK_LSHIFT, true)).result,
              cxxime::ProcessResult::REJECTED);
    cxxime::KeyEvent shift_up = make_key(VK_LSHIFT);
    shift_up.is_key_up = true;
    ASSERT_EQ(manager.process_key(id, shift_up).result, cxxime::ProcessResult::ACCEPTED);

    ASSERT_EQ(manager.clear_composition(id).status, cxxime::IPCStatus::OK);
    const ProcessKeyResult resumed = manager.process_key(id, make_key('N'));
    ASSERT_TRUE(resumed.composing);
    ASSERT_TRUE(!resumed.presentation.items.empty());
    ASSERT_TRUE(resumed.ime_status.chinese_mode());
}

TEST(SessionIntegration, inline_ascii_binding_restores_chinese_mode_after_focus_out) {
    auto config = std::make_shared<cxxime::Config>();
    config->ascii_switch_key["Shift_L"] = "inline_ascii";
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
    const uint32_t id = manager.create_session();

    ASSERT_EQ(manager.process_key(id, make_key('N')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(manager.process_key(id, make_key('I')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(manager.process_key(id, make_key(VK_LSHIFT, true)).result,
              cxxime::ProcessResult::REJECTED);
    cxxime::KeyEvent shift_up = make_key(VK_LSHIFT);
    shift_up.is_key_up = true;
    ASSERT_EQ(manager.process_key(id, shift_up).result, cxxime::ProcessResult::ACCEPTED);

    ASSERT_EQ(manager.focus_out(id).status, cxxime::IPCStatus::OK);
    const ProcessKeyResult resumed = manager.process_key(id, make_key('N'));
    ASSERT_TRUE(resumed.composing);
    ASSERT_TRUE(!resumed.presentation.items.empty());
    ASSERT_TRUE(resumed.ime_status.chinese_mode());
}

TEST(SessionIntegration, clear_binding_cancels_composition_on_modifier_key_up) {
    auto config = std::make_shared<cxxime::Config>();
    config->ascii_switch_key["Shift_L"] = "clear";
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
    const uint32_t id = manager.create_session();

    ASSERT_EQ(manager.process_key(id, make_key('N')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(manager.process_key(id, make_key('I')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(manager.process_key(id, make_key(VK_LSHIFT, true)).result,
              cxxime::ProcessResult::REJECTED);
    cxxime::KeyEvent shift_up = make_key(VK_LSHIFT);
    shift_up.is_key_up = true;
    const ProcessKeyResult cleared = manager.process_key(id, shift_up);
    ASSERT_EQ(cleared.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(cleared.result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(!cleared.composing);
    ASSERT_TRUE(!cleared.ime_status.chinese_mode());
}

TEST(SessionIntegration, inline_ascii_binding_restores_chinese_mode_after_deleting_preedit) {
    auto config = std::make_shared<cxxime::Config>();
    config->ascii_switch_key["Shift_L"] = "inline_ascii";
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict(), config));

    for (bool use_delete : {false, true}) {
        const uint32_t id = manager.create_session();
        ASSERT_EQ(manager.process_key(id, make_key('N')).result, cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(manager.process_key(id, make_key(VK_LSHIFT, true)).result,
                  cxxime::ProcessResult::REJECTED);
        cxxime::KeyEvent shift_up = make_key(VK_LSHIFT);
        shift_up.is_key_up = true;
        ASSERT_EQ(manager.process_key(id, shift_up).result, cxxime::ProcessResult::ACCEPTED);

        if (use_delete) {
            ASSERT_EQ(manager.process_key(id, make_key(VK_LEFT)).result,
                      cxxime::ProcessResult::ACCEPTED);
        }
        const ProcessKeyResult cleared =
            manager.process_key(id, make_key(use_delete ? VK_DELETE : VK_BACK));
        ASSERT_TRUE(!cleared.composing);
        ASSERT_TRUE(cleared.ime_status.chinese_mode());

        const ProcessKeyResult resumed = manager.process_key(id, make_key('N'));
        ASSERT_TRUE(resumed.composing);
        ASSERT_TRUE(resumed.ime_status.chinese_mode());
        const ProcessKeyResult candidates = manager.process_key(id, make_key('I'));
        ASSERT_TRUE(candidates.composing);
        ASSERT_TRUE(!candidates.presentation.items.empty())
            << "delete=" << use_delete << " preedit=" << candidates.preedit
            << " known=" << candidates.presentation.extent.known_count;
        ASSERT_TRUE(candidates.ime_status.chinese_mode());
    }
}

static bool wait_for_count(std::atomic<int>& value, int expected, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (value.load() < expected) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

TEST(SessionIntegration, initialize_rejects_unknown_wubi_index_role) {
    const std::string dict_path = make_temp_path("test_unknown_wubi_role_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni", "你", 100}});
    write_manifest_for_files(dict_path, {
        {"pinyin_dict", dict_path},
        {"pinyin_idx", dict_path + ".idx"},
        {"pinyin_spellings", dict_path + ".spellings.bin"},
        {"pinyin_topn", dict_path + ".topn.bin"},
        {"wubi_dict", dict_path + ".wubi.bin"},
        {"wubi_idx", dict_path + ".wubi.bin.idx"},
    });

    SessionManager manager;
    ASSERT_TRUE(!manager.initialize(dict_path));
    delete_test_dictionary_bundle(dict_path);
}

// ============================================================
// process_key tests
// ============================================================

TEST(SessionIntegration, pinyin_scheme_switch_rebinds_active_session) {
    auto full_pinyin = std::make_shared<cxxime::Config>();
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict(), full_pinyin));
    const uint32_t id =
        manager.create_session(cxxime::kClientCapabilitySegmentedPreeditPresentation);

    ASSERT_EQ(manager.process_key(id, make_key('N')).preedit, "n");

    auto shuangpin = std::make_shared<cxxime::Config>(*full_pinyin);
    shuangpin->pinyin_scheme = "microsoft_shuangpin";
    manager.apply_config(shuangpin);

    const ProcessKeyResult first = manager.process_key(id, make_key('N'));
    ASSERT_EQ(first.preedit, "n");
    const ProcessKeyResult second = manager.process_key(id, make_key('I'));
    ASSERT_TRUE(candidate_contains(second.presentation, "你"));
    const ProcessKeyResult third = manager.process_key(id, make_key('H'));
    ASSERT_EQ(third.preedit, "ni'h");
    const ProcessKeyResult fourth = manager.process_key(id, make_key('K'));
    ASSERT_EQ(fourth.preedit, "ni'hk");
    ASSERT_TRUE(candidate_contains(fourth.presentation, "你好"));
}

TEST(SessionIntegration, server_accepts_only_canonical_pinyin_user_codes) {
    const std::string dict_path = make_temp_path("test_shuangpin_user_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni:hao", "你好", 1000}});
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    DeleteFileA(user_path.c_str());

    auto config = std::make_shared<cxxime::Config>();
    config->pinyin_scheme = "microsoft_shuangpin";
    SharedResources resources;
    ASSERT_TRUE(resources.load(dict_path, config));
    ASSERT_TRUE(resources.add_user_entry(cxxime::UserDictKind::PINYIN, "不可达", "nihk") !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(resources.add_user_entry(cxxime::UserDictKind::PINYIN, "拟好", "nihao"),
              cxxime::IPCStatus::OK);

    const auto result = resources.query_user_entries("拟好", cxxime::UserDictKind::PINYIN, 0, 10);
    ASSERT_EQ(result.entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(result.entries[0].code, "nihao");
    ASSERT_TRUE(result.entries[0].syllables.empty());
    ASSERT_TRUE(resources.freeze_and_stop_composition_learning());

    DeleteFileA(user_path.c_str());
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, built_in_shuangpin_schemes_can_be_selected_at_cold_start) {
    struct SchemeCase {
        const char* id;
        const char* input;
        const char* preedit;
    };
    const SchemeCase cases[] = {
        {"microsoft_shuangpin", "nihk", "ni'hk"},
        {"xiaohe_shuangpin", "nihc", "ni'hc"},
        {"ziranma_shuangpin", "nihk", "ni'hk"},
        {"sogou_shuangpin", "nihk", "ni'hk"},
    };
    const std::string dict_path = setup_test_dict();

    for (const auto& item : cases) {
        auto config = std::make_shared<cxxime::Config>();
        config->pinyin_scheme = item.id;
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path, config));
        const uint32_t id =
            manager.create_session(cxxime::kClientCapabilitySegmentedPreeditPresentation);

        ProcessKeyResult result;
        for (const char* key = item.input; *key; ++key) {
            result = manager.process_key(id, make_key(static_cast<uint32_t>(*key - 'a' + 'A')));
        }
        ASSERT_EQ(result.preedit, item.preedit);
        ASSERT_TRUE(candidate_contains(result.presentation, "你好"));
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, pinyin_scheme_switch_reuses_large_shared_resources) {
    const std::string dict_path = make_temp_path("test_scheme_shared_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"ni", "你", 1000},
        {"hao", "好", 800},
        {"nihao", "你好", 900},
    });
    {
        auto full_pinyin = std::make_shared<cxxime::Config>();
        SharedResources resources;
        ASSERT_TRUE(resources.load(dict_path, full_pinyin));
        const SharedResourceSnapshot before = resources.snapshot();

        auto shuangpin = std::make_shared<cxxime::Config>(*full_pinyin);
        shuangpin->pinyin_scheme = "microsoft_shuangpin";
        shuangpin->fuzzy_pinyin = false;
        ASSERT_TRUE(resources.prepare_config(shuangpin));
        const SharedResourceSnapshot prepared = resources.snapshot();
        ASSERT_EQ(prepared.config.get(), before.config.get());
        ASSERT_EQ(prepared.spellings.get(), before.spellings.get());
        ASSERT_TRUE(resources.commit_prepared_config(shuangpin));
        const SharedResourceSnapshot after = resources.snapshot();

        ASSERT_EQ(after.dict.get(), before.dict.get());
        ASSERT_EQ(after.wubi_dict.get(), before.wubi_dict.get());
        ASSERT_TRUE(after.spellings.get() != before.spellings.get());
        ASSERT_TRUE(after.syllabifier.get() != before.syllabifier.get());
        ASSERT_TRUE(!after.spellings->fuzzy_enabled());
        ASSERT_EQ(after.config->pinyin_scheme, "microsoft_shuangpin");
        const cxxime::CandidateOrderQueryResult order =
            resources.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 10);
        ASSERT_TRUE(std::any_of(order.entries.begin(), order.entries.end(),
                                [](const auto& entry) { return entry.text == "你好"; }));
        ASSERT_TRUE(resources.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, snapshot_sync_binds_config_before_scheme_resources) {
    const std::string dict_path = make_temp_path("test_scheme_snapshot_order_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ying", "应", 700}});
    {
        auto full_pinyin = std::make_shared<cxxime::Config>();
        SharedResources resources;
        ASSERT_TRUE(resources.load(dict_path, full_pinyin));
        const SharedResourceSnapshot before = resources.snapshot();

        SessionEntry entry;
        entry.engine = std::make_unique<cxxime::Engine>();
        ASSERT_TRUE(entry.engine->initialize(*before.dict, *before.spellings,
                                            before.syllabifier.get(), *before.config));
        entry.resources = before;

        auto shuangpin = std::make_shared<cxxime::Config>(*full_pinyin);
        shuangpin->pinyin_scheme = "microsoft_shuangpin";
        ASSERT_TRUE(resources.prepare_config(shuangpin));
        ASSERT_TRUE(resources.commit_prepared_config(shuangpin));
        const SharedResourceSnapshot after = resources.snapshot();

        apply_resource_snapshot(entry, after);
        ASSERT_EQ(entry.resources.config.get(), after.config.get());
        ASSERT_EQ(entry.resources.spellings.get(), after.spellings.get());
        ASSERT_EQ(entry.engine->process_key(make_key('Y')), cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(entry.engine->process_key(make_key(VK_OEM_1)),
            cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(entry.engine->context().active_input(), "y;");
        const auto& candidates = entry.engine->context().candidate_page().candidates;
        ASSERT_TRUE(std::any_of(candidates.begin(), candidates.end(),
                                [](const auto& candidate) { return candidate.text == "应"; }));

        entry.engine->finalize();
        ASSERT_TRUE(resources.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, failed_pinyin_scheme_switch_keeps_previous_snapshot) {
    const std::string dict_path = make_temp_path("test_scheme_failure_dict.bin");
    create_test_dictionary_bundle(dict_path, {
        {"ni", "你", 1000},
        {"hao", "好", 800},
    });
    {
        auto full_pinyin = std::make_shared<cxxime::Config>();
        SharedResources resources;
        ASSERT_TRUE(resources.load(dict_path, full_pinyin));
        const SharedResourceSnapshot before = resources.snapshot();
        ASSERT_TRUE(DeleteFileA((dict_path + ".microsoft-shuangpin.spellings.bin").c_str()));

        auto shuangpin = std::make_shared<cxxime::Config>(*full_pinyin);
        shuangpin->pinyin_scheme = "microsoft_shuangpin";
        ASSERT_TRUE(!resources.prepare_config(shuangpin));
        ASSERT_TRUE(!resources.commit_prepared_config(shuangpin));
        const SharedResourceSnapshot after = resources.snapshot();

        ASSERT_EQ(after.dict.get(), before.dict.get());
        ASSERT_EQ(after.spellings.get(), before.spellings.get());
        ASSERT_EQ(after.syllabifier.get(), before.syllabifier.get());
        ASSERT_EQ(after.config.get(), before.config.get());
        ASSERT_TRUE(resources.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, failed_scheme_prepare_is_not_persisted_or_published) {
    const std::string dict_path = make_temp_path("test_scheme_transaction_dict.bin");
    const std::string user_config_path = make_temp_path("test_scheme_transaction_config.json");
    create_test_dictionary_bundle(dict_path, {{"ni", "你", 1000}});
    ASSERT_TRUE(DeleteFileA((dict_path + ".microsoft-shuangpin.spellings.bin").c_str()));
    DeleteFileA(user_config_path.c_str());

    ConfigStore store;
    std::shared_ptr<const cxxime::Config> initial_config;
    ASSERT_TRUE(store.initialize(std::string(CXXIME_DATA_DIR) + "default.json", user_config_path,
                                 std::string(CXXIME_DATA_DIR) + "themes.json", &initial_config));
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(dict_path, initial_config));

    std::atomic<int> apply_count{0};
    ConfigWriteCoordinator coordinator;
    ASSERT_TRUE(coordinator.start(
        &store,
        [&](const std::shared_ptr<const cxxime::Config>& config) {
            if (manager.apply_config(config)) {
                apply_count.fetch_add(1);
            }
        },
        [&](const std::shared_ptr<const cxxime::Config>& config, unsigned long* error_code) {
            return manager.prepare_config(config, error_code);
        },
        [&]() { manager.cancel_prepared_config(); }));

    unsigned long error_code = ERROR_SUCCESS;
    ASSERT_TRUE(!coordinator.submit(cxxime::UserConfigMutationKind::kMergePatch,
                                    R"({"engine":{"pinyin_scheme":"microsoft_shuangpin"}})",
                                    nullptr, &error_code));
    ASSERT_EQ(error_code, static_cast<unsigned long>(ERROR_INVALID_DATA));
    ASSERT_EQ(apply_count.load(), 0);
    ASSERT_EQ(GetFileAttributesA(user_config_path.c_str()), INVALID_FILE_ATTRIBUTES);

    coordinator.stop();
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, process_key_ok) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();
    ASSERT_GT(id, (uint32_t)0);

    // Type a letter in Chinese mode
    auto r = mgr.process_key(id, make_key('N'));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(r.composing);
    ASSERT_EQ(r.preedit, "n");
    ASSERT_EQ(r.preedit_cursor, static_cast<size_t>(1));

    r = mgr.process_key(id, make_key(VK_LEFT));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_TRUE(r.composing);
    ASSERT_EQ(r.preedit, "n");
    ASSERT_EQ(r.preedit_cursor, static_cast<size_t>(0));
}

TEST(SessionIntegration, wubi_fifth_key_returns_commit_and_next_composition) {
    const std::string dict_path = make_temp_path("test_wubi_fifth_key_session.bin");
    create_test_dictionary_bundle_with_wubi(dict_path, {{"a", "拼", 100}},
                                                {
                                                    {"abcd", "首选", 300},
                                                    {"abcd", "次选", 200},
                                                    {"e", "下一项", 100},
                                                });

    auto config = std::make_shared<cxxime::Config>();
    config->input_mode = static_cast<int>(cxxime::InputMode::WUBI);
    config->wubi_commit_first_on_fifth_key = true;
    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(dict_path, config));
    const uint32_t id = mgr.create_session();

    ASSERT_EQ(mgr.process_key(id, make_key('A')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(mgr.process_key(id, make_key('B')).result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(mgr.process_key(id, make_key('C')).result, cxxime::ProcessResult::ACCEPTED);
    const ProcessKeyResult fourth = mgr.process_key(id, make_key('D'));
    ASSERT_EQ(fourth.result, cxxime::ProcessResult::ACCEPTED);
    ASSERT_EQ(fourth.focused_preedit_start_bytes, fourth.preedit.size());
    ASSERT_EQ(fourth.focused_preedit_end_bytes, fourth.preedit.size());

    const auto result = mgr.process_key(id, make_key('E'));
    ASSERT_EQ(result.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(result.result, cxxime::ProcessResult::COMMITTED);
    ASSERT_EQ(result.commit_text, "首选");
    ASSERT_TRUE(result.composing);
    ASSERT_EQ(result.preedit, "e");
    ASSERT_EQ(result.preedit_cursor, static_cast<size_t>(1));
    ASSERT_TRUE(candidate_contains(result.presentation, "下一项"));

    mgr.destroy_session(id);
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, segmented_preedit_focuses_wubi_and_mixed_wubi_candidates) {
    const std::string dict_path = make_temp_path("test_wubi_preedit_focus_session.bin");
    create_test_dictionary_bundle_with_wubi(dict_path, {{"a", "拼", 100}},
                                            {{"abcd", "五笔首选", 300}, {"abcd", "五笔次选", 200}});
    constexpr uint64_t kCapabilities = cxxime::kClientCapabilitySegmentedSelection |
                                       cxxime::kClientCapabilitySegmentedPreeditPresentation;

    for (cxxime::InputMode mode : {cxxime::InputMode::WUBI, cxxime::InputMode::MIXED}) {
        auto config = std::make_shared<cxxime::Config>();
        config->input_mode = static_cast<int>(mode);
        config->mixed_candidate_preference = cxxime::MixedCandidatePreference::kWubi;
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path, config));
        const uint32_t id = manager.create_session(kCapabilities);

        ProcessKeyResult result;
        for (char key : std::string("ABCD")) {
            result = manager.process_key(id, make_key(static_cast<uint32_t>(key)));
        }
        ASSERT_TRUE(result.composing);
        ASSERT_TRUE(!result.presentation.items.empty());
        ASSERT_EQ(result.preedit, "abcd");
        ASSERT_EQ(result.focused_preedit_start_bytes, static_cast<std::size_t>(0));
        ASSERT_EQ(result.focused_preedit_end_bytes, result.preedit.size());
        ASSERT_EQ(result.preedit_presentation_flags, static_cast<uint32_t>(0));
        ASSERT_EQ(manager.clear_composition(id).status, cxxime::IPCStatus::OK);
        for (char key : std::string("HSE")) {
            result = manager.process_key(id, make_key(static_cast<uint32_t>(key)));
        }
        ASSERT_TRUE(result.composing);
        ASSERT_EQ(result.preedit, "hse");
        ASSERT_TRUE(result.presentation.items.empty());
        ASSERT_EQ(result.focused_preedit_start_bytes, static_cast<std::size_t>(0));
        ASSERT_EQ(result.focused_preedit_end_bytes, result.preedit.size());
        manager.destroy_session(id);
    }

    delete_test_dictionary_bundle(dict_path);
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
    ASSERT_EQ(r.ime_status.chinese_mode(), false);
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

TEST(SessionIntegration, english_enter_passes_to_application) {
    SessionManager mgr;
    mgr.initialize(setup_test_dict());
    uint32_t id = mgr.create_session();

    mgr.toggle_chinese(id);

    auto r = mgr.process_key(id, make_key(VK_RETURN));
    ASSERT_EQ(r.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(r.result, cxxime::ProcessResult::REJECTED);
    ASSERT_TRUE(r.commit_text.empty());
    ASSERT_TRUE(!r.composing);
    ASSERT_EQ(r.ime_status.chinese_mode(), false);
}

TEST(SessionIntegration, append_enter_preserves_case_through_output_composer) {
    std::string cfg_path = make_temp_path("test_append_config.json");
    {
        std::ofstream f(cfg_path);
        f << R"({"ascii_composer":{"switch_key":{"Caps_Lock":"append"}}})";
    }

    auto config = std::make_shared<cxxime::Config>();
    ASSERT_TRUE(config->load(cfg_path));
    SessionManager mgr;
    mgr.initialize(setup_test_dict(), config);
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

TEST(SessionIntegration, user_data_merge_updates_live_lexicon_and_skips_invalid_rows) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));

    const std::map<std::string, std::string> replacement = {
        {"user_pinyin.tsv", "备份词\tbeifenci\t100\tbei:fen:ci\n"},
    };
    merge_user_data_for_test(manager, replacement);
    const auto imported = manager.query_lexicon_entries(
        cxxime::LexiconResource::kUserLexicon, "备份词", cxxime::UserDictKind::PINYIN, 0, 16, true);
    ASSERT_EQ(imported.entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(imported.entries[0].code, "beifenci");

    const std::map<std::string, std::string> invalid_files[] = {
        {{"user_pinyin.tsv", "invalid"}},
        {{"learning_pinyin.tsv", "invalid"}},
        {{"candidate_order_pinyin.tsv", "invalid"}},
        {{"disabled_pinyin.tsv", "invalid\tentry"}},
        {{"learning_composition.tsv", "invalid"}},
    };
    for (const auto& invalid : invalid_files) {
        std::size_t imported = 0;
        std::size_t skipped = 0;
        manager.merge_user_data(invalid, &imported, &skipped);
        ASSERT_EQ(imported, static_cast<std::size_t>(0));
        ASSERT_TRUE(skipped != 0);
    }
    ASSERT_EQ(manager
                    .query_lexicon_entries(cxxime::LexiconResource::kUserLexicon, "备份词",
                                            cxxime::UserDictKind::PINYIN, 0, 16, true)
                    .entries.size(),
                static_cast<std::size_t>(1));

}

TEST(SessionIntegration, user_data_merge_skips_failed_file_and_continues) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    const std::string learning_path = test_user_data_dir + "\\learning_composition.tsv";
    DeleteFileA(learning_path.c_str());
    RemoveDirectoryA(learning_path.c_str());
    ASSERT_TRUE(CreateDirectoryA(learning_path.c_str(), nullptr) != FALSE);

    const std::map<std::string, std::string> imported_data = {
        {"learning_composition.tsv", "你好\tnihao\tni:hao\t2\t3\n"},
        {"user_pinyin.tsv", "继续导入\tjixudaoru\t5\tji:xu:dao:ru\n"},
    };
    std::size_t imported = 0;
    std::size_t skipped = 0;
    manager.merge_user_data(imported_data, &imported, &skipped);
    ASSERT_EQ(imported, static_cast<std::size_t>(1));
    ASSERT_TRUE(skipped != 0);
    ASSERT_EQ(manager
                  .query_lexicon_entries(cxxime::LexiconResource::kUserLexicon, "继续导入",
                                         cxxime::UserDictKind::PINYIN, 0, 16, true)
                  .entries.size(),
              static_cast<std::size_t>(1));

    ASSERT_TRUE(RemoveDirectoryA(learning_path.c_str()) != FALSE);
    manager.merge_user_data({{"learning_composition.tsv", "你好\tnihao\tni:hao\t2\t3\n"}},
                            &imported, &skipped);
    ASSERT_EQ(imported, static_cast<std::size_t>(1));
    ASSERT_EQ(skipped, static_cast<std::size_t>(0));
    DeleteFileA(learning_path.c_str());
}

int main() {
    GetTempPathA(MAX_PATH, temp_path);
    const std::string directory_name =
        "cxxime-session-integration-" + std::to_string(GetCurrentProcessId());
    test_user_data_dir = make_temp_path(directory_name.c_str());
    CreateDirectoryA(test_user_data_dir.c_str(), nullptr);
    DeleteFileA((test_user_data_dir + "\\default.json").c_str());
    DeleteFileA((test_user_data_dir + "\\user_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\user_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\candidate_order_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\candidate_order_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\learning_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\learning_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\learning_composition.tsv").c_str());

    cxxime::set_data_dir(CXXIME_DATA_DIR);
    cxxime::set_user_data_dir(test_user_data_dir);
    const int result = test::RunAllTests();

    DeleteFileA((test_user_data_dir + "\\default.json").c_str());
    DeleteFileA((test_user_data_dir + "\\user_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\user_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\disabled_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\candidate_order_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\candidate_order_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\learning_pinyin.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\learning_wubi.tsv").c_str());
    DeleteFileA((test_user_data_dir + "\\learning_composition.tsv").c_str());
    RemoveDirectoryA(test_user_data_dir.c_str());
    return result;
}
