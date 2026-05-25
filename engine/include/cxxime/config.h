// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONFIG_H_
#define CXXIME_CONFIG_H_

#include <string>
#include <unordered_map>

namespace cxxime {

struct LayoutConfig {
    int min_width = 160;
    int max_width = 0;
    int max_height = 0;
    int margin_x = 12;
    int margin_y = 12;
    int spacing = 10;          // preedit to candidates gap
    int candidate_spacing = 8; // between candidate cells
    int hilite_spacing = 4;    // inner gap: label↔text
    int hilite_padding_x = 4;  // highlight rect horizontal padding (InflateRect)
    int hilite_padding_y = 2;  // highlight rect vertical padding (InflateRect)
    int round_corner = 4;      // highlight rect corner radius
    int round_corner_ex = 4;   // window corner radius
    int border_width = 1;      // window border
    int label_font_point = 0;  // label/preedit font size, 0 = use font_point
    std::string align_type = "center";
};

struct Config {
    bool load(const std::string& path);
    bool load_themes(const std::string& path);  // load themes.json separately

    // engine
    int page_size = 9;

    // style
    std::string font_name = "Microsoft YaHei UI";
    int font_size = 14;
    std::string layout = "horizontal";  // horizontal | vertical
    bool inline_preedit = true;
    std::string preedit_type = "composition";

    // theme
    std::string theme = "azure";

    // layout (spacing and sizing)
    LayoutConfig layout_config;

    // ascii_composer
    std::unordered_map<std::string, std::string> ascii_switch_key;
    bool good_old_caps_lock = false;

    // Color scheme loaded from themes.json.
    // Fields default to -1 = "not set" (resolved to Weasel-style fallbacks in load_themes).
    struct SchemeColors {
        int text_color = -1;
        int back_color = -1;
        int border_color = -1;
        int candidate_text_color = -1;
        int label_text_color = -1;
        int hilited_text_color = -1;
        int hilited_back_color = -1;
        int hilited_candidate_text_color = -1;
        int hilited_candidate_back_color = -1;
        int comment_text_color = -1;
        int prevpage_color = -1;
        int nextpage_color = -1;
    };
    std::unordered_map<std::string, SchemeColors> preset_color_schemes;
};

} // namespace cxxime

#endif // CXXIME_CONFIG_H_
