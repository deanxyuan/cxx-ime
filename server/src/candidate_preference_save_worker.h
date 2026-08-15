// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_PREFERENCE_SAVE_WORKER_H_
#define CXXIME_CANDIDATE_PREFERENCE_SAVE_WORKER_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class CandidatePreferenceSaveWorker {
public:
    using SaveCallback = std::function<bool(bool force)>;

    ~CandidatePreferenceSaveWorker();
    bool start(SaveCallback callback);
    void stop();

private:
    void run();

    SaveCallback callback_;
    std::condition_variable condition_;
    std::mutex mutex_;
    std::thread thread_;
    bool stopping_ = false;
};

#endif // CXXIME_CANDIDATE_PREFERENCE_SAVE_WORKER_H_
