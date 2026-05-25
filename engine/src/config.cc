// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/config.h>
#include <json.hpp>
#include <fstream>

namespace cxxime {

static void load_int(nlohmann::json& obj, const char* key, int& val) {
    if (obj.contains(key) && obj[key].is_number()) val = obj[key].get<int>();
}

static void load_string(nlohmann::json& obj, const char* key, std::string& val) {
    if (obj.contains(key) && obj[key].is_string()) val = obj[key].get<std::string>();
}

static void load_bool(nlohmann::json& obj, const char* key, bool& val) {
    if (obj.contains(key) && obj[key].is_boolean()) val = obj[key].get<bool>();
}

bool Config::load(const std::string& path) {
    if (path.empty()) return true;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j = nlohmann::json::parse(file);

        if (j.contains("engine") && j["engine"].is_object()) {
            auto& e = j["engine"];
            load_int(e, "page_size", page_size);
            if (page_size < 1) page_size = 1;
            if (page_size > 100) page_size = 100;
        }

        if (j.contains("style") && j["style"].is_object()) {
            auto& s = j["style"];
            load_string(s, "font_face", font_name);
            load_int(s, "font_point", font_size);
            if (font_size < 8) font_size = 8;
            if (font_size > 72) font_size = 72;
            load_string(s, "layout", layout);
            load_bool(s, "inline_preedit", inline_preedit);
            load_string(s, "render_backend", render_backend);
            load_string(s, "preedit_type", preedit_type);
            if (preedit_type != "composition" && preedit_type != "preview" && preedit_type != "preview_all")
                preedit_type = "composition";
        }

        if (j.contains("layout") && j["layout"].is_object()) {
            auto& l = j["layout"];
            load_int(l, "min_width", layout_config.min_width);
            load_int(l, "max_width", layout_config.max_width);
            load_int(l, "max_height", layout_config.max_height);
            load_int(l, "margin_x", layout_config.margin_x);
            load_int(l, "margin_y", layout_config.margin_y);
            load_int(l, "spacing", layout_config.spacing);
            load_int(l, "candidate_spacing", layout_config.candidate_spacing);
            load_int(l, "hilite_spacing", layout_config.hilite_spacing);
            load_int(l, "hilite_padding_x", layout_config.hilite_padding_x);
            load_int(l, "hilite_padding_y", layout_config.hilite_padding_y);
            load_int(l, "round_corner", layout_config.round_corner);
            load_int(l, "round_corner_ex", layout_config.round_corner_ex);
            load_int(l, "label_font_point", layout_config.label_font_point);
            load_int(l, "border_width", layout_config.border_width);
            load_string(l, "align_type", layout_config.align_type);
        }

        load_string(j, "theme", theme);

        if (j.contains("ascii_composer") && j["ascii_composer"].is_object()) {
            auto& ac = j["ascii_composer"];
            load_bool(ac, "good_old_caps_lock", good_old_caps_lock);
            if (ac.contains("switch_key") && ac["switch_key"].is_object()) {
                for (auto& [key, val] : ac["switch_key"].items()) {
                    if (val.is_string()) ascii_switch_key[key] = val.get<std::string>();
                }
            }
        }

    } catch (const nlohmann::json::exception&) {
        return false;
    }

    return true;
}

bool Config::load_themes(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        if (j.contains("preset_color_schemes") && j["preset_color_schemes"].is_object()) {
            for (auto& [name, sc] : j["preset_color_schemes"].items()) {
                if (!sc.is_object()) continue;
                SchemeColors c;
                load_int(sc, "back_color", c.back_color);
                load_int(sc, "border_color", c.border_color);
                load_int(sc, "text_color", c.text_color);
                load_int(sc, "candidate_text_color", c.candidate_text_color);
                load_int(sc, "label_text_color", c.label_text_color);
                load_int(sc, "hilited_text_color", c.hilited_text_color);
                load_int(sc, "hilited_back_color", c.hilited_back_color);
                load_int(sc, "hilited_candidate_text_color", c.hilited_candidate_text_color);
                load_int(sc, "hilited_candidate_back_color", c.hilited_candidate_back_color);
                load_int(sc, "comment_text_color", c.comment_text_color);
                load_int(sc, "prevpage_color", c.prevpage_color);
                load_int(sc, "nextpage_color", c.nextpage_color);
                // Weasel-style fallback chain (resolved at load time, not render time)
                if (c.text_color == -1) c.text_color = 0xff000000;  // black
                if (c.back_color == -1) c.back_color = 0xffffffff;  // white
                if (c.candidate_text_color == -1) c.candidate_text_color = c.text_color;
                if (c.border_color == -1) c.border_color = c.text_color;
                if (c.hilited_text_color == -1) c.hilited_text_color = c.text_color;
                if (c.hilited_back_color == -1) c.hilited_back_color = c.back_color;
                if (c.hilited_candidate_text_color == -1) c.hilited_candidate_text_color = c.hilited_text_color;
                if (c.hilited_candidate_back_color == -1) c.hilited_candidate_back_color = c.hilited_back_color;
                if (c.label_text_color == -1) c.label_text_color = c.text_color;
                if (c.comment_text_color == -1) c.comment_text_color = c.label_text_color;
                if (c.prevpage_color == -1) c.prevpage_color = c.text_color;
                if (c.nextpage_color == -1) c.nextpage_color = c.text_color;
                preset_color_schemes[name] = c;
            }
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return true;
}

} // namespace cxxime
