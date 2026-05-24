// sqlite_query — interactive SQLite .db dictionary reader
// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
#include <sqlite3.h>

static void print_usage() {
    std::puts("Usage: sqlite_query <dict.db>");
    std::puts("");
    std::puts("Interactive SQLite dictionary reader.");
    std::puts("");
    std::puts("Commands:");
    std::puts("  <input>    Look up candidates by code prefix");
    std::puts("  :q         Quit");
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string db_path = argv[1];

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(db_path.c_str(), &db,
                              SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        std::fprintf(stderr, "ERROR: Cannot open %s: %s\n",
                     db_path.c_str(), sqlite3_errmsg(db));
        return 1;
    }

    std::printf("Opened: %s\nType :q to quit.\n\n", db_path.c_str());

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

        const char* sql =
            "SELECT text, code, frequency FROM dict "
            "WHERE code LIKE ?1 "
            "ORDER BY frequency DESC LIMIT 20";
        sqlite3_stmt* stmt = nullptr;
        std::string pattern = input + "%";
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            continue;
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

        int idx = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* text = (const char*)sqlite3_column_text(stmt, 0);
            const char* code = (const char*)sqlite3_column_text(stmt, 1);
            int freq = sqlite3_column_int(stmt, 2);
            std::printf("[%d] %s  (%s, %d)\n", idx++,
                        text ? text : "?", code ? code : "?", freq);
        }
        if (idx == 0)
            std::puts("  (no matches)");
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return 0;
}
