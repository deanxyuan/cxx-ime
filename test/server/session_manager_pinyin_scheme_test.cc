// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "config_store.h"
#include "config_write_coordinator.h"
#include "session_manager_integration_test_support.h"

TEST(PinyinSchemeRuntime, concurrent_snapshots_and_session_binding_keep_runtime_identity) {
    const std::string dict_path = make_temp_path("test_concurrent_runtime_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni", "candidate", 100}});
    {
        SharedResources resources;
        auto full = std::make_shared<cxxime::Config>();
        full->page_size = 7;
        auto shuangpin = std::make_shared<cxxime::Config>(*full);
        shuangpin->pinyin_scheme = "microsoft_shuangpin";
        shuangpin->fuzzy_pinyin = false;
        shuangpin->page_size = 3;
        ASSERT_TRUE(resources.load(dict_path, full));
        const auto original = resources.snapshot();
        std::atomic<int> published{0};
        std::atomic<int> observed{0};
        bool snapshots_ok = true;
        bool switches_ok = true;
        auto inspect = [&]() {
            const auto snapshot = resources.snapshot();
            const auto& runtime = *snapshot.runtime;
            const bool is_full = runtime.config().pinyin_scheme == "full_pinyin";
            if ((!is_full && runtime.config().pinyin_scheme != "microsoft_shuangpin") ||
                runtime.pinyin_resources().scheme_id() != runtime.config().pinyin_scheme ||
                runtime.pinyin_resources().kind() != (is_full
                                                          ? cxxime::PinyinSchemeKind::kFullPinyin
                                                          : cxxime::PinyinSchemeKind::kShuangpin) ||
                runtime.config().page_size != (is_full ? 7 : 3) ||
                runtime.pinyin_query_policy().enable_fuzzy != is_full ||
                runtime.pinyin_dict_ptr() != original.runtime->pinyin_dict_ptr()) {
                return false;
            }
            SessionEntry entry;
            entry.engine = std::make_unique<cxxime::Engine>();
            if (!entry.engine->initialize(snapshot.runtime)) {
                return false;
            }
            entry.resources = snapshot;
            // Bind a later snapshot while publication continues; both generations stay alive.
            apply_resource_snapshot(entry, resources.snapshot());
            const auto page = entry.engine->translate_for_search("ni");
            return !page.candidates.empty() && page.candidates.front().text == "candidate";
        };
        std::thread reader([&]() {
            for (int round = 1; round <= 64; ++round) {
                while (published.load() < round) {
                    snapshots_ok = inspect() && snapshots_ok;
                }
                snapshots_ok = inspect() && snapshots_ok;
                observed.store(round);
            }
        });
        std::thread writer([&]() {
            for (int round = 1; round <= 64; ++round) {
                const auto& config = round % 2 ? shuangpin : full;
                const bool prepared = resources.prepare_config(config);
                const bool committed = resources.commit_prepared_config(config);
                switches_ok = prepared && committed && switches_ok;
                published.store(round);
                while (observed.load() < round) {
                    std::this_thread::yield();
                }
            }
        });
        writer.join();
        reader.join();
        ASSERT_TRUE(switches_ok);
        ASSERT_TRUE(snapshots_ok);
        ASSERT_TRUE(resources.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(PinyinSchemeRuntime, concurrent_session_creation_and_scheme_switching) {
    const std::string dict_path = make_temp_path("test_concurrent_sessions_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni", "candidate", 100}});
    {
        SessionManager manager;
        auto full = std::make_shared<cxxime::Config>();
        auto shuangpin = std::make_shared<cxxime::Config>(*full);
        shuangpin->pinyin_scheme = "microsoft_shuangpin";
        shuangpin->fuzzy_pinyin = false;
        ASSERT_TRUE(manager.initialize(dict_path, full));
        std::atomic<int> ready{0};
        bool sessions_ok = true;
        bool switches_ok = true;
        auto rendezvous = [&](int round) {
            ready.fetch_add(1);
            while (ready.load() < (round + 1) * 2) {
                std::this_thread::yield();
            }
        };
        std::thread creator([&]() {
            for (int round = 0; round < 64; ++round) {
                rendezvous(round);
                const uint32_t id = manager.create_session();
                const auto result = manager.process_key(id, make_key('N'));
                sessions_ok = id != 0 && result.status == cxxime::IPCStatus::OK &&
                              result.result == cxxime::ProcessResult::ACCEPTED &&
                              candidate_contains(result.presentation, "candidate") && sessions_ok;
                manager.destroy_session(id);
            }
        });
        std::thread writer([&]() {
            for (int round = 0; round < 64; ++round) {
                rendezvous(round);
                switches_ok = manager.apply_config(round % 2 ? full : shuangpin) && switches_ok;
            }
        });
        creator.join();
        writer.join();
        ASSERT_TRUE(sessions_ok);
        ASSERT_TRUE(switches_ok);
        ASSERT_TRUE(manager.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(PinyinSchemeRuntime, same_scheme_config_reuses_resources_without_mutating_old_runtime) {
    const std::string dict_path = make_temp_path("test_same_scheme_runtime_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni", "candidate", 100}});

    {
        SharedResources resources;
        auto initial_config = std::make_shared<cxxime::Config>();
        ASSERT_TRUE(resources.load(dict_path, initial_config));
        const SharedResourceSnapshot before = resources.snapshot();

        auto next_config = std::make_shared<cxxime::Config>(*initial_config);
        next_config->fuzzy_pinyin = false;
        ASSERT_TRUE(resources.prepare_config(next_config));
        ASSERT_TRUE(resources.commit_prepared_config(next_config));
        const SharedResourceSnapshot after = resources.snapshot();

        ASSERT_TRUE(after.runtime.get() != before.runtime.get());
        ASSERT_EQ(after.runtime->pinyin_dict_ptr().get(), before.runtime->pinyin_dict_ptr().get());
        ASSERT_EQ(after.runtime->wubi_dict_ptr().get(), before.runtime->wubi_dict_ptr().get());
        ASSERT_EQ(after.runtime->pinyin_resources_ptr().get(),
                  before.runtime->pinyin_resources_ptr().get());
        ASSERT_TRUE(before.runtime->pinyin_query_policy().enable_fuzzy);
        ASSERT_TRUE(!after.runtime->pinyin_query_policy().enable_fuzzy);

        ASSERT_TRUE(resources.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(PinyinSchemeRuntime, dictionary_reload_invalidates_prepared_runtime) {
    const std::string dict_path = make_temp_path("test_stale_runtime_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni", "candidate", 100}});

    {
        SharedResources resources;
        auto initial_config = std::make_shared<cxxime::Config>();
        ASSERT_TRUE(resources.load(dict_path, initial_config));

        auto next_config = std::make_shared<cxxime::Config>(*initial_config);
        next_config->fuzzy_pinyin = false;
        ASSERT_TRUE(resources.prepare_config(next_config));
        const SharedResourceSnapshot before_reload = resources.snapshot();
        ASSERT_TRUE(resources.reload_dictionaries());
        const SharedResourceSnapshot after_reload = resources.snapshot();
        ASSERT_TRUE(after_reload.runtime.get() != before_reload.runtime.get());

        ASSERT_TRUE(!resources.commit_prepared_config(next_config));
        const SharedResourceSnapshot after_commit = resources.snapshot();
        ASSERT_EQ(after_commit.runtime.get(), after_reload.runtime.get());
        ASSERT_TRUE(after_commit.runtime->pinyin_query_policy().enable_fuzzy);

        ASSERT_TRUE(resources.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(PinyinSchemeRuntime, server_requires_full_pinyin_spelling_resource) {
    const std::string dict_path = make_temp_path("test_required_spelling_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ni", "candidate", 100}});
    ASSERT_TRUE(DeleteFileA((dict_path + ".spellings.bin").c_str()));

    {
        SharedResources resources;
        ASSERT_TRUE(!resources.load(dict_path, std::make_shared<cxxime::Config>()));
    }

    delete_test_dictionary_bundle(dict_path);
}

TEST(PinyinSchemeRuntime, server_rejects_empty_and_invalid_full_pinyin_spellings) {
    const std::string dict_path = make_temp_path("test_invalid_spelling_dict.bin");
    for (bool empty_trie : {true, false}) {
        create_test_dictionary_bundle(dict_path, {{"ni", "candidate", 100}});
        const std::string spelling_path = dict_path + ".spellings.bin";
        if (empty_trie) {
            ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spelling_path, {}));
        } else {
            std::ofstream output(spelling_path, std::ios::binary);
            output << "invalid spelling resource";
            ASSERT_TRUE(output.good());
        }
        // Refresh hashes so rejection exercises resource loading, not manifest integrity.
        write_manifest_for_files(dict_path, {
                                                {"pinyin_dict", dict_path},
                                                {"pinyin_idx", dict_path + ".idx"},
                                                {"pinyin_spellings", spelling_path},
                                                {"pinyin_topn", dict_path + ".topn.bin"},
                                                {"wubi_dict", dict_path + ".wubi.bin"},
                                                {"wubi_prefix_index", dict_path + ".wubi.bin.idx"},
                                            });
        SharedResources resources;
        ASSERT_TRUE(!resources.load(dict_path, std::make_shared<cxxime::Config>()));
        delete_test_dictionary_bundle(dict_path);
    }
}

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

TEST(SessionIntegration, built_in_pinyin_schemes_match_standalone_and_server_cold_start) {
    struct SchemeCase {
        const char* id;
        const char* input;
        const char* preedit;
    };
    const SchemeCase cases[] = {
        {"full_pinyin", "nihao", "nihao"},     {"microsoft_shuangpin", "nihk", "ni'hk"},
        {"xiaohe_shuangpin", "nihc", "ni'hc"}, {"ziranma_shuangpin", "nihk", "ni'hk"},
        {"sogou_shuangpin", "nihk", "ni'hk"},
    };
    const std::string dict_path = setup_test_dict();
    char standalone_directory[MAX_PATH] = {};
    ASSERT_TRUE(GetTempFileNameA(temp_path, "psa", 0, standalone_directory) != 0);
    ASSERT_TRUE(DeleteFileA(standalone_directory));
    ASSERT_TRUE(CreateDirectoryA(standalone_directory, nullptr));
    const std::string standalone_dict = std::string(standalone_directory) + "\\pinyin.dict.bin";
    const std::string standalone_config = std::string(standalone_directory) + "\\config.json";
    ASSERT_TRUE(CopyFileA(dict_path.c_str(), standalone_dict.c_str(), TRUE));

    for (const auto& item : cases) {
        auto config = std::make_shared<cxxime::Config>();
        config->pinyin_scheme = item.id;
        const auto& scheme = cxxime::resolve_pinyin_scheme(item.id);
        const std::string spelling_source =
            dict_path + std::string(scheme.spelling_filename).substr(6);
        const std::string spelling_target =
            std::string(standalone_directory) + "\\" + scheme.spelling_filename;
        ASSERT_TRUE(CopyFileA(spelling_source.c_str(), spelling_target.c_str(), TRUE));
        {
            std::ofstream output(standalone_config);
            output << config->to_runtime_json();
            ASSERT_TRUE(output.good());
        }
        cxxime::Engine standalone;
        ASSERT_TRUE(standalone.initialize(standalone_dict, standalone_config));
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path, config));
        const uint32_t id =
            manager.create_session(cxxime::kClientCapabilitySegmentedPreeditPresentation);

        ProcessKeyResult result;
        for (const char* key = item.input; *key; ++key) {
            const uint32_t vk = static_cast<uint32_t>(*key - 'a' + 'A');
            result = manager.process_key(id, make_key(vk));
            ASSERT_EQ(standalone.process_key(make_key(vk)), cxxime::ProcessResult::ACCEPTED);
        }
        ASSERT_EQ(result.preedit, item.preedit);
        ASSERT_TRUE(candidate_contains(result.presentation, "你好"));
        const auto& candidates = standalone.context().candidate_page().candidates;
        ASSERT_TRUE(!candidates.empty());
        ASSERT_EQ(candidates.front().text, "你好");
        ASSERT_TRUE(standalone.select_candidate(0));
        ASSERT_EQ(standalone.get_commit_text(), "你好");
        standalone.finalize();
        ASSERT_TRUE(DeleteFileA(spelling_target.c_str()));
        ASSERT_TRUE(manager.freeze_and_stop_composition_learning());
    }
    ASSERT_TRUE(DeleteFileA(standalone_config.c_str()));
    ASSERT_TRUE(DeleteFileA(standalone_dict.c_str()));
    ASSERT_TRUE(RemoveDirectoryA(standalone_directory));
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
        ASSERT_EQ(prepared.runtime.get(), before.runtime.get());
        ASSERT_TRUE(resources.commit_prepared_config(shuangpin));
        const SharedResourceSnapshot after = resources.snapshot();

        ASSERT_EQ(after.runtime->pinyin_dict_ptr().get(), before.runtime->pinyin_dict_ptr().get());
        ASSERT_EQ(after.runtime->wubi_dict_ptr().get(), before.runtime->wubi_dict_ptr().get());
        ASSERT_TRUE(after.runtime->pinyin_resources_ptr().get() !=
                    before.runtime->pinyin_resources_ptr().get());
        ASSERT_TRUE(!after.runtime->pinyin_query_policy().enable_fuzzy);
        ASSERT_EQ(after.runtime->config().pinyin_scheme, "microsoft_shuangpin");
        const cxxime::CandidateOrderQueryResult order =
            resources.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 10);
        ASSERT_TRUE(std::any_of(order.entries.begin(), order.entries.end(),
                                [](const auto& entry) { return entry.text == "你好"; }));
        ASSERT_TRUE(resources.freeze_and_stop_composition_learning());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, snapshot_sync_binds_complete_runtime) {
    const std::string dict_path = make_temp_path("test_scheme_snapshot_order_dict.bin");
    create_test_dictionary_bundle(dict_path, {{"ying", "应", 700}});
    {
        auto full_pinyin = std::make_shared<cxxime::Config>();
        SharedResources resources;
        ASSERT_TRUE(resources.load(dict_path, full_pinyin));
        const SharedResourceSnapshot before = resources.snapshot();

        SessionEntry entry;
        entry.engine = std::make_unique<cxxime::Engine>();
        ASSERT_TRUE(entry.engine->initialize(before.runtime));
        entry.resources = before;

        auto shuangpin = std::make_shared<cxxime::Config>(*full_pinyin);
        shuangpin->pinyin_scheme = "microsoft_shuangpin";
        ASSERT_TRUE(resources.prepare_config(shuangpin));
        ASSERT_TRUE(resources.commit_prepared_config(shuangpin));
        const SharedResourceSnapshot after = resources.snapshot();

        apply_resource_snapshot(entry, after);
        ASSERT_EQ(entry.resources.runtime.get(), after.runtime.get());
        ASSERT_EQ(entry.engine->process_key(make_key('Y')), cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(entry.engine->process_key(make_key(VK_OEM_1)), cxxime::ProcessResult::ACCEPTED);
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

        ASSERT_EQ(after.runtime.get(), before.runtime.get());
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
