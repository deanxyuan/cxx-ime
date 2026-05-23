// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cxxime/config.h>
#include <json.hpp>
#include <fstream>

namespace cxxime {

bool Config::load(const std::string& path) {
    if (path.empty())
        return true;

    std::ifstream file(path);
    if (!file.is_open())
        return false;

    try {
        nlohmann::json j = nlohmann::json::parse(file);

        if (j.contains("engine") && j["engine"].is_object()) {
            auto& engine = j["engine"];
            if (engine.contains("page_size") && engine["page_size"].is_number())
                page_size = engine["page_size"].get<int>();
        }

        if (j.contains("style") && j["style"].is_object()) {
            auto& style = j["style"];
            if (style.contains("font_face") && style["font_face"].is_string())
                font_name = style["font_face"].get<std::string>();
            if (style.contains("font_point") && style["font_point"].is_number())
                font_size = style["font_point"].get<int>();
            if (style.contains("layout") && style["layout"].is_string())
                layout = style["layout"].get<std::string>();
        }

        if (j.contains("theme") && j["theme"].is_string())
            theme = j["theme"].get<std::string>();

    } catch (const nlohmann::json::exception&) {
        return false;
    }

    return true;
}

} // namespace cxxime
