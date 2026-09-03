// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager_integration_test_support.h"
TEST(SessionIntegration, lexicon_control_mutations_and_pagination) {
    const std::string preference_path = test_user_data_dir + "\\learning_pinyin.tsv";
    {
        std::ofstream output(preference_path, std::ios::binary | std::ios::trunc);
        output << "偏好一\tpreferenceone\tpreferenceone\t2\t1\t\n"
            << "偏好二\tpreferencetwo\tpreferencetwo\t3\t2\t\n";
    }
    SessionManager mgr;
    ASSERT_TRUE(mgr.initialize(setup_test_dict()));

    auto execute = [&](const cxxime::LexiconControlRequest& request,
                       cxxime::LexiconControlResult* result) {
        std::string request_payload;
        std::string response_payload;
        return cxxime::encode_lexicon_request(request, &request_payload) &&
               handle_lexicon_control_request(mgr, request_payload, &response_payload) &&
               cxxime::decode_lexicon_result(response_payload, result);
    };

    cxxime::LexiconControlRequest request;
    request.operation = cxxime::LexiconOperation::kAdd;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.text = "control-entry";
    request.code = "controlcode";
    cxxime::LexiconControlResult result;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request.text.assign(cxxime::kCandidateTextCapacity, 'x');
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(result.error_code, static_cast<uint32_t>(ERROR_BUFFER_OVERFLOW));

    request.text = "invalid-code";
    request.code = "ABC";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(result.error_code, static_cast<uint32_t>(ERROR_INVALID_DATA));

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "control";
    request.offset = 0;
    request.limit = 1;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<size_t>(1));
    ASSERT_EQ(result.query.entries.size(), static_cast<size_t>(1));
    ASSERT_TRUE(result.query.entries[0].text == "control-entry");

    request = {};
    request.operation = cxxime::LexiconOperation::kReplace;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.old_text = "control-entry";
    request.old_code = "controlcode";
    request.text = "control-replaced";
    request.code = "controlcode";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kAdd;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.text = "control-second";
    request.code = "controlsecond";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {
        {"control-replaced", "controlcode"}, {"control-second", "INVALID"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(mgr.query_user_entries("control", cxxime::UserDictKind::PINYIN, 0, 10).match_total,
              static_cast<std::size_t>(2));

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {
        {"control-replaced", "controlcode"}, {"control-second", "controlsecond"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(mgr.query_user_entries("control", cxxime::UserDictKind::PINYIN, 0, 10).match_total,
              static_cast<std::size_t>(0));

    request = {};
    request.operation = cxxime::LexiconOperation::kSave;
    request.kind = cxxime::UserDictKind::PINYIN;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request.resource = cxxime::LexiconResource::kCandidatePreference;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.resource = cxxime::LexiconResource::kCandidatePreference;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {{"偏好一", "preferenceone"}, {"偏好二", "preferencetwo"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.resource = cxxime::LexiconResource::kCandidatePreference;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "";
    request.limit = 10;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<std::size_t>(0));

    const std::string import_path = make_temp_path("control_import.tsv");
    {
        std::ofstream output(import_path, std::ios::binary | std::ios::trunc);
        output << "control-imported\tcontrolimported\t9\n";
    }
    request.operation = cxxime::LexiconOperation::kImport;
    request.resource = cxxime::LexiconResource::kUserLexicon;
    request.source_path = import_path;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(mgr.query_user_entries("control-imported", cxxime::UserDictKind::PINYIN, 0, 10)
                  .match_total,
              static_cast<std::size_t>(1));
    DeleteFileA(import_path.c_str());

    request = {};
    request.operation = cxxime::LexiconOperation::kDelete;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.entries = {{"control-imported", "controlimported"}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kAdd;
    request.resource = cxxime::LexiconResource::kCandidatePreference;
    request.text = "unsupported";
    request.code = "unsupported";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(result.error_code, static_cast<uint32_t>(ERROR_NOT_SUPPORTED));

    request = {};
    request.operation = cxxime::LexiconOperation::kDisableSystemEntry;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.text = "你好";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);

    const std::string disabled_path = test_user_data_dir + "\\disabled_pinyin.tsv";
    ASSERT_TRUE(SetFileAttributesA(disabled_path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
    request.text = "失败词";
    ASSERT_TRUE(execute(request, &result));
    SetFileAttributesA(disabled_path.c_str(), FILE_ATTRIBUTE_NORMAL);
    ASSERT_TRUE(!result.succeeded);

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "失败词";
    request.limit = 10;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<std::size_t>(0));

    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.query = "你好";
    request.limit = 10;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(result.query.entries[0].text, "你好");

    request = {};
    request.operation = cxxime::LexiconOperation::kQuerySystemEntryStatus;
    request.resource = cxxime::LexiconResource::kDisabledSystemLexicon;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.texts = {"世界", "你好"};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(result.query.entries[0].text, "你好");

    request.operation = cxxime::LexiconOperation::kRestoreSystemEntry;
    request.text = "你好";
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    DeleteFileA(preference_path.c_str());
}

TEST(SessionIntegration, candidate_order_control_updates_effective_server_order) {
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    const std::string learning_path = test_user_data_dir + "\\learning_pinyin.tsv";
    const std::string order_path = test_user_data_dir + "\\candidate_order_pinyin.tsv";
    DeleteFileA(user_path.c_str());
    DeleteFileA(learning_path.c_str());
    DeleteFileA(order_path.c_str());

    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    ASSERT_EQ(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "manual-choice", "nihao"),
              cxxime::IPCStatus::OK);

    auto execute = [&](const cxxime::LexiconControlRequest& request,
                       cxxime::LexiconControlResult* result) {
        std::string request_payload;
        std::string response_payload;
        return cxxime::encode_lexicon_request(request, &request_payload) &&
               handle_lexicon_control_request(manager, request_payload, &response_payload) &&
               cxxime::decode_lexicon_result(response_payload, result);
    };

    cxxime::LexiconControlRequest request;
    request.operation = cxxime::LexiconOperation::kQuery;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.resource = cxxime::LexiconResource::kUserLexicon;
    request.query = "manual-choice";
    request.exact_text = true;
    request.limit = 16;
    cxxime::LexiconControlResult result;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(result.query.match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(result.query.entries.front().code, "nihao");

    request = {};
    request.operation = cxxime::LexiconOperation::kQueryCandidateOrder;
    request.kind = cxxime::UserDictKind::PINYIN;
    request.resource = cxxime::LexiconResource::kManualCandidateOrder;
    request.code = "nihao";
    request.limit = 16;
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    const std::uint64_t initial_version = result.candidate_order.version;

    request.operation = cxxime::LexiconOperation::kSetCandidateOrder;
    request.expected_version = initial_version;
    request.candidate_order = {{"manual-choice", "nihao", ""}};
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_TRUE(!result.candidate_order.entries.empty());
    ASSERT_EQ(result.candidate_order.entries.front().text, "manual-choice");
    ASSERT_EQ(result.candidate_order.entries.front().reason,
              cxxime::CandidateOrderReason::kManual);
    const std::uint64_t pinned_version = result.candidate_order.version;

    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(!result.succeeded);
    ASSERT_EQ(result.error_code, static_cast<std::uint32_t>(ERROR_REVISION_MISMATCH));

    request.operation = cxxime::LexiconOperation::kClearCandidateOrder;
    request.expected_version = pinned_version;
    request.candidate_order.clear();
    ASSERT_TRUE(execute(request, &result));
    ASSERT_TRUE(result.succeeded);
    ASSERT_TRUE(std::none_of(result.candidate_order.entries.begin(),
                             result.candidate_order.entries.end(),
                             [](const auto& entry) {
                                 return entry.reason == cxxime::CandidateOrderReason::kManual;
                             }));

    DeleteFileA(user_path.c_str());
    DeleteFileA(learning_path.c_str());
    DeleteFileA(order_path.c_str());
}

TEST(SessionIntegration, topn_candidate_order_uses_canonical_pinyin_identity) {
    const std::string dict_path = make_temp_path("test_topn_candidate_order.bin");
    create_test_dictionary_bundle(dict_path, {{"ni:hao", "topn-canonical", 900}});
    const std::string order_path = test_user_data_dir + "\\candidate_order_pinyin.tsv";
    DeleteFileA(order_path.c_str());

    SessionManager manager;
    ASSERT_TRUE(manager.initialize(dict_path));
    const auto queried = manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 16);
    ASSERT_EQ(queried.entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(queried.entries.front().code, "nihao");
    ASSERT_EQ(queried.entries.front().syllables, "ni:hao");

    bool version_conflict = false;
    const std::vector<cxxime::ManualCandidateOrderEntry> order = {
        {"topn-canonical", "nihao", "ni:hao"}};
    ASSERT_EQ(manager.replace_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", order,
                                              queried.version, &version_conflict),
              cxxime::IPCStatus::OK);
    ASSERT_TRUE(!version_conflict);
    const auto saved = manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 1);
    ASSERT_EQ(saved.entries.front().text, "topn-canonical");
    ASSERT_EQ(saved.entries.front().reason, cxxime::CandidateOrderReason::kManual);

    DeleteFileA(order_path.c_str());
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, candidate_order_marks_pinned_entries_beyond_page_available) {
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    const std::string order_path = test_user_data_dir + "\\candidate_order_pinyin.tsv";
    DeleteFileA(user_path.c_str());
    DeleteFileA(order_path.c_str());

    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    ASSERT_EQ(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "first-candidate", "nihao"),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "second-candidate", "nihao"),
              cxxime::IPCStatus::OK);
    const auto queried = manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 2);
    bool version_conflict = false;
    const std::vector<cxxime::ManualCandidateOrderEntry> order = {
        {"first-candidate", "nihao", ""}, {"second-candidate", "nihao", ""}};
    ASSERT_EQ(manager.replace_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", order,
                                              queried.version, &version_conflict),
              cxxime::IPCStatus::OK);

    const auto limited = manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 1);
    const auto second =
        std::find_if(limited.entries.begin(), limited.entries.end(),
                     [](const auto& entry) { return entry.text == "second-candidate"; });
    ASSERT_TRUE(second != limited.entries.end());
    ASSERT_EQ(second->reason, cxxime::CandidateOrderReason::kManual);
    ASSERT_TRUE(second->available);

    DeleteFileA(user_path.c_str());
    DeleteFileA(order_path.c_str());
}

TEST(SessionIntegration, candidate_order_marks_hidden_pinned_entry_unavailable) {
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    const std::string order_path = test_user_data_dir + "\\candidate_order_pinyin.tsv";
    const std::string disabled_path = test_user_data_dir + "\\disabled_pinyin.tsv";
    DeleteFileA(user_path.c_str());
    DeleteFileA(order_path.c_str());
    DeleteFileA(disabled_path.c_str());

    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    const auto initial = manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 16);
    const auto system =
        std::find_if(initial.entries.begin(), initial.entries.end(), [](const auto& entry) {
            return entry.available && entry.reason == cxxime::CandidateOrderReason::kDefault;
        });
    ASSERT_TRUE(system != initial.entries.end());
    const cxxime::ManualCandidateOrderEntry pinned = {system->text, system->code,
                                                      system->syllables};
    bool version_conflict = false;
    ASSERT_EQ(manager.replace_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", {pinned},
                                              initial.version, &version_conflict),
              cxxime::IPCStatus::OK);
    ASSERT_TRUE(!version_conflict);
    ASSERT_EQ(manager.disable_system_entry(cxxime::UserDictKind::PINYIN, pinned.text),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "visible-user", "nihao"),
              cxxime::IPCStatus::OK);

    const auto limited = manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 1);
    const auto hidden =
        std::find_if(limited.entries.begin(), limited.entries.end(), [&](const auto& entry) {
            return entry.text == pinned.text && entry.code == pinned.code &&
                   entry.syllables == pinned.syllables;
        });
    ASSERT_TRUE(hidden != limited.entries.end());
    ASSERT_EQ(hidden->reason, cxxime::CandidateOrderReason::kManual);
    ASSERT_TRUE(!hidden->available);

    DeleteFileA(user_path.c_str());
    DeleteFileA(order_path.c_str());
    DeleteFileA(disabled_path.c_str());
}

TEST(SessionIntegration, composed_candidate_order_entry_is_not_available) {
    const std::string dict_path = make_temp_path("test_composed_candidate_order.bin");
    create_test_dictionary_bundle(dict_path, {{"a", "piece", 900}});
    const std::string learning_path = test_user_data_dir + "\\learning_pinyin.tsv";
    const std::string order_path = test_user_data_dir + "\\candidate_order_pinyin.tsv";
    DeleteFileA(learning_path.c_str());
    DeleteFileA(order_path.c_str());

    SessionManager manager;
    ASSERT_TRUE(manager.initialize(dict_path));
    const auto queried =
        manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "aaaaaaa", 16);
    const auto composed =
        std::find_if(queried.entries.begin(), queried.entries.end(), [](const auto& entry) {
            return entry.text == "piecepiecepiecepiecepiecepiecepiece";
        });
    ASSERT_TRUE(composed != queried.entries.end());
    ASSERT_TRUE(!composed->available);

    bool version_conflict = false;
    const std::vector<cxxime::ManualCandidateOrderEntry> order = {
        {composed->text, composed->code, composed->syllables}};
    ASSERT_TRUE(manager.replace_candidate_order(cxxime::UserDictKind::PINYIN, "aaaaaaa", order,
                                                queried.version,
                                                &version_conflict) != cxxime::IPCStatus::OK);
    ASSERT_TRUE(!version_conflict);

    DeleteFileA(learning_path.c_str());
    DeleteFileA(order_path.c_str());
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, stale_learned_candidate_order_entry_is_not_available) {
    const std::string dict_path = make_temp_path("test_stale_learned_candidate_order.bin");
    create_test_dictionary_bundle(dict_path, {{"ni:hao", "valid-candidate", 900}});
    const std::string learning_path = test_user_data_dir + "\\learning_pinyin.tsv";
    const std::string order_path = test_user_data_dir + "\\candidate_order_pinyin.tsv";
    DeleteFileA(order_path.c_str());
    {
        std::ofstream output(learning_path, std::ios::binary | std::ios::trunc);
        output << "stale-candidate\tnihao\tnihao\t1\t1\tni:hao\n";
    }

    auto config = std::make_shared<cxxime::Config>();
    config->candidate_learning = true;
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(dict_path, config));
    const auto queried = manager.query_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", 16);
    const auto stale =
        std::find_if(queried.entries.begin(), queried.entries.end(),
                     [](const auto& entry) { return entry.text == "stale-candidate"; });
    ASSERT_TRUE(stale != queried.entries.end());
    ASSERT_EQ(stale->reason, cxxime::CandidateOrderReason::kLearned);
    ASSERT_TRUE(!stale->available);

    bool version_conflict = false;
    const std::vector<cxxime::ManualCandidateOrderEntry> order = {
        {stale->text, stale->code, stale->syllables}};
    ASSERT_TRUE(manager.replace_candidate_order(cxxime::UserDictKind::PINYIN, "nihao", order,
                                                queried.version,
                                                &version_conflict) != cxxime::IPCStatus::OK);
    ASSERT_TRUE(!version_conflict);

    DeleteFileA(learning_path.c_str());
    DeleteFileA(order_path.c_str());
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionIntegration, user_lexicon_save_failure_keeps_memory_unchanged) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));

    const auto query = [&](const std::string& text) {
        return manager.query_user_entries(text, cxxime::UserDictKind::PINYIN, 0, 10);
    };
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    ASSERT_EQ(
        manager.add_user_entry(cxxime::UserDictKind::PINYIN, "rollback-baseline", "rollbackcode"),
        cxxime::IPCStatus::OK);
    ASSERT_EQ(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "rollback-second",
                                     "rollbacksecond"),
              cxxime::IPCStatus::OK);
    ASSERT_TRUE(DeleteFileA(user_path.c_str()) != FALSE);
    ASSERT_TRUE(CreateDirectoryA(user_path.c_str(), nullptr) != FALSE);

    ASSERT_TRUE(manager.add_user_entry(cxxime::UserDictKind::PINYIN, "rollback-added",
                                       "rollbackadded") != cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-added").match_total, static_cast<std::size_t>(0));

    ASSERT_TRUE(manager.replace_user_entry(cxxime::UserDictKind::PINYIN, "rollback-baseline",
                                           "rollbackcode", "rollback-replaced",
                                           "rollbackreplaced") != cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-baseline").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(query("rollback-replaced").match_total, static_cast<std::size_t>(0));

    const std::vector<cxxime::LexiconEntryKey> entries = {
        {"rollback-baseline", "rollbackcode"}, {"rollback-second", "rollbacksecond"}};
    ASSERT_TRUE(manager.delete_user_entries(cxxime::UserDictKind::PINYIN, entries) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-baseline").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(query("rollback-second").match_total, static_cast<std::size_t>(1));

    ASSERT_TRUE(RemoveDirectoryA(user_path.c_str()) != FALSE);
    ASSERT_EQ(manager.delete_user_entries(cxxime::UserDictKind::PINYIN, entries),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(query("rollback-baseline").match_total, static_cast<std::size_t>(0));
    ASSERT_EQ(query("rollback-second").match_total, static_cast<std::size_t>(0));
}

TEST(SessionIntegration, user_lexicon_import_is_atomic_and_server_owned) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));

    const auto query = [&](const std::string& text) {
        return manager.query_user_entries(text, cxxime::UserDictKind::PINYIN, 0, 10);
    };
    const std::string user_path = test_user_data_dir + "\\user_pinyin.tsv";
    const std::string valid_source = make_temp_path("valid_user_import.tsv");
    const std::string invalid_source = make_temp_path("invalid_user_import.tsv");
    const std::string missing_source = make_temp_path("missing_user_import.tsv");
    const std::string oversized_source = make_temp_path("oversized_user_import.tsv");
    const std::string replacement_source = make_temp_path("replacement_user_import.tsv");
    const std::string empty_source = make_temp_path("empty_user_import.tsv");
    DeleteFileA(missing_source.c_str());
    {
        HANDLE file = CreateFileA(oversized_source.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
        LARGE_INTEGER size = {};
        size.QuadPart = static_cast<LONGLONG>(cxxime::kMaxUserDictImportBytes + 1);
        ASSERT_TRUE(SetFilePointerEx(file, size, nullptr, FILE_BEGIN) != FALSE);
        ASSERT_TRUE(SetEndOfFile(file) != FALSE);
        CloseHandle(file);
    }

    ASSERT_EQ(
        manager.add_user_entry(cxxime::UserDictKind::PINYIN, "import-baseline", "importbaseline"),
        cxxime::IPCStatus::OK);
    const std::string baseline_contents = read_text_file(user_path);
    {
        std::ofstream output(invalid_source, std::ios::binary | std::ios::trunc);
        output.write("\xc3\x28", 2);
        output << "\tvalid\t1\n";
    }
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, invalid_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(query("import-baseline").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(read_text_file(user_path), baseline_contents);
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, missing_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(read_text_file(user_path), baseline_contents);
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, oversized_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(read_text_file(user_path), baseline_contents);

    {
        std::ofstream output(valid_source, std::ios::binary | std::ios::trunc);
        output << "imported-entry\timportedcode\t7\n";
    }
    ASSERT_EQ(manager.import_user_dict(cxxime::UserDictKind::PINYIN, valid_source),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(query("import-baseline").match_total, static_cast<std::size_t>(0));
    ASSERT_EQ(query("imported-entry").match_total, static_cast<std::size_t>(1));
    const std::string imported_contents = read_text_file(user_path);

    {
        std::ofstream output(replacement_source, std::ios::binary | std::ios::trunc);
        output << "replacement-entry\treplacementcode\t8\n";
    }
    HANDLE user_lock = CreateFileA(user_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(user_lock != INVALID_HANDLE_VALUE);
    ASSERT_TRUE(manager.import_user_dict(cxxime::UserDictKind::PINYIN, replacement_source) !=
                cxxime::IPCStatus::OK);
    ASSERT_EQ(query("imported-entry").match_total, static_cast<std::size_t>(1));
    ASSERT_EQ(query("replacement-entry").match_total, static_cast<std::size_t>(0));
    ASSERT_EQ(read_text_file(user_path), imported_contents);
    CloseHandle(user_lock);

    {
        std::ofstream output(empty_source, std::ios::binary | std::ios::trunc);
    }
    ASSERT_EQ(manager.import_user_dict(cxxime::UserDictKind::PINYIN, empty_source),
              cxxime::IPCStatus::OK);
    ASSERT_EQ(query("").match_total, static_cast<std::size_t>(0));
    DeleteFileA(valid_source.c_str());
    DeleteFileA(invalid_source.c_str());
    DeleteFileA(oversized_source.c_str());
    DeleteFileA(replacement_source.c_str());
    DeleteFileA(empty_source.c_str());
}
