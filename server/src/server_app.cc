// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "server_app.h"
#include <cxxime/logging.h>
#include <cstring>

#define WM_TRAYICON (WM_USER + 1)

bool ServerApp::initialize(const std::string& dict_path, const std::string& config_path) {
    // Resolve exe directory for auto-detection
    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string path = exe_path;
    auto pos = path.find_last_of("\\/");
    std::string dir = (pos != std::string::npos) ? path.substr(0, pos + 1) : "";

    // Resolve dictionary path: explicit arg > exe dir > ../data/
    std::string resolved_dict = dict_path;
    if (resolved_dict.empty()) {
        std::string candidate1 = dir + "pinyin.dict.db";
        std::string candidate2 = dir + "..\\data\\pinyin.dict.db";
        DWORD attr1 = GetFileAttributesA(candidate1.c_str());
        DWORD attr2 = GetFileAttributesA(candidate2.c_str());
        if (attr1 != INVALID_FILE_ATTRIBUTES)
            resolved_dict = candidate1;
        else if (attr2 != INVALID_FILE_ATTRIBUTES)
            resolved_dict = candidate2;
        else
            resolved_dict = candidate1;
    }

    // Resolve config path: explicit arg > exe dir > ../data/
    std::string cfg = config_path;
    if (cfg.empty()) {
        std::string candidate1 = dir + "default.json";
        std::string candidate2 = dir + "..\\data\\default.json";
        DWORD attr1 = GetFileAttributesA(candidate1.c_str());
        DWORD attr2 = GetFileAttributesA(candidate2.c_str());
        if (attr1 != INVALID_FILE_ATTRIBUTES)
            cfg = candidate1;
        else if (attr2 != INVALID_FILE_ATTRIBUTES)
            cfg = candidate2;
    }
    config_path_ = cfg;

    CXXIME_LOG(L"Dictionary path: %S", resolved_dict.c_str());
    CXXIME_LOG(L"Config path: %S", config_path_.c_str());

    // Pre-load shared resources (Dict, SpellingsIndex, Config) once at startup.
    // Session creation will reference these instead of opening files again.
    if (!session_mgr_.initialize(resolved_dict, config_path_)) {
        MessageBoxW(nullptr, L"Failed to initialize session manager.", L"CxxIME Server",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    // Create hidden window for tray icon messages
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

    // Create tray icon
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid_.szTip, L"CxxIME Server");
    Shell_NotifyIconW(NIM_ADD, &nid_);

    // Start IPC server
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
        Shell_NotifyIconW(NIM_DELETE, &nid_);
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

        auto result = engine->process_key(event);

        response.ascii_mode = engine->ascii_composer().is_ascii_mode();
        response.composing = engine->context().is_composing();

        if (result == cxxime::ProcessResult::COMMITTED) {
            std::string commit = engine->get_commit_text();
            strncpy_s(response.commit_text, commit.c_str(), sizeof(response.commit_text) - 1);
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
    ServerApp* app = reinterpret_cast<ServerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_TRAYICON) {
        if (lp == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, L"Settings...");
            AppendMenuW(hMenu, MF_STRING, 2, L"View Log");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 3, L"About CxxIME");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 4, L"Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
            if (cmd == 1) {
                if (app && !app->config_path_.empty()) {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, app->config_path_.c_str(), -1, nullptr, 0);
                    std::wstring wpath(wlen - 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, app->config_path_.c_str(), -1, &wpath[0], wlen);
                    ShellExecuteW(hwnd, L"open", wpath.c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
                } else {
                    MessageBoxW(hwnd, L"Config file not found.", L"CxxIME", MB_OK | MB_ICONWARNING);
                }
            } else if (cmd == 2) {
                MessageBoxW(hwnd,
                    L"Use DebugView (Sysinternals) to view logs.\n"
                    L"Download: https://learn.microsoft.com/en-us/sysinternals/downloads/debugview\n\n"
                    L"Filter: [CxxIME]",
                    L"View Log", MB_OK | MB_ICONINFORMATION);
            } else if (cmd == 3) {
                MessageBoxW(hwnd, L"CxxIME - Lightweight Pinyin Input Method\nVersion 0.1.0", L"About", MB_OK);
            } else if (cmd == 4) {
                PostQuitMessage(0);
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
