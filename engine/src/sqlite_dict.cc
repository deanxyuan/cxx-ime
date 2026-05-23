// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cxxime/dict.h>
#include <sqlite3.h>
#include <cstring>

namespace cxxime {

SqliteDict::~SqliteDict() {
    close();
}

bool SqliteDict::open(const std::string& db_path) {
    close();
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        db_ = nullptr;
        return false;
    }

    // Create tables if not exist
    const char* schema = "CREATE TABLE IF NOT EXISTS dict ("
                         "id INTEGER PRIMARY KEY, "
                         "text TEXT NOT NULL, "
                         "code TEXT NOT NULL, "
                         "frequency INTEGER DEFAULT 0);"
                         "CREATE INDEX IF NOT EXISTS idx_code ON dict(code);"
                         "CREATE TABLE IF NOT EXISTS user_dict ("
                         "id INTEGER PRIMARY KEY, "
                         "text TEXT NOT NULL, "
                         "code TEXT NOT NULL, "
                         "frequency INTEGER DEFAULT 1, "
                         "last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                         "UNIQUE(text, code));"
                         "CREATE INDEX IF NOT EXISTS idx_user_code ON user_dict(code);";

    char* err = nullptr;
    rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err);
    if (err) {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

void SqliteDict::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDict::is_open() const {
    return db_ != nullptr;
}

std::vector<Candidate> SqliteDict::lookup(const std::string& code_prefix, int limit) {
    std::vector<Candidate> results;
    if (!db_)
        return results;

    // Query system dict + user dict, merge and sort by frequency
    const char* sql =
        "SELECT text, code, frequency FROM dict WHERE code LIKE ?1 "
        "UNION ALL "
        "SELECT text, code, frequency FROM user_dict WHERE code LIKE ?1 "
        "ORDER BY frequency DESC LIMIT ?2";

    sqlite3_stmt* stmt = nullptr;
    std::string pattern = code_prefix + "%";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Candidate c;
            const char* text = (const char*)sqlite3_column_text(stmt, 0);
            const char* code = (const char*)sqlite3_column_text(stmt, 1);
            c.text = text ? text : "";
            c.frequency = sqlite3_column_int(stmt, 2);
            results.push_back(std::move(c));
        }
        sqlite3_finalize(stmt);
    }

    return results;
}

int SqliteDict::count(const std::string& code_prefix) {
    if (!db_)
        return 0;

    const char* sql = "SELECT COUNT(*) FROM ("
                      "SELECT text FROM dict WHERE code LIKE ?1 "
                      "UNION ALL "
                      "SELECT text FROM user_dict WHERE code LIKE ?1)";

    sqlite3_stmt* stmt = nullptr;
    int result = 0;
    std::string pattern = code_prefix + "%";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            result = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    return result;
}

std::string SqliteDict::reverse_lookup(const std::string& text) {
    if (!db_)
        return {};

    const char* sql = "SELECT code FROM dict WHERE text = ?1 LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    std::string result;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, text.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* code = (const char*)sqlite3_column_text(stmt, 0);
            result = code ? code : "";
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

void SqliteDict::update_frequency(const std::string& text, const std::string& code) {
    if (!db_)
        return;

    const char* sql = "INSERT INTO user_dict (text, code, frequency) VALUES (?1, ?2, 1) "
                      "ON CONFLICT(text, code) DO UPDATE SET frequency = frequency + 1, "
                      "last_used = CURRENT_TIMESTAMP";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, code.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

} // namespace cxxime
