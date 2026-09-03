// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <windows.h>

#include <cxxime/dict.h>

#include "candidate_preference_save_worker.h"
#include "support/testutil.h"

namespace {

std::string make_temp_path(const char* prefix) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, prefix, 0, path) == 0) {
        return {};
    }
    return path;
}

} // namespace

TEST(CandidatePreferenceSaveWorker, coalesces_updates_and_flushes_complete_frequency) {
    const std::string preference_path = make_temp_path("cps");
    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));

    cxxime::Candidate candidate;
    candidate.text = "你好";
    candidate.code = "nihao";
    candidate.source = cxxime::CandidateSource::kPinyin;
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(dictionary.record_candidate_preference(candidate, "nihao"));
    }

    std::atomic<int> periodic_saves{0};
    std::atomic<int> forced_saves{0};
    CandidatePreferenceSaveWorker worker;
    ASSERT_TRUE(worker.start([&](bool force) {
        if (force) {
            forced_saves.fetch_add(1);
            return dictionary.save_candidate_preferences();
        }
        periodic_saves.fetch_add(1);
        return dictionary.save_candidate_preferences_if_due(std::chrono::milliseconds(0));
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    worker.stop();

    ASSERT_LE(periodic_saves.load(), 2);
    ASSERT_EQ(forced_saves.load(), 1);
    dictionary.close();

    cxxime::Dict reloaded;
    ASSERT_TRUE(reloaded.load_candidate_preferences(preference_path));
    const auto entries = reloaded.query_candidate_preferences("nihao", 0, 10);
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(entries[0].frequency, 1000);
    reloaded.close();
    DeleteFileA(preference_path.c_str());
}

RUN_ALL_TESTS()
