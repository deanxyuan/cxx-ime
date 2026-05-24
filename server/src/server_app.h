// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_APP_H_
#define CXXIME_SERVER_APP_H_

#include <windows.h>
#include <shellapi.h>
#include <cxxime/ipc_server.h>
#include <cxxime/ipc_protocol.h>
#include "session_manager.h"

class ServerApp {
public:
    bool initialize(const std::string& dict_path = "", const std::string& config_path = "");
    void run();
    void finalize();

private:
    cxxime::IPCResponse handle_request(const cxxime::IPCRequest& request);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    SessionManager session_mgr_;
    cxxime::IpcServer ipc_server_;
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    std::string config_path_;  // Kept for tray "Settings..." menu
};

#endif // CXXIME_SERVER_APP_H_
