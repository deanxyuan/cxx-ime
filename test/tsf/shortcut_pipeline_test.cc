// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "ipc_response_builder.h"
#include "server/session_manager_integration_test_support.h"
#include "text_service_test_support.h"

namespace {

class ShortcutData {
public:
    std::string directory;
    std::string dictionary;

    ShortcutData() {
        char temporary[MAX_PATH] = {};
        char unique[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, temporary));
        ASSERT_TRUE(GetTempFileNameA(temporary, "cxk", 0, unique));
        ASSERT_TRUE(DeleteFileA(unique));
        ASSERT_TRUE(CreateDirectoryA(unique, nullptr));
        directory = unique;
        dictionary = directory + "\\shortcut.bin";
        cxxime::set_data_dir(CXXIME_DATA_DIR);
        cxxime::set_user_data_dir(directory);
        create_test_dictionary_bundle(dictionary, {{"ni", "test", 100}});
    }
    ~ShortcutData() {
        WIN32_FIND_DATAA file = {};
        HANDLE search = FindFirstFileA((directory + "\\*").c_str(), &file);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    ASSERT_TRUE(DeleteFileA((directory + "\\" + file.cFileName).c_str()));
                }
            } while (FindNextFileA(search, &file));
            FindClose(search);
        }
        ASSERT_TRUE(RemoveDirectoryA(directory.c_str()));
    }
};

class ShortcutPipeline {
public:
    ShortcutData data;
    SessionManager manager;
    tsf_test::KeyboardState keyboard;
    tsf_test::Fixture host;

    ShortcutPipeline() {
        auto config = std::make_shared<cxxime::Config>();
        config->ascii_switch_key["Shift_L"] = "clear";
        ASSERT_TRUE(manager.initialize(data.dictionary, config));
        keyboard.caps(false);
        host.view.foreground_window = true;
        host.host.active_view = &host.view;
        host.server.server.set_handler([this](const cxxime::IPCRequest& request) {
            cxxime::IPCResponse response;
            switch (request.command) {
            case cxxime::IPCCommand::START_SESSION:
                response.highlighted = manager.create_session(request.client_capabilities);
                break;
            case cxxime::IPCCommand::END_SESSION:
                manager.destroy_session(request.session_id);
                break;
            case cxxime::IPCCommand::PROCESS_KEY:
                fill_key_response(
                    manager.process_key(request.session_id, {request.key_code, request.modifiers,
                                                             request.is_key_up != 0}),
                    request.key_code, &response);
                break;
            case cxxime::IPCCommand::FOCUS_IN:
                fill_process_response(manager.focus_in(request.session_id), &response);
                break;
            case cxxime::IPCCommand::FOCUS_OUT:
                fill_process_response(manager.focus_out(request.session_id), &response);
                break;
            case cxxime::IPCCommand::CLEAR_COMPOSITION:
                fill_process_response(manager.clear_composition(request.session_id), &response);
                break;
            default:
                const auto status = manager.get_ime_status(request.session_id);
                response.status = status.first;
                response.ime_status = status.second;
                break;
            }
            return response;
        });
        ASSERT_TRUE(TextServiceTestPeer::connect(host.service, host.server.pipe));
    }

    bool key(uint32_t key, bool up = false, bool shift = false) {
        BYTE state[256] = {};
        state[VK_SHIFT] = shift ? 0x80 : 0;
        ASSERT_TRUE(SetKeyboardState(state));
        BOOL eaten = FALSE;
        if (up) {
            ASSERT_EQ(host.service.OnTestKeyUp(&host.host, key, 0, &eaten), S_OK);
            if (eaten) {
                ASSERT_EQ(host.service.OnKeyUp(&host.host, key, 0, &eaten), S_OK);
            }
        } else {
            ASSERT_EQ(host.service.OnTestKeyDown(&host.host, key, 0, &eaten), S_OK);
            if (eaten) {
                ASSERT_EQ(host.service.OnKeyDown(&host.host, key, 0, &eaten), S_OK);
            }
        }
        return eaten != FALSE;
    }
};

} // namespace

TEST(ShortcutPipeline, engine_through_pipe_to_tsf_preserves_each_intermediate_state) {
    ShortcutPipeline pipeline;
    ASSERT_TRUE(!pipeline.key(VK_ESCAPE));
    ASSERT_TRUE(pipeline.key('N'));
    ASSERT_TRUE(pipeline.key('I'));
    ASSERT_EQ(pipeline.host.host.range.text, L"ni");
    const int writes = pipeline.host.host.range.writes;
    const auto generation = TextServiceTestPeer::generation(pipeline.host.service);
    ASSERT_TRUE(!pipeline.key(VK_LSHIFT, false, true));
    ASSERT_EQ(pipeline.host.host.range.writes, writes);
    for (int repeat = 0; repeat < 3; ++repeat) {
        ASSERT_TRUE(!pipeline.key(VK_RETURN, false, true));
        ASSERT_EQ(pipeline.host.host.range.writes, writes);
        ASSERT_EQ(pipeline.host.host.composition.ends, 0);
        ASSERT_EQ(pipeline.host.host.range.text, L"ni");
    }
    ASSERT_TRUE(!pipeline.key(VK_LSHIFT, true));
    ASSERT_TRUE(!pipeline.key(VK_RETURN, true));
    ASSERT_EQ(pipeline.host.host.range.writes, writes);
    ASSERT_EQ(TextServiceTestPeer::generation(pipeline.host.service), generation);
    ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
    ASSERT_TRUE(pipeline.key(VK_SPACE, false, true));
    ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).full_shape());
    ASSERT_EQ(pipeline.host.host.range.writes, writes);
    ASSERT_TRUE(pipeline.key(VK_RETURN));
    ASSERT_TRUE(TextServiceTestPeer::empty(pipeline.host.service));
    ASSERT_EQ(pipeline.host.host.range.text, L"ni");
    ASSERT_EQ(pipeline.host.host.composition.ends, 1);
    ASSERT_TRUE(!pipeline.key(VK_ESCAPE));
}

TEST(ShortcutPipeline, standalone_modifier_clears_once_and_switches_without_host_replay) {
    ShortcutPipeline pipeline;
    ASSERT_TRUE(pipeline.key('N'));
    ASSERT_TRUE(pipeline.key('I'));
    const int writes = pipeline.host.host.range.writes;
    ASSERT_TRUE(!pipeline.key(VK_LSHIFT, false, true));
    ASSERT_EQ(pipeline.host.host.range.writes, writes);
    ASSERT_TRUE(pipeline.key(VK_LSHIFT, true));
    ASSERT_TRUE(TextServiceTestPeer::empty(pipeline.host.service));
    ASSERT_TRUE(pipeline.host.host.range.text.empty());
    ASSERT_EQ(pipeline.host.host.composition.ends, 1);
    ASSERT_TRUE(!TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
    const int cleared_writes = pipeline.host.host.range.writes;
    ASSERT_TRUE(!pipeline.key(VK_LSHIFT, false, true));
    ASSERT_TRUE(pipeline.key(VK_LSHIFT, true));
    ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
    ASSERT_EQ(pipeline.host.host.range.writes, cleared_writes);
    ASSERT_EQ(pipeline.host.host.composition.ends, 1);
}
