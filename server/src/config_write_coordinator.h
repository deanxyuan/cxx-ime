// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_CONFIG_WRITE_COORDINATOR_H_
#define CXXIME_SERVER_CONFIG_WRITE_COORDINATOR_H_

#include <functional>
#include <memory>
#include <string>

#include <cxxime/config.h>
#include <cxxime/control_protocol.h>

class ConfigStore;

class ConfigWriteCoordinator {
public:
    using ApplyHandler = std::function<void(const std::shared_ptr<const cxxime::Config>& config)>;

    ConfigWriteCoordinator();
    ~ConfigWriteCoordinator();

    ConfigWriteCoordinator(const ConfigWriteCoordinator&) = delete;
    ConfigWriteCoordinator& operator=(const ConfigWriteCoordinator&) = delete;

    bool start(ConfigStore* store, ApplyHandler apply_handler);
    void stop();

    bool submit(cxxime::UserConfigMutationKind kind, const std::string& payload,
                std::string* config_json, unsigned long* error_code);
    bool enqueue_patch(const std::string& merge_patch_json);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // CXXIME_SERVER_CONFIG_WRITE_COORDINATOR_H_
