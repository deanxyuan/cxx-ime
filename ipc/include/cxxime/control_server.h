// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONTROL_SERVER_H_
#define CXXIME_CONTROL_SERVER_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <cxxime/control_protocol.h>

namespace cxxime {

class ControlServer {
public:
    using MutationHandler =
        std::function<bool(UserConfigMutationKind kind, const std::string& payload,
                           std::string* config_json, unsigned long* error_code)>;

    ControlServer();
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(const std::string& initial_config_json, MutationHandler mutation_handler,
               const std::wstring& pipe_name = L"");
    void stop();

    bool publish_snapshot(const std::string& config_json, ConfigGeneration* generation = nullptr);
    ConfigGeneration generation() const;
    std::size_t subscriber_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cxxime

#endif // CXXIME_CONTROL_SERVER_H_
