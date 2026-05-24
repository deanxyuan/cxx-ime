// dict_query — interactive dictionary lookup tool for pinyin and wubi
// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/dict.h>
#include <cxxime/translator.h>
#include <cxxime/syllabifier.h>
#include <cxxime/spellings_index.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

static void print_usage() {
    std::puts("Usage: dict_query --mode pinyin|wubi [--dict <path>] [--spellings <path>]");
    std::puts("");
    std::puts("  --mode pinyin   Load dict + spellings trie, use PinyinTranslator");
    std::puts("  --mode wubi     Load dict (binary). Use sqlite_query to read .db directly");
    std::puts("  --dict <path>   Dictionary file path");
    std::puts("  --spellings <path>  Spellings trie path (pinyin mode only)");
    std::puts("");
    std::puts("Interactive commands:");
    std::puts("  <input>    Look up candidates");
    std::puts("  :q         Quit");
    std::puts("  :s <code>  Show segmentation (pinyin mode)");
}

static std::string to_utf8(const std::string& s) { return s; }

// ─── Wubi mode (binary dict) ──────────────────────────────────────────

static void run_wubi_binary(const std::string& dict_path) {
    cxxime::Dict dict;
    if (!dict.open_dict(dict_path)) {
        std::fprintf(stderr, "ERROR: Cannot open dict: %s\n", dict_path.c_str());
        return;
    }

    std::puts("Wubi mode (binary). Type :q to quit.\n");

    char line[256];
    for (;;) {
        std::fputs("> ", stdout);
        std::fflush(stdout);
        if (!std::fgets(line, (int)sizeof(line), stdin))
            break;

        std::string input(line);
        while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
            input.pop_back();
        if (input.empty())
            continue;
        if (input == ":q")
            break;

        auto results = dict.lookup(input, 20);
        int idx = 0;
        for (auto& c : results) {
            std::string code = dict.reverse_lookup(c.text);
            std::printf("[%d] %s  (%s, %d)\n", idx++,
                        c.text.c_str(), code.c_str(), c.frequency);
        }
        if (idx == 0)
            std::puts("  (no matches)");
    }
    dict.close();
}

// ─── Pinyin mode ──────────────────────────────────────────────────────

static void run_pinyin(const std::string& dict_path, const std::string& spellings_path) {
    cxxime::Dict dict;
    if (!dict.open_dict(dict_path)) {
        std::fprintf(stderr, "ERROR: Cannot open dict: %s\n", dict_path.c_str());
        return;
    }

    cxxime::SpellingsIndex spellings;
    cxxime::Syllabifier* syllabifier = nullptr;
    std::unique_ptr<cxxime::Syllabifier> syllabifier_owner;

    if (!spellings_path.empty()) {
        if (spellings.load(spellings_path) && spellings.has_spellings()) {
            syllabifier_owner = std::make_unique<cxxime::Syllabifier>(spellings);
            syllabifier = syllabifier_owner.get();
            std::puts("Spellings trie loaded.");
        } else {
            std::puts("WARNING: Spellings not loaded, abbreviation expansion disabled.");
        }
    }

    cxxime::PinyinTranslator translator;
    translator.set_dict(&dict);
    if (syllabifier)
        translator.set_syllabifier(syllabifier);

    std::puts("Pinyin mode. Type :q to quit, :s <code> to show segmentation.\n");

    char line[256];
    for (;;) {
        std::fputs("> ", stdout);
        std::fflush(stdout);
        if (!std::fgets(line, (int)sizeof(line), stdin))
            break;

        std::string input(line);
        while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
            input.pop_back();
        if (input.empty())
            continue;
        if (input == ":q")
            break;

        if (input.size() > 2 && input[0] == ':' && input[1] == 's') {
            // Show segmentation
            std::string code = input.substr(3);
            if (syllabifier && !code.empty()) {
                auto paths = syllabifier->segment(code);
                std::printf("  %zu path(s):\n", paths.size());
                for (size_t i = 0; i < paths.size(); ++i) {
                    std::printf("  [%zu] ", i);
                    for (size_t j = 0; j < paths[i].size(); ++j) {
                        if (j > 0) std::fputs(":", stdout);
                        std::fputs(paths[i][j].c_str(), stdout);
                    }
                    std::putchar('\n');
                }
            } else {
                std::puts("  (no syllabifier loaded)");
            }
            continue;
        }

        auto page = translator.translate(input, 0, 20);
        int idx = 0;
        for (auto& c : page.candidates) {
            std::printf("[%d] %s  (%d)\n", idx++, c.text.c_str(), c.frequency);
        }
        if (idx == 0)
            std::puts("  (no matches)");
    }
    dict.close();
}

// ─── Main ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    std::string mode;
    std::string dict_path;
    std::string spellings_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc)
            mode = argv[++i];
        else if (std::strcmp(argv[i], "--dict") == 0 && i + 1 < argc)
            dict_path = argv[++i];
        else if (std::strcmp(argv[i], "--spellings") == 0 && i + 1 < argc)
            spellings_path = argv[++i];
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    if (mode.empty()) {
        std::fputs("ERROR: --mode is required (pinyin or wubi)\n", stderr);
        return 1;
    }

    // Resolve exe directory for default paths
    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string exe_dir(exe_path);
    auto pos = exe_dir.find_last_of("\\/");
    if (pos != std::string::npos)
        exe_dir = exe_dir.substr(0, pos);

    auto resolve_path = [&](const std::string& arg, const char* filename) -> std::string {
        if (!arg.empty())
            return arg;
        // Try exe_dir/data/, then walk up ../data/ until found
        std::string base = exe_dir;
        for (int up = 0; up < 6; ++up) {
            std::string c = base + "\\data\\" + filename;
            if (GetFileAttributesA(c.c_str()) != INVALID_FILE_ATTRIBUTES)
                return c;
            base += "\\..";
        }
        return std::string(exe_dir) + "\\data\\" + filename;  // fallback
    };

    if (mode == "pinyin") {
        dict_path = resolve_path(dict_path, "pinyin.dict.bin");
        spellings_path = resolve_path(spellings_path, "pinyin.spellings.bin");
        run_pinyin(dict_path, spellings_path);
    } else if (mode == "wubi") {
        dict_path = resolve_path(dict_path, "wubi86.dict.bin");
        run_wubi_binary(dict_path);
    } else {
        std::fprintf(stderr, "ERROR: Unknown mode '%s'. Use pinyin or wubi.\n", mode.c_str());
        return 1;
    }

    return 0;
}
