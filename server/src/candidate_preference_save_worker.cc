// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "candidate_preference_save_worker.h"

#include <chrono>
#include <utility>

#include <cxxime/logging.h>

CandidatePreferenceSaveWorker::~CandidatePreferenceSaveWorker() { stop(); }

bool CandidatePreferenceSaveWorker::start(SaveCallback callback) {
    if (!callback || thread_.joinable()) {
        return false;
    }
    callback_ = std::move(callback);
    stopping_ = false;
    thread_ = std::thread([this]() { run(); });
    return true;
}

void CandidatePreferenceSaveWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stopping_ = true;
    }
    condition_.notify_one();
    thread_.join();
    callback_ = {};
}

void CandidatePreferenceSaveWorker::run() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (condition_.wait_for(lock, std::chrono::seconds(1),
                                    [this]() { return stopping_; })) {
                break;
            }
        }
        if (!callback_(false)) {
            CXXIME_LOG(L"%s", L"candidate_preference save result=0 force=0");
        }
    }
    if (!callback_(true)) {
        CXXIME_LOG(L"%s", L"candidate_preference save result=0 force=1");
    }
}
