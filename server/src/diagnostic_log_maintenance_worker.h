// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_DIAGNOSTIC_LOG_MAINTENANCE_WORKER_H_
#define CXXIME_SERVER_DIAGNOSTIC_LOG_MAINTENANCE_WORKER_H_

#include <condition_variable>
#include <mutex>
#include <thread>

class DiagnosticLogMaintenanceWorker {
public:
    DiagnosticLogMaintenanceWorker() = default;
    ~DiagnosticLogMaintenanceWorker();

    DiagnosticLogMaintenanceWorker(const DiagnosticLogMaintenanceWorker&) = delete;
    DiagnosticLogMaintenanceWorker& operator=(const DiagnosticLogMaintenanceWorker&) = delete;

    void start();
    void stop();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    bool stop_requested_ = false;
};

#endif // CXXIME_SERVER_DIAGNOSTIC_LOG_MAINTENANCE_WORKER_H_
