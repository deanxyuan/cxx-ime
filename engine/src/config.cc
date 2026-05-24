// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

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
            if (engine.contains("page_size") && engine["page_size"].is_number()) {
                page_size = engine["page_size"].get<int>();
                if (page_size < 1) page_size = 1;
                if (page_size > 100) page_size = 100;
            }
        }

        if (j.contains("style") && j["style"].is_object()) {
            auto& style = j["style"];
            if (style.contains("font_face") && style["font_face"].is_string())
                font_name = style["font_face"].get<std::string>();
            if (style.contains("font_point") && style["font_point"].is_number()) {
                font_size = style["font_point"].get<int>();
                if (font_size < 8) font_size = 8;
                if (font_size > 72) font_size = 72;
            }
            if (style.contains("layout") && style["layout"].is_string())
                layout = style["layout"].get<std::string>();
            if (style.contains("inline_preedit") && style["inline_preedit"].is_boolean())
                inline_preedit = style["inline_preedit"].get<bool>();
            if (style.contains("preedit_type") && style["preedit_type"].is_string()) {
                preedit_type = style["preedit_type"].get<std::string>();
                if (preedit_type != "composition" && preedit_type != "preview" &&
                    preedit_type != "preview_all")
                    preedit_type = "composition";
            }
        }

        if (j.contains("theme") && j["theme"].is_string())
            theme = j["theme"].get<std::string>();

        if (j.contains("ascii_composer") && j["ascii_composer"].is_object()) {
            auto& ac = j["ascii_composer"];
            if (ac.contains("good_old_caps_lock") && ac["good_old_caps_lock"].is_boolean())
                good_old_caps_lock = ac["good_old_caps_lock"].get<bool>();
            if (ac.contains("switch_key") && ac["switch_key"].is_object()) {
                for (auto& [key, val] : ac["switch_key"].items()) {
                    if (val.is_string())
                        ascii_switch_key[key] = val.get<std::string>();
                }
            }
        }

    } catch (const nlohmann::json::exception&) {
        return false;
    }

    return true;
}

} // namespace cxxime
