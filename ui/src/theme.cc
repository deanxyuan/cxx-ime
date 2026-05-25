// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/render_context.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>
#include <string>

namespace cxxime {

static Color to_color(int v) {
    return {(uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), 255};
}

static const Config::SchemeColors* find_scheme(const Config& cfg, const std::string& name) {
    auto it = cfg.preset_color_schemes.find(name);
    return (it != cfg.preset_color_schemes.end()) ? &it->second : nullptr;
}

Theme build_theme_from_config(const Config& cfg) {
    const Config::SchemeColors* s = find_scheme(cfg, cfg.theme);
    if (!s) s = find_scheme(cfg, "aqua");

    Config fallback;
    if (!s && fallback.load_themes(data_path("themes.json"))) {
        s = find_scheme(fallback, cfg.theme);
        if (!s) s = find_scheme(fallback, "aqua");
    }

    if (!s) {
        // Hardcoded azure fallback
        Theme t;
        t.background   = {1, 78, 139, 255};
        t.text         = {232, 232, 255, 255};
        t.label_text   = {100, 150, 198, 255};
        t.hilited_text = {254, 255, 127, 255};
        t.hilited_back = {1, 94, 169, 255};
        t.border       = {1, 78, 139, 255};
        t.preedit_text = {248, 255, 255, 255};
        t.prev_page    = {100, 150, 198, 255};
        t.next_page    = {100, 150, 198, 255};
        return t;
    }

    Theme t;
    t.background   = to_color(s->back_color);
    t.border       = to_color(s->border_color);
    t.text         = to_color(s->candidate_text_color);
    t.label_text   = to_color(s->label_text_color);
    t.hilited_text = to_color(s->hilited_candidate_text_color);
    t.hilited_back = to_color(s->hilited_candidate_back_color);
    t.preedit_text = to_color(s->hilited_text_color);
    t.prev_page    = to_color(s->prevpage_color);
    t.next_page    = to_color(s->nextpage_color);
    return t;
}

Theme get_theme(const std::string& name) {
    Config cfg;
    cfg.theme = name;
    cfg.load_themes(data_path("themes.json"));
    return build_theme_from_config(cfg);
}

Theme make_light_theme() { return get_theme("aqua"); }
Theme make_dark_theme()  { return get_theme("dark_temple"); }

} // namespace cxxime
