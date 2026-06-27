// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "server_app.h"
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include <cstring>
#include <algorithm>

namespace {

std::string request_text_field(const char* field, size_t size) {
    return std::string(field, strnlen(field, size));
}

void response_copy_field(char* dst, size_t dst_size, const std::string& src) {
    if (!dst || dst_size == 0)
        return;
    strncpy_s(dst, dst_size, src.c_str(), _TRUNCATE);
}

cxxime::UserDictKind request_user_dict_kind(const cxxime::IPCRequest& request) {
    return request.modifiers == static_cast<uint32_t>(cxxime::UserDictKind::WUBI)
           ? cxxime::UserDictKind::WUBI
           : cxxime::UserDictKind::PINYIN;
}

} // namespace

bool ServerApp::initialize(const std::string& dict_path, const std::string& config_path) {
    std::string resolved_dict = dict_path.empty() ? cxxime::data_path("pinyin.dict.bin") : dict_path;
    std::string cfg = config_path.empty() ? cxxime::data_path("default.json") : config_path;
    config_path_ = cfg;

    CXXIME_LOG(L"Dictionary path: %S", resolved_dict.c_str());
    CXXIME_LOG(L"Config path: %S", config_path_.c_str());

    if (!session_mgr_.initialize(resolved_dict, config_path_)) {
        std::wstring msg = L"Failed to initialize session manager.\n\n";
        msg += L"Dict: ";
        msg += std::wstring(resolved_dict.begin(), resolved_dict.end());
        msg += L"\nConfig: ";
        msg += std::wstring(config_path_.begin(), config_path_.end());
        msg += L"\nData dir: ";
        std::string dd = cxxime::data_dir();
        msg += std::wstring(dd.begin(), dd.end());
        msg += L"\nUser data dir: ";
        std::string udd = cxxime::user_data_dir();
        msg += std::wstring(udd.begin(), udd.end());
        MessageBoxW(nullptr, msg.c_str(), L"CxxIME Server", MB_OK | MB_ICONERROR);
        return false;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CxxIMEServerClass";
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, L"CxxIMEServerClass", L"CxxIME Server", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), this);
    if (!hwnd_)
        return false;

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ipc_server_.set_handler([this](const cxxime::IPCRequest& req) { return handle_request(req); });

    if (!ipc_server_.start(cxxime::IPC_PIPE_BASE_NAME)) {
        MessageBoxW(nullptr, L"Failed to start IPC server.", L"CxxIME Server", MB_OK | MB_ICONERROR);
        return false;
    }

    // Start config change watcher — reload config directly on change
    config_monitor_.initialize();
    config_monitor_.start([this]() {
        session_mgr_.reload_config();
    });

    return true;
}

void ServerApp::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void ServerApp::finalize() {
    config_monitor_.stop();
    ipc_server_.stop();

    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

cxxime::IPCResponse ServerApp::handle_request(const cxxime::IPCRequest& request) {
    CXXIME_LOG(L"handle_request: cmd=%u, session=%u",
               (uint32_t)request.command, request.session_id);

    cxxime::IPCResponse response = {};
    memset(&response, 0, sizeof(response));
    response.status = cxxime::IPCStatus::OK;

    switch (request.command) {
    case cxxime::IPCCommand::PING:
        response.status = cxxime::IPCStatus::OK;
        break;

    case cxxime::IPCCommand::START_SESSION: {
        uint32_t id = session_mgr_.create_session();
        if (id == 0) {
            response.status = cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
            response.highlighted = 0;
        } else {
            response.highlighted = id;
        }
        CXXIME_LOG(L"START_SESSION: new session=%u", id);
        break;
    }

    case cxxime::IPCCommand::END_SESSION:
        session_mgr_.destroy_session(request.session_id);
        CXXIME_LOG(L"END_SESSION: session=%u", request.session_id);
        break;

    case cxxime::IPCCommand::PROCESS_KEY: {
        cxxime::KeyEvent event;
        event.keycode = request.key_code;
        event.modifiers = request.modifiers;
        event.is_key_up = request.is_key_up;

        auto r = session_mgr_.process_key(request.session_id, event);

        if (r.status != cxxime::IPCStatus::OK) {
            response.status = r.status;
            break;
        }

        response.status = cxxime::IPCStatus::OK;
        response.ascii_mode = !r.ime_status.chinese_mode;
        response.composing = r.composing;
        response.ime_status = r.ime_status;

        if (r.result == cxxime::ProcessResult::REJECTED) {
            // If the engine rejected the key but cleared the composition
            // (e.g. CapsLock with "clear" style), return OK so the TSF
            // client can clean up its composition state.
            if (!r.composing) {
                response.status = cxxime::IPCStatus::OK;
            } else {
                response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
            }
            break;
        }

        if (r.composing) {
            strncpy_s(response.preedit, r.preedit.c_str(), sizeof(response.preedit) - 1);
            response.candidate_count = (uint32_t)r.candidates.candidates.size();
            for (uint32_t i = 0; i < response.candidate_count && i < 10; ++i) {
                strncpy_s(response.candidates[i], r.candidates.candidates[i].text.c_str(),
                          sizeof(response.candidates[i]) - 1);
            }
            response.highlighted = (uint32_t)r.candidates.highlighted;
            response.page_current = (uint32_t)(r.candidates.page_index + 1);
            int ps = r.candidates.page_size > 0 ? r.candidates.page_size : 9;
            response.page_total = (r.candidates.total_count > 0)
                ? (uint32_t)((r.candidates.total_count + ps - 1) / ps)
                : 1;
        }

        if (r.result == cxxime::ProcessResult::COMMITTED) {
            strncpy_s(response.commit_text, r.commit_text.c_str(), sizeof(response.commit_text) - 1);
        }
        break;
    }

    case cxxime::IPCCommand::SELECT_CANDIDATE: {
        auto r = session_mgr_.select_candidate(request.session_id, request.candidate_index);
        if (r.status != cxxime::IPCStatus::OK) {
            response.status = r.status;
            break;
        }
        response.status = cxxime::IPCStatus::OK;
        response.ime_status = r.ime_status;
        if (r.result == cxxime::ProcessResult::COMMITTED) {
            strncpy_s(response.commit_text, r.commit_text.c_str(), sizeof(response.commit_text) - 1);
        } else {
            response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
        }
        break;
    }

    case cxxime::IPCCommand::COMMIT_COMPOSITION: {
        auto r = session_mgr_.commit_composition(request.session_id);
        if (r.status != cxxime::IPCStatus::OK) {
            response.status = r.status;
            break;
        }
        response.status = cxxime::IPCStatus::OK;
        response.ime_status = r.ime_status;
        if (r.result == cxxime::ProcessResult::COMMITTED) {
            strncpy_s(response.commit_text, r.commit_text.c_str(), sizeof(response.commit_text) - 1);
        }
        break;
    }

    case cxxime::IPCCommand::CLEAR_COMPOSITION:
        response.status = session_mgr_.clear_composition(request.session_id);
        break;

    case cxxime::IPCCommand::FOCUS_IN:
        session_mgr_.touch_session(request.session_id);
        break;

    case cxxime::IPCCommand::FOCUS_OUT:
        session_mgr_.focus_out(request.session_id);
        break;

    case cxxime::IPCCommand::TOGGLE_CHINESE: {
        auto [status, ime_status] = session_mgr_.toggle_chinese(request.session_id);
        if (status != cxxime::IPCStatus::OK) {
            response.status = status;
            break;
        }
        response.ime_status = ime_status;
        response.ascii_mode = !ime_status.chinese_mode;
        break;
    }

    case cxxime::IPCCommand::TOGGLE_SHAPE: {
        auto [status, ime_status] = session_mgr_.toggle_shape(request.session_id);
        if (status != cxxime::IPCStatus::OK) {
            response.status = status;
            break;
        }
        response.ime_status = ime_status;
        break;
    }

    case cxxime::IPCCommand::TOGGLE_PUNCT: {
        auto [status, ime_status] = session_mgr_.toggle_punct(request.session_id);
        if (status != cxxime::IPCStatus::OK) {
            response.status = status;
            break;
        }
        response.ime_status = ime_status;
        break;
    }

    case cxxime::IPCCommand::SWITCH_INPUT_MODE: {
        // Explicit calls carry target mode in candidate_index. Legacy toggle
        // calls leave modifiers clear and keep the old cycle behavior.
        std::pair<cxxime::IPCStatus, cxxime::ImeStatus> result;
        if ((request.modifiers & cxxime::IPC_SWITCH_INPUT_MODE_EXPLICIT) != 0) {
            auto mode = static_cast<cxxime::InputMode>(request.candidate_index);
            result = session_mgr_.switch_input_mode(request.session_id, mode);
        } else {
            result = session_mgr_.switch_input_mode(request.session_id);
        }
        auto [status, ime_status] = result;
        if (status != cxxime::IPCStatus::OK) {
            response.status = status;
            break;
        }
        response.ime_status = ime_status;
        break;
    }

    case cxxime::IPCCommand::GET_STATUS: {
        auto [status, ime_status] = session_mgr_.get_ime_status(request.session_id);
        if (status != cxxime::IPCStatus::OK) {
            response.status = status;
            break;
        }
        response.status = cxxime::IPCStatus::OK;
        response.ascii_mode = !ime_status.chinese_mode;
        response.ime_status = ime_status;
        break;
    }

    case cxxime::IPCCommand::SYNC_CAPS_LOCK: {
        auto [status, ime_status] =
            session_mgr_.sync_caps_lock(request.session_id, (request.modifiers & 0x08) != 0);
        if (status != cxxime::IPCStatus::OK) {
            response.status = status;
            break;
        }
        response.status = cxxime::IPCStatus::OK;
        response.ascii_mode = !ime_status.chinese_mode;
        response.ime_status = ime_status;
        break;
    }

    case cxxime::IPCCommand::RELOAD_CONFIG:
        session_mgr_.reload_config();
        break;

    case cxxime::IPCCommand::RELOAD_DICTIONARIES:
        response.status = session_mgr_.reload_dictionaries();
        break;

    case cxxime::IPCCommand::ADD_USER_ENTRY: {
        std::string text = request_text_field(request.text, sizeof(request.text));
        std::string code = request_text_field(request.code, sizeof(request.code));
        response.status = session_mgr_.add_user_entry(
            request.session_id, request_user_dict_kind(request), text, code);
        break;
    }

    case cxxime::IPCCommand::QUERY_USER_ENTRIES: {
        std::string query = request_text_field(request.text, sizeof(request.text));
        size_t total = 0;
        auto entries = session_mgr_.query_user_entries(
            query, request_user_dict_kind(request), 32, total);
        response.status = cxxime::IPCStatus::OK;
        response.user_entry_total = static_cast<uint32_t>(
            (std::min)(total, static_cast<size_t>(UINT32_MAX)));
        response.user_entry_count = static_cast<uint32_t>(entries.size());
        for (size_t i = 0; i < entries.size() && i < 32; ++i) {
            response_copy_field(response.user_entries[i].text,
                sizeof(response.user_entries[i].text), entries[i].text);
            response_copy_field(response.user_entries[i].code,
                sizeof(response.user_entries[i].code), entries[i].code);
            response.user_entries[i].frequency = entries[i].frequency;
        }
        break;
    }

    case cxxime::IPCCommand::DELETE_USER_ENTRY: {
        std::string text = request_text_field(request.text, sizeof(request.text));
        std::string code = request_text_field(request.code, sizeof(request.code));
        response.status = session_mgr_.delete_user_entry(request_user_dict_kind(request),
                                                         text, code);
        break;
    }

    case cxxime::IPCCommand::REPLACE_USER_ENTRY: {
        std::string old_text = request_text_field(request.old_text, sizeof(request.old_text));
        std::string old_code = request_text_field(request.old_code, sizeof(request.old_code));
        std::string text = request_text_field(request.text, sizeof(request.text));
        std::string code = request_text_field(request.code, sizeof(request.code));
        response.status = session_mgr_.replace_user_entry(request_user_dict_kind(request),
                                                          old_text, old_code, text, code);
        break;
    }

    case cxxime::IPCCommand::RELOAD_USER_DICT:
        response.status = session_mgr_.reload_user_dict(request_user_dict_kind(request));
        break;

    case cxxime::IPCCommand::SAVE_USER_DICT:
        response.status = session_mgr_.save_user_dict(request_user_dict_kind(request));
        break;

    default:
        response.status = cxxime::IPCStatus::ERR_UNKNOWN_COMMAND;
        break;
    }

    return response;
}

LRESULT CALLBACK ServerApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        ServerApp* app = reinterpret_cast<ServerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (app) {
            app->hwnd_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
