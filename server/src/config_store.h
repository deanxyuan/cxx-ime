// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_CONFIG_STORE_H_
#define CXXIME_SERVER_CONFIG_STORE_H_

#include <memory>
#include <string>
#include <vector>

#include <cxxime/config.h>
#include <cxxime/control_protocol.h>

struct ConfigMutation {
    cxxime::UserConfigMutationKind kind = cxxime::UserConfigMutationKind::kMergePatch;
    std::string payload;
};

class ConfigStore {
public:
    bool initialize(const std::string& base_config_path, const std::string& user_config_path,
                    const std::string& themes_path, std::shared_ptr<const cxxime::Config>* config,
                    unsigned long* error_code = nullptr);

    bool apply(const std::vector<ConfigMutation>& mutations,
               std::shared_ptr<const cxxime::Config>* config, unsigned long* error_code);

private:
    bool build_effective_config(const std::string& user_config_json,
                                std::shared_ptr<const cxxime::Config>* config,
                                unsigned long* error_code) const;

    std::string base_config_path_;
    std::string user_config_path_;
    std::string themes_path_;
    std::string user_config_json_;
};

#endif // CXXIME_SERVER_CONFIG_STORE_H_
