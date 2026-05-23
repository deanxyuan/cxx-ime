// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONFIG_H_
#define CXXIME_CONFIG_H_

#include <string>

namespace cxxime {

class Config {
public:
    bool load(const std::string& path);

    std::string font_name = "Microsoft YaHei UI";
    int font_size = 14;
    int page_size = 9;
    std::string layout = "horizontal";
    std::string theme = "light";
    bool auto_commit = true;
    bool inline_preedit = true;
    std::string preedit_type = "composition";
};

} // namespace cxxime

#endif // CXXIME_CONFIG_H_
