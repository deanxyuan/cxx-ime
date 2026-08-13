// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "diagnostic_log_maintenance_worker.h"

#include <chrono>

#include <cxxime/diagnostic_log_maintenance.h>
#include <cxxime/diagnostic_log_path.h>
#include <cxxime/diagnostics_config.h>

DiagnosticLogMaintenanceWorker::~DiagnosticLogMaintenanceWorker() { stop(); }

void DiagnosticLogMaintenanceWorker::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) {
        return;
    }
    stop_requested_ = false;
    thread_ = std::thread(&DiagnosticLogMaintenanceWorker::run, this);
}

void DiagnosticLogMaintenanceWorker::stop() {
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stop_requested_ = true;
        condition_.notify_all();
        thread = std::move(thread_);
    }
    thread.join();
}

void DiagnosticLogMaintenanceWorker::run() {
    constexpr auto kInterval = std::chrono::hours(6);
    while (true) {
        cxxime::cleanup_diagnostic_log_directory(
            cxxime::diagnostic_log_directory(),
            cxxime::diagnostic_log_retention_options(cxxime::diagnostics_config()));

        std::unique_lock<std::mutex> lock(mutex_);
        if (condition_.wait_for(lock, kInterval, [this] { return stop_requested_; })) {
            return;
        }
    }
}
