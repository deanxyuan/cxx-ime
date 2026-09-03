// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_ENGINE_TEST_SUPPORT_H_
#define CXXIME_TEST_ENGINE_TEST_SUPPORT_H_

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/engine.h>
#include <cxxime/input_limits.h>
#include <cxxime/key_event.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/translator.h>
#include <cxxime/wubi_translator.h>

#include "support/testutil.h"

inline char engine_test_temp_path[MAX_PATH] = {};

inline std::string make_temp_path(const char* name) {
    return std::string(engine_test_temp_path) + "\\" + name;
}

inline void type_code(cxxime::Engine& engine, const std::string& code) {
    for (char ch : code) {
        cxxime::KeyEvent event;
        event.keycode = static_cast<uint32_t>(std::toupper(static_cast<unsigned char>(ch)));
        event.is_key_up = false;
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }
}

#endif // CXXIME_TEST_ENGINE_TEST_SUPPORT_H_
