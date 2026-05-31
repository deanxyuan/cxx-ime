// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <cxxime/config_monitor.h>
#include <cxxime/config_notify.h>

// Helper: wait for condition with timeout
static bool wait_for(std::atomic<int>& val, int expected, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (val.load() < expected) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

TEST(ConfigMonitor, initialize_and_destroy) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());
}

TEST(ConfigMonitor, initialize_idempotent) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());
    ASSERT_TRUE(m.initialize());  // second call is no-op
}

TEST(ConfigMonitor, start_stop) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());

    std::atomic<int> count{0};
    m.start([&]() { count.fetch_add(1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    m.stop();
    // No config change, callback should not have been called
    ASSERT_EQ(count.load(), 0);
}

TEST(ConfigMonitor, callback_on_config_change) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());

    std::atomic<int> count{0};
    m.start([&]() { count.fetch_add(1); });

    // Give watcher thread time to enter WaitForSingleObject
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Trigger config change
    cxxime::notify_config_changed();

    // Wait for callback (watcher has 100ms timeout, notify sleeps 200ms)
    ASSERT_TRUE(wait_for(count, 1, 1000));
    ASSERT_EQ(count.load(), 1);

    m.stop();
}

TEST(ConfigMonitor, callback_not_duplicated) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());

    std::atomic<int> count{0};
    m.start([&]() { count.fetch_add(1); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Trigger change once
    cxxime::notify_config_changed();
    ASSERT_TRUE(wait_for(count, 1, 1000));

    // Wait a bit more — callback should NOT fire again for the same version
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ASSERT_EQ(count.load(), 1);

    m.stop();
}

TEST(ConfigMonitor, multiple_changes) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());

    std::atomic<int> count{0};
    m.start([&]() { count.fetch_add(1); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Two separate changes
    cxxime::notify_config_changed();
    ASSERT_TRUE(wait_for(count, 1, 1000));

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    cxxime::notify_config_changed();
    ASSERT_TRUE(wait_for(count, 2, 1000));

    ASSERT_EQ(count.load(), 2);
    m.stop();
}

TEST(ConfigMonitor, start_idempotent) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());

    std::atomic<int> count{0};
    m.start([&]() { count.fetch_add(1); });
    m.start([&]() { count.fetch_add(100); });  // should be no-op

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cxxime::notify_config_changed();
    ASSERT_TRUE(wait_for(count, 1, 1000));

    // If second start() was not a no-op, count would be >= 100
    ASSERT_EQ(count.load(), 1);
    m.stop();
}

TEST(ConfigMonitor, stop_without_start) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());
    m.stop();  // should be safe, no crash
}

TEST(ConfigMonitor, ref_count_basic) {
    cxxime::ConfigMonitor m;
    ASSERT_TRUE(m.initialize());
    ASSERT_EQ(m.ref_count(), 0);
    m.add_ref();
    ASSERT_EQ(m.ref_count(), 1);
    m.add_ref();
    ASSERT_EQ(m.ref_count(), 2);
    m.dec_ref();
    ASSERT_EQ(m.ref_count(), 1);
    // Don't dec_ref to 0 — delete this on stack object is UB
}

TEST(ConfigMonitor, dec_ref_stops_watcher) {
    auto* m = new cxxime::ConfigMonitor();
    ASSERT_TRUE(m->initialize());

    std::atomic<int> count{0};
    m->start([&]() { count.fetch_add(1); });
    m->add_ref();  // ref_count = 1

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cxxime::notify_config_changed();
    ASSERT_TRUE(wait_for(count, 1, 1000));

    m->dec_ref();  // ref_count = 0 → stop() + delete this
    // m is now deleted, watcher stopped

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cxxime::notify_config_changed();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // No crash means watcher was properly stopped before delete
}

RUN_ALL_TESTS()
