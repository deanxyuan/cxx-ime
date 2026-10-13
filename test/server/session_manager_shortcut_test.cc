// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>

#include <cxxime/ipc_client.h>
#include <cxxime/ipc_server.h>
#include <cxxime/pipe_names.h>

#include "ipc_response_builder.h"
#include "session_manager_integration_test_support.h"

namespace {

cxxime::IPCResponse process(SessionManager& manager, uint32_t session, uint32_t key,
                            uint32_t modifiers = 0, bool up = false) {
    cxxime::IPCResponse response;
    fill_key_response(manager.process_key(session, {key, modifiers, up}), key, &response);
    ASSERT_EQ(response.status, cxxime::IPCStatus::OK);
    return response;
}

void assert_result(const cxxime::IPCResponse& response, cxxime::KeyDisposition disposition,
                   bool changed) {
    ASSERT_EQ(response.key_disposition, disposition);
    ASSERT_EQ(response.presentation_changed, changed ? 1u : 0u);
}

} // namespace

TEST(SessionShortcut, ownership_and_presentation_are_independent) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    const auto session = manager.create_session(cxxime::kClientCapabilityExplicitKeyResult);
    for (uint32_t key : {VK_ESCAPE, VK_RETURN, VK_SPACE, VK_LEFT, VK_F1}) {
        assert_result(process(manager, session, key), cxxime::KeyDisposition::PassToHost, false);
    }
    assert_result(process(manager, session, 'N'), cxxime::KeyDisposition::Handled, true);
    const auto initial = process(manager, session, 'I');
    ASSERT_EQ(std::string(initial.preedit), "ni");
    for (uint32_t key : {'A', 'C', 'V'}) {
        for (bool up : {false, false, true}) {
            const auto response = process(manager, session, key, cxxime::kKeyModifierControl, up);
            assert_result(response,
                          up ? cxxime::KeyDisposition::PassToHost : cxxime::KeyDisposition::Handled,
                          false);
            ASSERT_EQ(std::string(response.preedit), "ni");
            ASSERT_EQ(response.preedit_cursor, initial.preedit_cursor);
            ASSERT_EQ(response.candidate_count, initial.candidate_count);
            ASSERT_EQ(response.commit_text[0], '\0');
        }
    }
    for (uint32_t modifiers :
         {cxxime::kKeyModifierControl, cxxime::kKeyModifierAlt, cxxime::kKeyModifierWin}) {
        for (bool up : {false, false, true}) {
            const auto response = process(manager, session, VK_RETURN, modifiers, up);
            assert_result(response, cxxime::KeyDisposition::PassToHost, false);
            ASSERT_EQ(std::string(response.preedit), "ni");
            ASSERT_TRUE(response.composing);
            ASSERT_EQ(response.candidate_count, initial.candidate_count);
            ASSERT_EQ(response.commit_text[0], '\0');
        }
    }
    const auto shape = process(manager, session, VK_SPACE, cxxime::kKeyModifierShift);
    assert_result(shape, cxxime::KeyDisposition::Handled, false);
    ASSERT_TRUE(shape.ime_status.full_shape());
    const auto punctuation = process(manager, session, VK_OEM_PERIOD, cxxime::kKeyModifierControl);
    assert_result(punctuation, cxxime::KeyDisposition::Handled, false);
    ASSERT_TRUE(!punctuation.ime_status.chinese_punct());
    assert_result(process(manager, session, VK_HOME), cxxime::KeyDisposition::Handled, true);
    assert_result(process(manager, session, VK_HOME), cxxime::KeyDisposition::Handled, false);
    assert_result(process(manager, session, VK_ESCAPE), cxxime::KeyDisposition::Handled, true);
    assert_result(process(manager, session, VK_ESCAPE), cxxime::KeyDisposition::PassToHost, false);
}

TEST(SessionShortcut, modifier_bindings_return_explicit_results_for_each_action) {
    for (const char* binding : {"noop", "clear", "code", "candidate", "inline_ascii"}) {
        for (bool composing : {false, true}) {
            auto config = std::make_shared<cxxime::Config>();
            config->ascii_switch_key["Shift_L"] = binding;
            SessionManager manager;
            ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
            const auto session = manager.create_session(cxxime::kClientCapabilityExplicitKeyResult);
            if (composing) {
                process(manager, session, 'N');
                process(manager, session, 'I');
            }
            assert_result(process(manager, session, VK_LSHIFT, cxxime::kKeyModifierShift),
                          cxxime::KeyDisposition::PassToHost, false);
            const auto release = process(manager, session, VK_LSHIFT, 0, true);
            const bool noop = std::string(binding) == "noop";
            assert_result(release,
                          noop ? cxxime::KeyDisposition::PassToHost
                               : cxxime::KeyDisposition::Handled,
                          composing && !noop);
            if (composing &&
                (std::string(binding) == "code" || std::string(binding) == "candidate")) {
                ASSERT_NE(release.commit_text[0], '\0');
            } else {
                ASSERT_EQ(release.commit_text[0], '\0');
            }
        }
    }
}

TEST(SessionShortcut, global_mode_alignment_can_clear_composition_and_pass_key) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    const auto a = manager.create_session(cxxime::kClientCapabilityExplicitKeyResult);
    const auto b = manager.create_session();
    process(manager, a, 'N');
    process(manager, a, 'I');
    ASSERT_EQ(manager.switch_input_mode(b, cxxime::InputMode::WUBI).first, cxxime::IPCStatus::OK);
    const auto response = process(manager, a, VK_RETURN, cxxime::kKeyModifierShift);
    assert_result(response, cxxime::KeyDisposition::PassToHost, true);
    ASSERT_TRUE(!response.composing);
    ASSERT_EQ(response.preedit[0], '\0');
    ASSERT_EQ(response.commit_text[0], '\0');
}

TEST(SessionShortcut, caps_lock_bindings_do_not_lose_or_delay_commits) {
    for (const char* binding : {"clear", "code", "candidate", "append", "noop"}) {
        auto config = std::make_shared<cxxime::Config>();
        config->ascii_switch_key["Caps_Lock"] = binding;
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
        const auto session = manager.create_session(cxxime::kClientCapabilityExplicitKeyResult);
        process(manager, session, 'N');
        process(manager, session, 'I');
        const auto response = process(manager, session, VK_CAPITAL, cxxime::kKeyModifierCapsLock);
        const bool commits = std::string(binding) == "code" || std::string(binding) == "candidate";
        const bool clears = std::string(binding) == "clear";
        ASSERT_EQ(response.key_disposition, cxxime::KeyDisposition::Handled);
        ASSERT_TRUE(response.ime_status.caps_lock());
        ASSERT_EQ(response.commit_text[0] != '\0', commits);
        if (commits || clears) {
            ASSERT_TRUE(response.presentation_changed);
            ASSERT_TRUE(!response.composing);
        }
        const auto next = process(manager, session, VK_F1, cxxime::kKeyModifierCapsLock);
        assert_result(next, cxxime::KeyDisposition::PassToHost, false);
        ASSERT_EQ(next.commit_text[0], '\0');
    }
}

TEST(SessionShortcut, modern_and_legacy_sessions_share_real_transport_without_semantic_mixing) {
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict()));
    cxxime::IpcServer server;
    server.set_handler([&](const cxxime::IPCRequest& request) {
        cxxime::IPCResponse response;
        if (request.command == cxxime::IPCCommand::START_SESSION) {
            response.highlighted = manager.create_session(request.client_capabilities);
        } else if (request.command == cxxime::IPCCommand::PROCESS_KEY) {
            fill_key_response(
                manager.process_key(request.session_id,
                                    {request.key_code, request.modifiers, request.is_key_up != 0}),
                request.key_code, &response);
        }
        return response;
    });
    const std::wstring pipe =
        L"\\\\.\\pipe\\CxxIME-KeyContract-" + std::to_wstring(GetCurrentProcessId());
    ASSERT_TRUE(server.start(pipe));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto full_pipe = cxxime::make_user_pipe_name(pipe);
    while (!WaitNamedPipeW(full_pipe.c_str(), 2000)) {
        ASSERT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
        ASSERT_TRUE(std::chrono::steady_clock::now() < deadline);
        // The accept thread has not created its first instance yet.
        Sleep(1);
    }
    cxxime::IpcClient modern;
    cxxime::IpcClient legacy;
    ASSERT_TRUE(modern.connect(pipe));
    ASSERT_TRUE(legacy.connect(pipe));
    uint32_t modern_id = 0;
    uint32_t legacy_id = 0;
    ASSERT_TRUE(modern.start_session(modern_id, cxxime::kClientCapabilityExplicitKeyResult));
    ASSERT_TRUE(legacy.start_session(legacy_id));
    const uint32_t keys[] = {'N', 'I', VK_F4};
    for (uint32_t key : keys) {
        const auto modifiers = key == VK_F4 ? cxxime::kKeyModifierAlt : 0u;
        cxxime::IPCResponse next;
        cxxime::IPCResponse old;
        ASSERT_TRUE(modern.process_key(modern_id, key, modifiers, next));
        ASSERT_TRUE(legacy.process_key(legacy_id, key, modifiers, old));
        ASSERT_EQ(next.status, cxxime::IPCStatus::OK);
        ASSERT_EQ(old.status, key == VK_F4 ? cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED
                                           : cxxime::IPCStatus::OK);
        ASSERT_EQ(old.key_disposition, cxxime::KeyDisposition::Unspecified);
        ASSERT_EQ(old.key_handled, 0u);
        ASSERT_EQ(std::string(next.preedit), std::string(old.preedit));
        assert_result(next,
                      key == VK_F4 ? cxxime::KeyDisposition::PassToHost
                                   : cxxime::KeyDisposition::Handled,
                      key != VK_F4);
    }
    modern.disconnect();
    legacy.disconnect();
    server.stop();
}

TEST(SessionShortcut, serialization_errors_do_not_publish_partial_commit_or_key_instructions) {
    ProcessKeyResult result;
    result.status = cxxime::IPCStatus::OK;
    result.explicit_key_result = true;
    result.key_disposition = cxxime::KeyDisposition::Handled;
    result.presentation_changed = true;
    result.result = cxxime::ProcessResult::COMMITTED;
    result.commit_text = "committed";
    result.composing = true;
    result.preedit = "n";
    result.preedit_cursor = 2; // Invalid cursor after an otherwise valid commit.
    cxxime::IPCResponse response;
    fill_key_response(result, VK_RETURN, &response);
    ASSERT_EQ(response.status, cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED);
    assert_result(response, cxxime::KeyDisposition::Unspecified, false);
    ASSERT_EQ(response.commit_text[0], '\0');
    ASSERT_EQ(response.preedit[0], '\0');
}

TEST(SessionShortcut, legacy_caps_snapshots_keep_their_original_status) {
    for (const char* binding : {"noop", "append", "clear", "code"}) {
        auto config = std::make_shared<cxxime::Config>();
        config->ascii_switch_key["Caps_Lock"] = binding;
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
        const auto session = manager.create_session();
        manager.process_key(session, {'N'});
        manager.process_key(session, {'I'});
        const auto result =
            manager.process_key(session, {VK_CAPITAL, cxxime::kKeyModifierCapsLock});
        cxxime::IPCResponse response;
        fill_key_response(result, VK_CAPITAL, &response);
        const bool preserves = std::string(binding) == "noop" || std::string(binding) == "append";
        ASSERT_EQ(response.status,
                  preserves ? cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED : cxxime::IPCStatus::OK);
        ASSERT_EQ(response.key_disposition, cxxime::KeyDisposition::Unspecified);
        ASSERT_EQ(response.key_handled, 0u);
        ASSERT_EQ(response.commit_text[0] != '\0', std::string(binding) == "code");
    }
}

TEST(SessionShortcut, focus_out_discards_incomplete_modifier_tap) {
    auto config = std::make_shared<cxxime::Config>();
    config->ascii_switch_key["Shift_L"] = "code";
    SessionManager manager;
    ASSERT_TRUE(manager.initialize(setup_test_dict(), config));
    const auto session = manager.create_session(cxxime::kClientCapabilityExplicitKeyResult);
    for (bool delayed_release : {false, true}) {
        assert_result(process(manager, session, VK_LSHIFT, cxxime::kKeyModifierShift),
                      cxxime::KeyDisposition::PassToHost, false);
        ASSERT_EQ(manager.focus_out(session).status, cxxime::IPCStatus::OK);
        ASSERT_EQ(manager.focus_in(session).status, cxxime::IPCStatus::OK);
        if (delayed_release) {
            const auto release = process(manager, session, VK_LSHIFT, 0, true);
            assert_result(release, cxxime::KeyDisposition::PassToHost, false);
            ASSERT_TRUE(release.ime_status.chinese_mode());
        }
        // Even when the previous release was lost, a fresh standalone tap works.
        process(manager, session, VK_LSHIFT, cxxime::kKeyModifierShift);
        const auto release = process(manager, session, VK_LSHIFT, 0, true);
        assert_result(release, cxxime::KeyDisposition::Handled, false);
        ASSERT_TRUE(!release.ime_status.chinese_mode());
        process(manager, session, VK_LSHIFT, cxxime::kKeyModifierShift);
        ASSERT_TRUE(process(manager, session, VK_LSHIFT, 0, true).ime_status.chinese_mode());
    }
}
