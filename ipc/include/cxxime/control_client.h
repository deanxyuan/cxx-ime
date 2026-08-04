// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONTROL_CLIENT_H_
#define CXXIME_CONTROL_CLIENT_H_

#include <functional>
#include <memory>
#include <string>

#include <cxxime/control_protocol.h>

namespace cxxime {

class ControlClient {
public:
    using SnapshotHandler =
        std::function<void(ConfigGeneration generation, const std::string& config_json)>;

    ControlClient();
    ~ControlClient();

    ControlClient(const ControlClient&) = delete;
    ControlClient& operator=(const ControlClient&) = delete;

    bool start(SnapshotHandler handler, const std::wstring& pipe_name = L"");
    void stop();
    void patch_user_config(const std::string& merge_patch_json);
    bool is_running() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

bool replace_user_config(const std::string& config_json, ConfigGeneration* generation = nullptr,
                         unsigned long* error_code = nullptr, int timeout_ms = 1500,
                         const std::wstring& pipe_name = L"");

bool send_control_request(ControlMessageType request_type, const std::string& request_payload,
                          ControlMessageType response_type, ControlMessage* response,
                          unsigned long* error_code = nullptr, int timeout_ms = 1500,
                          const std::wstring& pipe_name = L"");

} // namespace cxxime

#endif // CXXIME_CONTROL_CLIENT_H_
