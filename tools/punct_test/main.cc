// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Punctuation & full-width preview tool — test engine conversion without IPC.
// Usage: punct_test [--data <dir>]

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <json.hpp>
#include <cxxime/output_composer.h>
#include <cxxime/punct_types.h>
#include <cxxime/data_path.h>

namespace {

// Punctuation mapping loaded from punctuation.json
struct PunctMappingLocal {
    std::unordered_map<std::string, cxxime::PunctEntry> half_shape;
    std::unordered_map<std::string, cxxime::PunctEntry> full_shape;
};

PunctMappingLocal g_punct;
bool g_chinese_punct = true;
bool g_full_shape = false;
// Pair/alternatives state (persists across characters, like the real engine)
std::unordered_map<std::string, bool> g_pair_open;
std::unordered_map<std::string, int> g_alt_index;

void parse_section(const nlohmann::json& j, const char* section,
                   std::unordered_map<std::string, cxxime::PunctEntry>& target) {
    if (!j.contains(section) || !j[section].is_object())
        return;
    for (auto it = j[section].begin(); it != j[section].end(); ++it) {
        std::string key = it.key();
        const auto& val = it.value();
        if (!val.is_object()) continue;
        cxxime::PunctEntry entry{};
        if (val.contains("commit") && val["commit"].is_string()) {
            entry.type = cxxime::PunctType::COMMIT;
            entry.commit = val["commit"].get<std::string>();
        } else if (val.contains("pair") && val["pair"].is_array()) {
            entry.type = cxxime::PunctType::PAIR;
            for (const auto& item : val["pair"])
                if (item.is_string()) entry.pair.push_back(item.get<std::string>());
        } else if (val.contains("alternatives") && val["alternatives"].is_array()) {
            entry.type = cxxime::PunctType::ALTERNATIVES;
            for (const auto& item : val["alternatives"])
                if (item.is_string()) entry.alternatives.push_back(item.get<std::string>());
        } else {
            continue;
        }
        target[key] = std::move(entry);
    }
}

bool load_punctuation(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        printf("Cannot open %s\n", path.c_str());
        return false;
    }
    try {
        auto j = nlohmann::json::parse(file);
        parse_section(j, "half_shape", g_punct.half_shape);
        parse_section(j, "full_shape", g_punct.full_shape);
        printf("Loaded punctuation: %zu half_shape, %zu full_shape entries\n",
               g_punct.half_shape.size(), g_punct.full_shape.size());
        return true;
    } catch (const std::exception& e) {
        printf("Parse error: %s\n", e.what());
        return false;
    }
}

// Try punctuation mapping. Returns output string, or empty if no match.
std::string try_punct(char ch) {
    std::string key(1, ch);
    const cxxime::PunctEntry* entry = nullptr;

    if (g_chinese_punct) {
        auto it = g_punct.half_shape.find(key);
        if (it != g_punct.half_shape.end())
            entry = &it->second;
    } else if (g_full_shape) {
        auto it = g_punct.full_shape.find(key);
        if (it != g_punct.full_shape.end())
            entry = &it->second;
    }

    if (!entry) return "";

    switch (entry->type) {
    case cxxime::PunctType::COMMIT:
        return entry->commit;
    case cxxime::PunctType::PAIR: {
        bool opened = g_pair_open[key];
        int idx = opened ? 1 : 0;
        g_pair_open[key] = !opened;
        return entry->pair[idx];
    }
    case cxxime::PunctType::ALTERNATIVES: {
        int idx = g_alt_index[key] % static_cast<int>(entry->alternatives.size());
        g_alt_index[key] = idx + 1;
        return entry->alternatives[idx];
    }
    }
    return "";
}

// Decode UTF-8 string to code points and format as "U+XXXX" per character.
std::string format_codepoints(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        unsigned char b = static_cast<unsigned char>(s[i]);
        int len = 0;
        if (b < 0x80)       { cp = b; len = 1; }
        else if (b < 0xE0)  { cp = b & 0x1F; len = 2; }
        else if (b < 0xF0)  { cp = b & 0x0F; len = 3; }
        else                { cp = b & 0x07; len = 4; }
        for (int j = 1; j < len && i + j < s.size(); ++j)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3F);
        if (!out.empty()) out += ' ';
        char buf[16];
        snprintf(buf, sizeof(buf), "U+%04X", cp);
        out += buf;
        i += len;
    }
    return out;
}

void print_result(const std::string& result) {
    printf("  -> %s  [%s]\n", result.c_str(), format_codepoints(result).c_str());
}

void process_char(char ch) {
    // 1. Try punctuation mapping first
    std::string result = try_punct(ch);
    if (!result.empty()) {
        print_result(result);
        return;
    }

    // 2. Try full-width conversion
    if (g_full_shape && ch >= 0x20 && ch <= 0x7e) {
        result = cxxime::OutputComposer::to_full_width(ch);
        print_result(result);
        return;
    }

    // 3. No conversion
    printf("  -> %c  [U+%04X]\n", ch, static_cast<unsigned char>(ch));
}

void print_status() {
    printf("chinese_punct=%d  full_shape=%d\n", g_chinese_punct, g_full_shape);
}

void print_help() {
    printf("Commands:\n");
    printf("  p    — toggle chinese/english punctuation (中文/英文标点)\n");
    printf("  f    — toggle full/half shape (全角/半角)\n");
    printf("  s    — show current status\n");
    printf("  r    — reset pair/alternatives state\n");
    printf("  h    — show this help\n");
    printf("  q    — quit\n");
    printf("\n");
    printf("Any other input is processed character by character.\n");
    printf("Special characters: . , ; / ' \" - < > [ ] = \\\n");
}

} // namespace

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Parse --data argument
    for (int i = 1; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--data") == 0) {
            cxxime::set_data_dir(argv[i + 1]);
            break;
        }
    }

    // Load punctuation
    std::string punct_path = cxxime::data_path("punctuation.json");
    if (!load_punctuation(punct_path))
        return 1;

    print_help();
    print_status();
    printf("\n> ");
    fflush(stdout);

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        // Strip newline
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) {
            printf("> ");
            fflush(stdout);
            continue;
        }

        // Single-char commands
        if (len == 1) {
            switch (line[0]) {
            case 'q': return 0;
            case 'p':
                g_chinese_punct = !g_chinese_punct;
                printf("chinese_punct = %d (%s)\n", g_chinese_punct,
                       g_chinese_punct ? "中文标点" : "英文标点");
                printf("> ");
                fflush(stdout);
                continue;
            case 'f':
                g_full_shape = !g_full_shape;
                printf("full_shape = %d (%s)\n", g_full_shape,
                       g_full_shape ? "全角" : "半角");
                printf("> ");
                fflush(stdout);
                continue;
            case 's':
                print_status();
                printf("> ");
                fflush(stdout);
                continue;
            case 'r':
                g_pair_open.clear();
                g_alt_index.clear();
                printf("State reset.\n");
                printf("> ");
                fflush(stdout);
                continue;
            case 'h':
                print_help();
                printf("> ");
                fflush(stdout);
                continue;
            }
        }

        // Process each character in the input line
        for (size_t i = 0; i < len; ++i) {
            char ch = line[i];
            if (ch >= 0x20 && ch <= 0x7e) {
                process_char(ch);
            }
        }
        printf("> ");
        fflush(stdout);
    }

    return 0;
}
