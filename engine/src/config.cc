// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/config.h>

#include <fstream>

#include <json.hpp>

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

static void apply_config_json(Config& config, nlohmann::json& j) {
    if (j.contains("engine") && j["engine"].is_object()) {
        auto& e = j["engine"];
        load_int(e, "page_size", config.page_size);
        if (config.page_size < 1) config.page_size = 1;
        if (config.page_size > 100) config.page_size = 100;
        load_int(e, "input_mode", config.input_mode);
        if (config.input_mode < 0) config.input_mode = 0;
        if (config.input_mode > 2) config.input_mode = 2;
        load_bool(e, "fuzzy_pinyin", config.fuzzy_pinyin);
        load_bool(e, "wubi_auto_commit", config.wubi_auto_commit);
        load_bool(e, "wubi_code_hint", config.wubi_code_hint);
        load_bool(e, "candidate_learning", config.candidate_learning);
    }

    if (j.contains("initial_state") && j["initial_state"].is_object()) {
        auto& initial = j["initial_state"];
        load_bool(initial, "full_shape", config.initial_full_shape);
        load_bool(initial, "chinese_punct", config.initial_chinese_punct);
    }

    if (j.contains("style") && j["style"].is_object()) {
        auto& s = j["style"];
        load_string(s, "font_face", config.font_name);
        load_int(s, "font_point", config.font_size);
        if (config.font_size < 8) config.font_size = 8;
        if (config.font_size > 72) config.font_size = 72;
        load_string(s, "layout", config.layout);
        load_bool(s, "inline_preedit", config.inline_preedit);
        load_string(s, "render_backend", config.render_backend);
        load_string(s, "preedit_type", config.preedit_type);
        if (config.preedit_type != "composition" && config.preedit_type != "preview")
            config.preedit_type = "composition";
    }

    if (j.contains("layout") && j["layout"].is_object()) {
        auto& l = j["layout"];
        load_int(l, "min_width", config.layout_config.min_width);
        load_int(l, "max_width", config.layout_config.max_width);
        load_int(l, "max_height", config.layout_config.max_height);
        load_int(l, "margin_x", config.layout_config.margin_x);
        load_int(l, "margin_y", config.layout_config.margin_y);
        load_int(l, "spacing", config.layout_config.spacing);
        load_int(l, "candidate_spacing", config.layout_config.candidate_spacing);
        load_int(l, "hilite_spacing", config.layout_config.hilite_spacing);
        load_int(l, "hilite_padding_x", config.layout_config.hilite_padding_x);
        load_int(l, "hilite_padding_y", config.layout_config.hilite_padding_y);
        load_int(l, "round_corner", config.layout_config.round_corner);
        load_int(l, "round_corner_ex", config.layout_config.round_corner_ex);
        load_int(l, "label_font_point", config.layout_config.label_font_point);
        load_int(l, "border_width", config.layout_config.border_width);
    }

    load_string(j, "theme", config.theme);

    if (j.contains("status_window") && j["status_window"].is_object()) {
        auto& sw = j["status_window"];
        load_bool(sw, "enable", config.status_window.enable);
        load_int(sw, "x", config.status_window.x);
        load_int(sw, "y", config.status_window.y);
        load_bool(sw, "show_on_startup", config.status_window.show_on_startup);
    }

    if (j.contains("ascii_composer") && j["ascii_composer"].is_object()) {
        auto& ac = j["ascii_composer"];
        if (ac.contains("switch_key") && ac["switch_key"].is_object()) {
            for (auto& [key, val] : ac["switch_key"].items()) {
                if (val.is_string()) config.ascii_switch_key[key] = val.get<std::string>();
            }
        }
    }
}

static bool apply_color_schemes(Config& config, nlohmann::json& schemes) {
    if (!schemes.is_object()) return false;

    for (auto& [name, sc] : schemes.items()) {
        if (!sc.is_object()) return false;
        Config::SchemeColors c;
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
        if (c.text_color == -1) c.text_color = 0xff000000; // black
        if (c.back_color == -1) c.back_color = 0xffffffff; // white
        if (c.candidate_text_color == -1) c.candidate_text_color = c.text_color;
        if (c.border_color == -1) c.border_color = c.text_color;
        if (c.hilited_text_color == -1) c.hilited_text_color = c.text_color;
        if (c.hilited_back_color == -1) c.hilited_back_color = c.back_color;
        if (c.hilited_candidate_text_color == -1)
            c.hilited_candidate_text_color = c.hilited_text_color;
        if (c.hilited_candidate_back_color == -1)
            c.hilited_candidate_back_color = c.hilited_back_color;
        if (c.label_text_color == -1) c.label_text_color = c.text_color;
        if (c.comment_text_color == -1) c.comment_text_color = c.label_text_color;
        if (c.prevpage_color == -1) c.prevpage_color = c.text_color;
        if (c.nextpage_color == -1) c.nextpage_color = c.text_color;
        config.preset_color_schemes[name] = c;
    }
    return true;
}

bool Config::load(const std::string& path) {
    if (path.empty()) return true;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        apply_config_json(*this, j);
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    cxxime::load_diagnostics_config(path, &diagnostics);
    return true;
}

bool Config::load_json(const std::string& json_text) {
    if (json_text.empty()) {
        return false;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(json_text);
        if (!j.is_object()) {
            return false;
        }
        apply_config_json(*this, j);
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    cxxime::load_diagnostics_config_json(json_text, &diagnostics);
    return true;
}

bool Config::load_runtime_json(const std::string& json_text) {
    if (json_text.empty()) {
        return false;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(json_text);
        if (!j.is_object()) {
            return false;
        }
        apply_config_json(*this, j);
        if (!j.contains("resolved_color_schemes") ||
            !apply_color_schemes(*this, j["resolved_color_schemes"]) ||
            preset_color_schemes.find(theme) == preset_color_schemes.end()) {
            return false;
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    cxxime::load_diagnostics_config_json(json_text, &diagnostics);
    return true;
}

bool Config::load_themes(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        if (!j.contains("preset_color_schemes") ||
            !apply_color_schemes(*this, j["preset_color_schemes"]) ||
            preset_color_schemes.find(theme) == preset_color_schemes.end()) {
            return false;
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return true;
}

static nlohmann::json build_config_json(const Config& config) {
    nlohmann::json j;
    j["schema"]["name"] = "CxxIME";
    j["schema"]["version"] = "1.0";
    j["schema"]["description"] = "CxxIME default configuration";

    j["engine"]["page_size"] = config.page_size;
    j["engine"]["input_mode"] = config.input_mode;
    j["engine"]["fuzzy_pinyin"] = config.fuzzy_pinyin;
    j["engine"]["wubi_auto_commit"] = config.wubi_auto_commit;
    j["engine"]["wubi_code_hint"] = config.wubi_code_hint;
    j["engine"]["candidate_learning"] = config.candidate_learning;
    j["engine"]["max_pinyin_length"] = 64;

    j["initial_state"]["full_shape"] = config.initial_full_shape;
    j["initial_state"]["chinese_punct"] = config.initial_chinese_punct;

    j["style"]["font_face"] = config.font_name;
    j["style"]["font_point"] = config.font_size;
    j["style"]["layout"] = config.layout;
    j["style"]["render_backend"] = config.render_backend;
    j["style"]["inline_preedit"] = config.inline_preedit;
    j["style"]["preedit_type"] = config.preedit_type;

    j["layout"]["min_width"] = config.layout_config.min_width;
    j["layout"]["max_width"] = config.layout_config.max_width;
    j["layout"]["max_height"] = config.layout_config.max_height;
    j["layout"]["margin_x"] = config.layout_config.margin_x;
    j["layout"]["margin_y"] = config.layout_config.margin_y;
    j["layout"]["spacing"] = config.layout_config.spacing;
    j["layout"]["candidate_spacing"] = config.layout_config.candidate_spacing;
    j["layout"]["hilite_spacing"] = config.layout_config.hilite_spacing;
    j["layout"]["hilite_padding_x"] = config.layout_config.hilite_padding_x;
    j["layout"]["hilite_padding_y"] = config.layout_config.hilite_padding_y;
    j["layout"]["round_corner"] = config.layout_config.round_corner;
    j["layout"]["round_corner_ex"] = config.layout_config.round_corner_ex;
    j["layout"]["label_font_point"] = config.layout_config.label_font_point;
    j["layout"]["border_width"] = config.layout_config.border_width;

    j["theme"] = config.theme;

    j["status_window"]["enable"] = config.status_window.enable;
    j["status_window"]["x"] = config.status_window.x;
    j["status_window"]["y"] = config.status_window.y;
    j["status_window"]["show_on_startup"] = config.status_window.show_on_startup;

    j["diagnostics"]["trace_mode"] = diagnostic_trace_mode_name(config.diagnostics.trace_mode);
    j["diagnostics"]["log_max_size"] = config.diagnostics.log_max_size;
    j["diagnostics"]["log_max_files"] = config.diagnostics.log_max_files;
    j["diagnostics"]["normal_sample_rate"] = config.diagnostics.normal_sample_rate;
    j["diagnostics"]["truncated_sample_rate"] = config.diagnostics.truncated_sample_rate;
    j["diagnostics"]["slow_query_us"] = config.diagnostics.slow_query_us;
    j["diagnostics"]["cache_miss_slow_us"] = config.diagnostics.cache_miss_slow_us;
    j["diagnostics"]["slow_ipc_us"] = config.diagnostics.slow_ipc_us;
    j["diagnostics"]["slow_window_us"] = config.diagnostics.slow_window_us;
    j["diagnostics"]["slow_total_us"] = config.diagnostics.slow_total_us;

    nlohmann::json ac;
    nlohmann::json sk;
    for (const auto& [k, v] : config.ascii_switch_key)
        sk[k] = v;
    ac["switch_key"] = sk;
    j["ascii_composer"] = ac;

    return j;
}

std::string Config::to_json() const { return build_config_json(*this).dump(4); }

std::string Config::to_runtime_json() const {
    nlohmann::json j = build_config_json(*this);
    j["resolved_color_schemes"] = nlohmann::json::object();
    for (const auto& [name, colors] : preset_color_schemes) {
        auto& scheme = j["resolved_color_schemes"][name];
        scheme["text_color"] = colors.text_color;
        scheme["back_color"] = colors.back_color;
        scheme["border_color"] = colors.border_color;
        scheme["candidate_text_color"] = colors.candidate_text_color;
        scheme["label_text_color"] = colors.label_text_color;
        scheme["hilited_text_color"] = colors.hilited_text_color;
        scheme["hilited_back_color"] = colors.hilited_back_color;
        scheme["hilited_candidate_text_color"] = colors.hilited_candidate_text_color;
        scheme["hilited_candidate_back_color"] = colors.hilited_candidate_back_color;
        scheme["comment_text_color"] = colors.comment_text_color;
        scheme["prevpage_color"] = colors.prevpage_color;
        scheme["nextpage_color"] = colors.nextpage_color;
    }
    return j.dump(4);
}

} // namespace cxxime
