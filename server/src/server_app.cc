// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "server_app.h"
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include <cstring>

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
        auto* engine = session_mgr_.get_engine(request.session_id);
        if (!engine) {
            response.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
            break;
        }

        cxxime::KeyEvent event;
        event.keycode = request.key_code;
        event.modifiers = request.modifiers;
        event.is_key_up = request.is_key_up;

        CXXIME_LOG(L"PROCESS_KEY: vk=%u, is_key_up=%d, composing=%d",
                   request.key_code, request.is_key_up, engine->context().is_composing());

        auto result = engine->process_key(event);

        response.ascii_mode = engine->ascii_composer().is_ascii_mode();
        response.composing = engine->context().is_composing();

        CXXIME_LOG(L"PROCESS_KEY: result=%d, ascii_mode=%d, composing=%d, committed_text='%S'",
                   (int)result, response.ascii_mode, response.composing,
                   engine->context().committed_text.c_str());

        if (result == cxxime::ProcessResult::COMMITTED) {
            std::string commit = engine->get_commit_text();
            strncpy_s(response.commit_text, commit.c_str(), sizeof(response.commit_text) - 1);
            CXXIME_LOG(L"PROCESS_KEY: COMMITTED, commit_text='%S'", response.commit_text);
        } else if (result == cxxime::ProcessResult::ACCEPTED) {
            const auto& ctx = engine->context();
            strncpy_s(response.preedit, ctx.pinyin_buffer.c_str(), sizeof(response.preedit) - 1);
            response.candidate_count = (uint32_t)ctx.candidates.candidates.size();
            for (uint32_t i = 0; i < response.candidate_count && i < 10; ++i) {
                strncpy_s(response.candidates[i], ctx.candidates.candidates[i].text.c_str(),
                          sizeof(response.candidates[i]) - 1);
            }
            response.highlighted = (uint32_t)ctx.candidates.highlighted;
        } else {
            response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
        }
        break;
    }

    case cxxime::IPCCommand::SELECT_CANDIDATE: {
        auto* engine = session_mgr_.get_engine(request.session_id);
        if (!engine) {
            response.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
            break;
        }
        if (engine->select_candidate(request.candidate_index)) {
            std::string commit = engine->get_commit_text();
            strncpy_s(response.commit_text, commit.c_str(), sizeof(response.commit_text) - 1);
        } else {
            response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
        }
        break;
    }

    case cxxime::IPCCommand::COMMIT_COMPOSITION: {
        auto* engine = session_mgr_.get_engine(request.session_id);
        if (engine) {
            std::string commit = engine->get_commit_text();
            strncpy_s(response.commit_text, commit.c_str(), sizeof(response.commit_text) - 1);
            engine->clear();
        } else {
            response.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
        }
        break;
    }

    case cxxime::IPCCommand::CLEAR_COMPOSITION: {
        auto* engine = session_mgr_.get_engine(request.session_id);
        if (engine) {
            engine->clear();
        } else {
            response.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
        }
        break;
    }

    case cxxime::IPCCommand::FOCUS_IN:
        break;

    case cxxime::IPCCommand::FOCUS_OUT: {
        auto* engine = session_mgr_.get_engine(request.session_id);
        if (engine) {
            engine->clear();
        }
        break;
    }

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
