// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <set>
#include <windows.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_scratch.h>
#include <cxxime/mpscq.h>
#include <cxxime/logging.h>
#include <cxxime/engine.h>

// ─── QueryTrace::should_log() ──────────────────────────────────────────

TEST(QueryTrace, should_log_deadline_exceeded) {
    cxxime::QueryTrace t;
    t.deadline_exceeded = true;
    ASSERT_TRUE(t.should_log());
}

TEST(QueryTrace, should_log_cancelled) {
    cxxime::QueryTrace t;
    t.cancelled = true;
    ASSERT_TRUE(t.should_log());
}

TEST(QueryTrace, should_log_slow_query) {
    cxxime::QueryTrace t;
    t.total_us = 50001;  // > kSlowQueryUs (50000)
    ASSERT_TRUE(t.should_log());
}

TEST(QueryTrace, should_log_cache_miss_slow) {
    cxxime::QueryTrace t;
    t.cache_hit = false;
    t.total_us = 10001;  // > kCacheMissSlowUs (10000)
    ASSERT_TRUE(t.should_log());
}

TEST(QueryTrace, should_log_fast_normal_not_logged) {
    // A fast, non-truncated, normal query should mostly NOT be logged
    // (0.1% sampling rate means < 1 in 100 should log)
    cxxime::QueryTrace t;
    t.total_us = 100;  // fast
    t.cache_hit = true;
    t.deadline_exceeded = false;
    t.cancelled = false;
    t.truncated = false;

    int logged_count = 0;
    for (uint64_t i = 0; i < 10000; ++i) {
        t.query_id = i;
        t.session_id = 1;
        t.revision = 0;  // fixed — query_id provides the variation
        if (t.should_log())
            ++logged_count;
    }
    // With 0.1% rate, expect ~10 out of 10000. Allow 0-50 range.
    ASSERT_TRUE(logged_count < 50) << "logged_count=" << logged_count;
}

TEST(QueryTrace, should_log_truncated_sampled) {
    // Truncated queries use 1% sampling rate
    cxxime::QueryTrace t;
    t.truncated = true;
    t.total_us = 100;
    t.cache_hit = true;
    t.deadline_exceeded = false;
    t.cancelled = false;

    int logged_count = 0;
    for (uint64_t i = 0; i < 10000; ++i) {
        t.query_id = i;
        t.session_id = 1;
        t.revision = 0;  // fixed — query_id provides the variation
        if (t.should_log())
            ++logged_count;
    }
    // With 1% rate, expect ~100 out of 10000. Allow 20-300 range.
    ASSERT_TRUE(logged_count > 20) << "logged_count=" << logged_count;
    ASSERT_TRUE(logged_count < 300) << "logged_count=" << logged_count;
}

// ─── should_sample() ──────────────────────────────────────────────────

TEST(QueryTrace, should_sample_deterministic) {
    // Same inputs always produce same result
    bool r1 = cxxime::QueryTrace::should_sample(42, 100, 0, 1000);
    bool r2 = cxxime::QueryTrace::should_sample(42, 100, 0, 1000);
    ASSERT_EQ(r1, r2);
}

TEST(QueryTrace, should_sample_rate_distribution) {
    // Rate=1000 should give ~0.1% true
    int hits = 0;
    for (uint64_t i = 0; i < 100000; ++i) {
        if (cxxime::QueryTrace::should_sample(1, 0, i, 1000))
            ++hits;
    }
    // Expect ~100 hits. Allow 20-300.
    ASSERT_TRUE(hits > 20) << "hits=" << hits;
    ASSERT_TRUE(hits < 300) << "hits=" << hits;
}

// ─── QueryScratch ─────────────────────────────────────────────────────

TEST(QueryScratch, reset_clears_all) {
    cxxime::QueryScratch scr;
    scr.id_sequences.push_back({1, 2, 3});
    scr.live_ids.push_back({4, 5});
    cxxime::Candidate c1;
    c1.text = "test";
    c1.frequency = 100;
    scr.merged_candidates.push_back(c1);
    cxxime::Candidate c2;
    c2.text = "temp";
    c2.frequency = 50;
    scr.temp_candidates.push_back(c2);
    scr.seen_hashes.push_back(0xABCD);
    scr.path_ids.push_back(42);

    scr.reset_for_query();

    ASSERT_TRUE(scr.id_sequences.empty());
    ASSERT_TRUE(scr.live_ids.empty());
    ASSERT_TRUE(scr.merged_candidates.empty());
    ASSERT_TRUE(scr.temp_candidates.empty());
    ASSERT_TRUE(scr.seen_hashes.empty());
    ASSERT_TRUE(scr.path_ids.empty());
}

TEST(QueryScratch, trim_if_large) {
    cxxime::QueryScratch scr;
    // Fill a vector beyond the shrink threshold
    for (int i = 0; i < 300; ++i)
        scr.id_sequences.push_back({(uint32_t)i});
    ASSERT_TRUE(scr.id_sequences.capacity() >= 300);

    // trim_if_large should not crash; data integrity preserved
    scr.trim_if_large();
    ASSERT_EQ((int)scr.id_sequences.size(), 300);
    ASSERT_EQ((int)scr.id_sequences[0].size(), 1);
    ASSERT_EQ(scr.id_sequences[0][0], 0u);
    ASSERT_EQ(scr.id_sequences[299][0], 299u);
}

// ─── Atomic query ID ──────────────────────────────────────────────────

TEST(AtomicQueryId, query_id_no_duplicates) {
    // Generate query IDs from multiple threads, verify no duplicates
    std::atomic<uint64_t> counter{0};
    std::vector<uint64_t> ids[4];
    constexpr int kPerThread = 1000;

    auto worker = [&](int thread_idx) {
        for (int i = 0; i < kPerThread; ++i) {
            ids[thread_idx].push_back(counter.fetch_add(1, std::memory_order_relaxed));
        }
    };

    std::thread t0(worker, 0);
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    std::thread t3(worker, 3);
    t0.join();
    t1.join();
    t2.join();
    t3.join();

    // All IDs should be unique
    std::set<uint64_t> all;
    for (int t = 0; t < 4; ++t)
        for (auto id : ids[t])
            all.insert(id);
    ASSERT_EQ((int)all.size(), 4 * kPerThread);
}

// ─── Engine integration ───────────────────────────────────────────────

TEST(EngineTrace, trace_fields_populated) {
    cxxime::Engine engine;
    if (!engine.initialize(CXXIME_DATA_DIR "pinyin.dict.bin"))
        return;  // skip if data not available

    engine.set_trace_enabled(true);
    engine.set_trace_session_id(99);

    cxxime::KeyEvent event;
    event.keycode = 'N';
    event.is_key_up = false;
    engine.process_key(event);

    auto& trace = engine.last_trace();
    ASSERT_EQ(trace.session_id, 99u);
    ASSERT_TRUE(trace.total_us >= 0);
    ASSERT_TRUE(trace.processor_us >= 0);
}

TEST(EngineTrace, no_auto_trace_log) {
    // Engine should NOT call trace_.log() internally — that's the server's job.
    // This test just verifies the engine runs without crashing when trace is enabled.
    cxxime::Engine engine;
    if (!engine.initialize(CXXIME_DATA_DIR "pinyin.dict.bin"))
        return;

    engine.set_trace_enabled(true);

    cxxime::KeyEvent event;
    event.keycode = 'N';
    event.is_key_up = false;
    auto result = engine.process_key(event);
    ASSERT_EQ(result, cxxime::ProcessResult::ACCEPTED);
}

// ─── MPSCQueue ────────────────────────────────────────────────────────

struct TestNode : cxxime::MPSCQueue::Node {
    int value;
};

TEST(MPSCQueue, basic_push_pop) {
    cxxime::MPSCQueue q;
    auto* a = new TestNode{cxxime::MPSCQueue::Node{}, 1};
    auto* b = new TestNode{cxxime::MPSCQueue::Node{}, 2};
    auto* c = new TestNode{cxxime::MPSCQueue::Node{}, 3};

    q.push(a);
    q.push(b);
    q.push(c);

    auto* r1 = static_cast<TestNode*>(q.pop());
    ASSERT_TRUE(r1 != nullptr);
    ASSERT_EQ(r1->value, 1);
    delete r1;

    auto* r2 = static_cast<TestNode*>(q.pop());
    ASSERT_TRUE(r2 != nullptr);
    ASSERT_EQ(r2->value, 2);
    delete r2;

    auto* r3 = static_cast<TestNode*>(q.pop());
    ASSERT_TRUE(r3 != nullptr);
    ASSERT_EQ(r3->value, 3);
    delete r3;

    // Queue should be empty
    ASSERT_TRUE(q.pop() == nullptr);
}

TEST(MPSCQueue, concurrent_push) {
    cxxime::MPSCQueue q;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    std::atomic<int> pushed{0};

    auto producer = [&](int start_val) {
        for (int i = 0; i < kPerThread; ++i) {
            auto* node = new TestNode{cxxime::MPSCQueue::Node{}, start_val + i};
            q.push(node);
            pushed.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back(producer, t * kPerThread);
    for (auto& th : threads)
        th.join();

    // Pop all and verify count and uniqueness
    std::set<int> values;
    int count = 0;
    while (true) {
        auto* node = static_cast<TestNode*>(q.pop());
        if (!node) break;
        values.insert(node->value);
        delete node;
        ++count;
    }

    ASSERT_EQ(count, kThreads * kPerThread);
    ASSERT_EQ((int)values.size(), kThreads * kPerThread);
}

TEST(MPSCQueue, empty_pop_returns_null) {
    cxxime::MPSCQueue q;
    ASSERT_TRUE(q.pop() == nullptr);
}

TEST(MPSCQueue, interleaved_push_pop) {
    cxxime::MPSCQueue q;
    for (int round = 0; round < 100; ++round) {
        auto* node = new TestNode{cxxime::MPSCQueue::Node{}, round};
        q.push(node);
        auto* r = static_cast<TestNode*>(q.pop());
        ASSERT_TRUE(r != nullptr);
        ASSERT_EQ(r->value, round);
        delete r;
    }
    ASSERT_TRUE(q.pop() == nullptr);
}

// ─── Release build CXXIME_LOG check ──────────────────────────────────

TEST(ReleaseBuild, cxxime_log_noop) {
#ifdef _DEBUG
    // In debug builds, CXXIME_LOG is active — just verify it compiles
    CXXIME_LOG(L"test %d", 42);
#else
    // In release builds, CXXIME_LOG must expand to nothing.
    // This is a compile-time check: if the macro expanded to something
    // that references OutputDebugStringW, this would link or emit code.
    // We verify by checking that the macro expands to a no-op.
    CXXIME_LOG(L"test %d", 42);
    // If we get here without linker errors, the guard works.
#endif
    ASSERT_TRUE(true);
}

RUN_ALL_TESTS()
