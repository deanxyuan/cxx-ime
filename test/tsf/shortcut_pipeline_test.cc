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
        create_test_dictionary_bundle(
            dictionary, {{"ni", "test", 100}, {"ni", "second", 90}, {"ni", "third", 80}});
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
        config->page_size = 1;
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

    bool key(uint32_t key, bool up = false, bool shift = false, bool ctrl = false) {
        BYTE state[256] = {};
        state[VK_SHIFT] = shift ? 0x80 : 0;
        state[VK_CONTROL] = ctrl ? 0x80 : 0;
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

// Native document navigation is the host's fallback after TSF declines a key.
// Composition edit sessions remain in the existing controlled COM host above.
class NavigationDocument {
public:
    struct Snapshot {
        DWORD start = 0;
        DWORD end = 0;
        LRESULT first_line = 0;
        std::wstring text;
    };

    NavigationDocument() {
        parent_ =
            CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"CxxIME navigation test", WS_POPUP, 0, 0,
                            480, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(parent_ != nullptr);
        window_ = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0,
            0, 480, 160, parent_, nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(window_ != nullptr);
        SendMessageW(window_, WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(SYSTEM_FIXED_FONT)), FALSE);
        std::wstring text;
        for (int line = 0; line < 120; ++line) {
            text += L"Document line " + std::to_wstring(line) + L" for editing.\r\n";
        }
        ASSERT_TRUE(SetWindowTextW(window_, text.c_str()));
        // The target substitutes keyboard samples for TSF. A native EDIT needs native state.
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto read_keyboard =
            reinterpret_cast<KeyboardFunction>(GetProcAddress(user32, "GetKeyboardState"));
        write_keyboard_ =
            reinterpret_cast<KeyboardFunction>(GetProcAddress(user32, "SetKeyboardState"));
        ASSERT_TRUE(read_keyboard != nullptr && write_keyboard_ != nullptr);
        ASSERT_TRUE(read_keyboard(original_keyboard_));
        reset_position();
    }
    ~NavigationDocument() {
        ASSERT_TRUE(write_keyboard_(original_keyboard_));
        ASSERT_TRUE(DestroyWindow(parent_));
    }
    void reset_position() {
        const LRESULT middle_line = SendMessageW(window_, EM_GETLINECOUNT, 0, 0) / 2;
        const LRESULT middle = SendMessageW(window_, EM_LINEINDEX, middle_line, 0);
        ASSERT_TRUE(middle > 0);
        SendMessageW(window_, EM_SETSEL, middle + 5, middle + 5);
        // Arrange the viewport explicitly without taking keyboard focus.
        const LRESULT first_line = SendMessageW(window_, EM_GETFIRSTVISIBLELINE, 0, 0);
        const LRESULT scrolled = SendMessageW(window_, EM_LINESCROLL, 0, middle_line - first_line);
        RECT text_rect = {};
        SendMessageW(window_, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&text_rect));
        ASSERT_TRUE(snapshot().first_line > 0)
            << middle_line << middle << scrolled << text_rect.right << text_rect.bottom;
    }
    Snapshot snapshot() const {
        Snapshot result;
        SendMessageW(window_, EM_GETSEL, reinterpret_cast<WPARAM>(&result.start),
                     reinterpret_cast<LPARAM>(&result.end));
        result.first_line = SendMessageW(window_, EM_GETFIRSTVISIBLELINE, 0, 0);
        result.text.resize(GetWindowTextLengthW(window_) + 1);
        const int length =
            GetWindowTextW(window_, result.text.data(), static_cast<int>(result.text.size()));
        result.text.resize(length);
        return result;
    }
    void assert_unchanged(const Snapshot& before) const {
        const auto after = snapshot();
        ASSERT_EQ(after.start, before.start);
        ASSERT_EQ(after.end, before.end);
        ASSERT_EQ(after.first_line, before.first_line);
        ASSERT_EQ(after.text, before.text);
    }
    void key(uint32_t key, bool up, bool shift, bool ctrl) {
        BYTE state[256] = {};
        state[VK_SHIFT] = shift ? 0x80 : 0;
        state[VK_CONTROL] = ctrl ? 0x80 : 0;
        ASSERT_TRUE(write_keyboard_(state));
        // Modifier presses may reach the host; only command delivery can navigate/edit it.
        if (!up && key != VK_LSHIFT && key != VK_LCONTROL) {
            ++command_key_downs;
        }
        SendMessageW(window_, up ? WM_KEYUP : WM_KEYDOWN, key, 1);
    }

    int command_key_downs = 0;

private:
    using KeyboardFunction = BOOL(WINAPI*)(PBYTE);
    HWND parent_ = nullptr;
    HWND window_ = nullptr;
    KeyboardFunction write_keyboard_ = nullptr;
    BYTE original_keyboard_[256] = {};
};

} // namespace

TEST(ShortcutPipeline, text_edits_are_ignored_without_blocking_host_functions_or_losing_preedit) {
    for (bool modifier_first : {false, true}) {
        ShortcutPipeline pipeline;
        ASSERT_TRUE(!pipeline.key(VK_ESCAPE));
        ASSERT_TRUE(pipeline.key('N'));
        ASSERT_TRUE(pipeline.key('I'));
        const int writes = pipeline.host.host.range.writes;
        const auto generation = TextServiceTestPeer::generation(pipeline.host.service);
        auto assert_unchanged = [&]() {
            ASSERT_EQ(pipeline.host.host.range.text, L"ni");
            ASSERT_EQ(pipeline.host.host.range.writes, writes);
            ASSERT_EQ(pipeline.host.host.composition.ends, 0);
            ASSERT_EQ(TextServiceTestPeer::generation(pipeline.host.service), generation);
            ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
        };
        ASSERT_TRUE(!pipeline.key(VK_LCONTROL, false, false, true));
        assert_unchanged();
        for (uint32_t key : {'C', 'V', 'A'}) {
            for (int repeat = 0; repeat < 2; ++repeat) {
                ASSERT_TRUE(pipeline.key(key, false, false, true));
                assert_unchanged();
            }
            if (key != 'A') {
                ASSERT_TRUE(!pipeline.key(key, true, false, true));
                assert_unchanged();
            }
        }
        if (modifier_first) {
            ASSERT_TRUE(!pipeline.key(VK_LCONTROL, true));
            assert_unchanged();
            ASSERT_TRUE(!pipeline.key('A', true));
        } else {
            ASSERT_TRUE(!pipeline.key('A', true, false, true));
            assert_unchanged();
            ASSERT_TRUE(!pipeline.key(VK_LCONTROL, true));
        }
        assert_unchanged();
        ASSERT_TRUE(!pipeline.key(VK_LSHIFT, false, true));
        assert_unchanged();
        ASSERT_TRUE(pipeline.key(VK_INSERT, false, true));
        assert_unchanged();
        ASSERT_TRUE(!pipeline.key(VK_INSERT, true, true));
        assert_unchanged();
        ASSERT_TRUE(!pipeline.key(VK_LSHIFT, true));
        assert_unchanged();
        for (uint32_t key : {VK_F1, VK_F5}) {
            ASSERT_TRUE(!pipeline.key(key));
            assert_unchanged();
            ASSERT_TRUE(!pipeline.key(key, true));
            assert_unchanged();
        }
        ASSERT_TRUE(pipeline.key(VK_SPACE, false, true));
        ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).full_shape());
        assert_unchanged();
        ASSERT_TRUE(pipeline.key(VK_RETURN, false, true));
        ASSERT_TRUE(TextServiceTestPeer::empty(pipeline.host.service));
        ASSERT_EQ(pipeline.host.host.range.text, L"ni");
        ASSERT_EQ(pipeline.host.host.composition.ends, 1);
        ASSERT_TRUE(!pipeline.key(VK_ESCAPE));
        for (uint32_t key : {'A', 'C', 'V'}) {
            ASSERT_TRUE(!pipeline.key(key, false, false, true));
            ASSERT_TRUE(!pipeline.key(key, true, false, true));
            ASSERT_EQ(pipeline.host.host.range.text, L"ni");
            ASSERT_EQ(pipeline.host.host.composition.ends, 1);
        }
    }
}

TEST(ShortcutPipeline, idle_shift_enter_reaches_host_in_both_release_orders) {
    for (bool modifier_first : {false, true}) {
        ShortcutPipeline pipeline;
        ASSERT_TRUE(!pipeline.key(VK_LSHIFT, false, true));
        for (int repeat = 0; repeat < 3; ++repeat) {
            ASSERT_TRUE(!pipeline.key(VK_RETURN, false, true));
            ASSERT_TRUE(TextServiceTestPeer::empty(pipeline.host.service));
            ASSERT_EQ(pipeline.host.host.range.writes, 0);
            ASSERT_EQ(pipeline.host.host.starts, 0);
            ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
        }
        if (modifier_first) {
            ASSERT_TRUE(!pipeline.key(VK_LSHIFT, true));
            ASSERT_TRUE(!pipeline.key(VK_RETURN, true));
        } else {
            ASSERT_TRUE(!pipeline.key(VK_RETURN, true, true));
            ASSERT_TRUE(!pipeline.key(VK_LSHIFT, true));
        }
        ASSERT_EQ(pipeline.host.host.range.writes, 0);
        ASSERT_EQ(pipeline.host.host.starts, 0);
        ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
        // A subsequent ordinary input still starts composition normally.
        ASSERT_TRUE(pipeline.key('N'));
        ASSERT_EQ(pipeline.host.host.range.text, L"n");
    }
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

TEST(ShortcutPipeline, overlapping_modifiers_preserve_preedit_at_every_event) {
    for (bool shift_pressed_first : {false, true}) {
        for (bool shift_released_first : {false, true}) {
            ShortcutPipeline pipeline;
            ASSERT_TRUE(pipeline.key('N'));
            ASSERT_TRUE(pipeline.key('I'));
            const int writes = pipeline.host.host.range.writes;
            const auto generation = TextServiceTestPeer::generation(pipeline.host.service);
            auto unchanged = [&](uint32_t key, bool up, bool shift, bool ctrl) {
                ASSERT_TRUE(!pipeline.key(key, up, shift, ctrl));
                ASSERT_EQ(pipeline.host.host.range.text, L"ni");
                ASSERT_EQ(pipeline.host.host.range.writes, writes);
                ASSERT_EQ(pipeline.host.host.composition.ends, 0);
                ASSERT_EQ(TextServiceTestPeer::generation(pipeline.host.service), generation);
                ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
            };
            unchanged(shift_pressed_first ? VK_LSHIFT : VK_LCONTROL, false, shift_pressed_first,
                      !shift_pressed_first);
            unchanged(shift_pressed_first ? VK_LCONTROL : VK_LSHIFT, false, true, true);
            unchanged(shift_released_first ? VK_LSHIFT : VK_LCONTROL, true, !shift_released_first,
                      shift_released_first);
            unchanged(shift_released_first ? VK_LCONTROL : VK_LSHIFT, false, !shift_released_first,
                      shift_released_first);
            unchanged(shift_released_first ? VK_LCONTROL : VK_LSHIFT, true, false, false);
            // No hidden cancellation: the original preedit can still be committed normally.
            ASSERT_TRUE(pipeline.key(VK_RETURN));
            ASSERT_EQ(pipeline.host.host.range.text, L"ni");
            ASSERT_EQ(pipeline.host.host.composition.ends, 1);
        }
    }
}

TEST(ShortcutPipeline, composing_navigation_never_scrolls_or_selects_the_host_document) {
    for (bool inline_preedit : {false, true}) {
        ShortcutPipeline pipeline;
        NavigationDocument document;
        if (!inline_preedit) {
            TextServiceTestPeer::popup_only(pipeline.host.service);
        }
        auto dispatch = [&](uint32_t key, bool up = false, bool shift = false, bool ctrl = false) {
            const bool eaten = pipeline.key(key, up, shift, ctrl);
            if (!eaten) {
                document.key(key, up, shift, ctrl);
            }
            return eaten;
        };
        auto assert_idle_page_moves = [&]() {
            document.reset_position();
            const auto before = document.snapshot();
            const int deliveries = document.command_key_downs;
            ASSERT_TRUE(!dispatch(VK_NEXT));
            const auto after = document.snapshot();
            ASSERT_TRUE(after.start != before.start);
            ASSERT_TRUE(after.first_line != before.first_line);
            ASSERT_EQ(after.text, before.text);
            ASSERT_EQ(document.command_key_downs, deliveries + 1);
            ASSERT_TRUE(!dispatch(VK_NEXT, true));
            document.assert_unchanged(after);
        };
        // Positive control: leaked keys really navigate, rather than hitting an inert mock.
        assert_idle_page_moves();
        document.reset_position();
        ASSERT_TRUE(dispatch('N'));
        ASSERT_TRUE(dispatch('I'));
        ASSERT_TRUE(!TextServiceTestPeer::candidate_page(pipeline.host.service).items.empty());
        const auto document_before = document.snapshot();
        const int deliveries = document.command_key_downs;
        auto assert_no_host_navigation = [&]() {
            document.assert_unchanged(document_before);
            ASSERT_EQ(document.command_key_downs, deliveries);
        };
        // Real candidate pages move; neither inline preedit nor popup mode moves the document.
        const auto composition_text = pipeline.host.host.range.text;
        const struct {
            uint32_t key;
            bool shift;
            int page;
        } navigation[] = {{VK_NEXT, false, 1}, {VK_NEXT, false, 2}, {VK_NEXT, false, 2},
                          {VK_PRIOR, true, 1}, {VK_PRIOR, true, 0}, {VK_PRIOR, true, 0}};
        for (const auto& action : navigation) {
            const int previous_page =
                TextServiceTestPeer::candidate_page(pipeline.host.service).page_index;
            const int writes = pipeline.host.host.range.writes;
            const int selections = pipeline.host.host.selections;
            ASSERT_TRUE(dispatch(action.key, false, action.shift));
            assert_no_host_navigation();
            ASSERT_EQ(TextServiceTestPeer::candidate_page(pipeline.host.service).page_index,
                      action.page);
            ASSERT_EQ(pipeline.host.host.range.text, composition_text);
            if (action.page == previous_page) {
                ASSERT_EQ(pipeline.host.host.range.writes, writes);
                ASSERT_EQ(pipeline.host.host.selections, selections);
            }
            ASSERT_EQ(pipeline.host.host.composition.ends, 0);
            const int after_writes = pipeline.host.host.range.writes;
            ASSERT_TRUE(!dispatch(action.key, true, action.shift));
            assert_no_host_navigation();
            ASSERT_EQ(pipeline.host.host.range.writes, after_writes);
        }
        // Stay in one composition while candidates disappear and return through real editing.
        for (bool no_candidates : {true, false}) {
            if (no_candidates) {
                ASSERT_TRUE(dispatch(VK_HOME));
                ASSERT_TRUE(dispatch(VK_DELETE));
                ASSERT_TRUE(dispatch('Z')); // ni -> i -> zi: no dictionary match.
            } else {
                ASSERT_TRUE(dispatch(VK_BACK));
                ASSERT_TRUE(dispatch('N')); // zi -> i -> ni: candidates return.
            }
            assert_no_host_navigation();
            ASSERT_EQ(TextServiceTestPeer::candidate_page(pipeline.host.service).items.empty(),
                      no_candidates);
            const auto page_before = TextServiceTestPeer::candidate_page(pipeline.host.service);
            const auto generation = TextServiceTestPeer::generation(pipeline.host.service);
            const auto preedit = TextServiceTestPeer::preedit(pipeline.host.service);
            const int writes = pipeline.host.host.range.writes;
            const int selection_count = pipeline.host.host.selections;
            auto unchanged = [&]() {
                assert_no_host_navigation();
                ASSERT_EQ(pipeline.host.host.range.writes, writes);
                ASSERT_EQ(pipeline.host.host.selections, selection_count);
                ASSERT_EQ(pipeline.host.host.composition.ends, 0);
                ASSERT_EQ(TextServiceTestPeer::generation(pipeline.host.service), generation);
                ASSERT_EQ(TextServiceTestPeer::preedit(pipeline.host.service), preedit);
                const auto& page = TextServiceTestPeer::candidate_page(pipeline.host.service);
                ASSERT_EQ(page.page_index, page_before.page_index);
                ASSERT_EQ(page.highlighted, page_before.highlighted);
                ASSERT_EQ(page.items.size(), page_before.items.size());
                ASSERT_TRUE(TextServiceTestPeer::status(pipeline.host.service).chinese_mode());
            };
            // In a populated page, ordinary navigation has actions; Ctrl navigation is a no-op.
            for (bool ctrl : {false, true}) {
                if (!ctrl && !no_candidates) {
                    continue;
                }
                for (bool shift : {false, true}) {
                    if (ctrl) {
                        ASSERT_TRUE(!dispatch(VK_LCONTROL, false, false, true));
                        unchanged();
                    }
                    if (shift) {
                        ASSERT_TRUE(!dispatch(VK_LSHIFT, false, true, ctrl));
                        unchanged();
                    }
                    for (uint32_t key : {VK_PRIOR, VK_NEXT, VK_UP, VK_DOWN}) {
                        for (int repeat = 0; repeat < 3; ++repeat) {
                            ASSERT_TRUE(dispatch(key, false, shift, ctrl));
                            unchanged();
                        }
                        ASSERT_TRUE(!dispatch(key, true, shift, ctrl));
                        unchanged();
                    }
                    if (shift) {
                        ASSERT_TRUE(!dispatch(VK_LSHIFT, true, false, ctrl));
                        unchanged();
                    }
                    if (ctrl) {
                        ASSERT_TRUE(!dispatch(VK_LCONTROL, true));
                        unchanged();
                    }
                }
            }
        }
        ASSERT_TRUE(dispatch(VK_ESCAPE));
        assert_no_host_navigation();
        ASSERT_TRUE(TextServiceTestPeer::empty(pipeline.host.service));
        assert_idle_page_moves();
        ASSERT_TRUE(dispatch('N'));
        ASSERT_TRUE(dispatch('I'));
        ASSERT_TRUE(dispatch(VK_RETURN));
        ASSERT_TRUE(TextServiceTestPeer::empty(pipeline.host.service));
        assert_idle_page_moves();
    }
}
