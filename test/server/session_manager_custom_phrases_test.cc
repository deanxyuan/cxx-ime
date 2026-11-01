// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager_integration_test_support.h"

#include <cxxime/custom_phrases.h>

namespace {

cxxime::LexiconControlResult execute_phrase_request(SessionManager& manager,
                                                    cxxime::LexiconControlRequest request) {
    request.resource = cxxime::LexiconResource::kCustomPhrases;
    std::string encoded;
    std::string response;
    cxxime::LexiconControlResult result;
    ASSERT_TRUE(cxxime::encode_lexicon_request(request, &encoded));
    ASSERT_TRUE(handle_lexicon_control_request(manager, encoded, &response));
    ASSERT_TRUE(cxxime::decode_lexicon_result(response, &result));
    return result;
}

ProcessKeyResult type_phrase(SessionManager& manager, std::uint32_t session, const char* code) {
    ProcessKeyResult result;
    for (const char* key = code; *key; ++key) {
        result = manager.process_key(session, make_key(static_cast<unsigned>(*key - 'a' + 'A')));
        ASSERT_TRUE(result.commit_text.empty());
    }
    return result;
}

} // namespace

TEST(SessionIntegration, custom_phrases_control_sessions_reload_and_backup_share_one_store) {
    const auto path = test_user_data_dir + "\\custom_phrases.tsv";
    DeleteFileA(path.c_str());
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    const auto first = manager.create_session();
    const auto second = manager.create_session();
    cxxime::LexiconControlRequest request;
    request.operation = cxxime::LexiconOperation::kAdd;
    request.code = "jtdz";
    request.text = "Address 1";
    ASSERT_TRUE(execute_phrase_request(manager, request).succeeded);
    request.text = "Address 2";
    ASSERT_TRUE(execute_phrase_request(manager, request).succeeded);
    request.operation = cxxime::LexiconOperation::kMoveUp;
    ASSERT_TRUE(execute_phrase_request(manager, request).succeeded);
    for (auto session : {first, second}) {
        const auto result = type_phrase(manager, session, "jtdz");
        ASSERT_EQ(result.presentation.items[0].text, "Address 2");
        ASSERT_EQ(manager.process_key(session, make_key(VK_SPACE)).commit_text, "Address 2");
    }
    std::map<std::string, std::string> backup;
    ASSERT_TRUE(manager.snapshot_user_data({"custom_phrases.tsv"}, &backup));
    ASSERT_TRUE(cxxime::CustomPhrases::validate_contents(backup.at("custom_phrases.tsv")));
    request.operation = cxxime::LexiconOperation::kDelete;
    request.entries = {{"Address 1", "jtdz"}, {"Address 2", "jtdz"}};
    ASSERT_TRUE(execute_phrase_request(manager, request).succeeded);
    std::size_t imported = 0;
    std::size_t skipped = 0;
    manager.merge_user_data(backup, &imported, &skipped);
    ASSERT_EQ(imported, std::size_t{2});
    ASSERT_EQ(skipped, std::size_t{0});
    ASSERT_EQ(manager.reload_dictionaries(), cxxime::IPCStatus::OK);
    ASSERT_EQ(type_phrase(manager, first, "jtdz").presentation.items[0].text, "Address 2");
    manager.process_key(first, make_key(VK_ESCAPE));
    request = {};
    request.operation = cxxime::LexiconOperation::kQuery;
    request.kind = cxxime::UserDictKind::WUBI;
    request.query = "jtdz";
    const auto queried = execute_phrase_request(manager, request);
    ASSERT_TRUE(queried.succeeded);
    ASSERT_EQ(queried.query.entries.size(), std::size_t{2});
    ASSERT_EQ(queried.query.entries[0].text, "Address 2");
    DeleteFileA(path.c_str());
}
