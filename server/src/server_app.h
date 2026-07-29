// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_APP_H_
#define CXXIME_SERVER_APP_H_

#include <string>

#include <windows.h>

#include <cxxime/control_server.h>
#include <cxxime/dictionary_monitor.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/ipc_server.h>

#include "config_store.h"
#include "config_write_coordinator.h"
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
    ConfigStore config_store_;
    ConfigWriteCoordinator config_writer_;
    cxxime::IpcServer ipc_server_;
    cxxime::ControlServer control_server_;
    cxxime::DictionaryMonitor dictionary_monitor_;
    HWND hwnd_ = nullptr;
    std::string config_path_;
};

#endif // CXXIME_SERVER_APP_H_
