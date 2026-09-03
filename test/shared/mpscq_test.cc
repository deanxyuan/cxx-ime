// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Ported from gRPC mpscq_test.cc (Apache 2.0, gRPC authors).

#include "support/testutil.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <cxxime/mpscq.h>

struct TestNode : cxxime::MPSCQueue::Node {
    size_t i;
    std::atomic<size_t>* ctr;
};

static TestNode* new_node(size_t i, std::atomic<size_t>* ctr) {
    auto* n = new TestNode();
    n->i = i;
    n->ctr = ctr;
    return n;
}

// Simple barrier using mutex+cv (C++17 compatible)
class SimpleBarrier {
public:
    explicit SimpleBarrier(int count) : count_(count), remaining_(count) {}
    void arrive_and_wait() {
        std::unique_lock<std::mutex> lock(mu_);
        if (--remaining_ == 0) {
            gen_++;
            remaining_ = count_;
            cv_.notify_all();
        } else {
            int gen = gen_;
            cv_.wait(lock, [&] { return gen_ != gen; });
        }
    }
private:
    std::mutex mu_;
    std::condition_variable cv_;
    int count_;
    int remaining_;
    int gen_ = 0;
};

// Serial: push N, pop N, verify FIFO order
TEST(MPSCQ, serial) {
    constexpr size_t kCount = 100000;
    cxxime::MPSCQueue q;

    for (size_t i = 0; i < kCount; ++i) {
        q.push(new_node(i, nullptr));
    }
    for (size_t i = 0; i < kCount; ++i) {
        auto* n = reinterpret_cast<TestNode*>(q.pop());
        ASSERT_TRUE(n != nullptr);
        ASSERT_EQ(n->i, i);
        delete n;
    }
    ASSERT_TRUE(q.pop() == nullptr);
}

// Multi-producer single-consumer: N threads push, 1 thread pops.
// Each thread pushes sequentially; consumer verifies per-thread ordering.
TEST(MPSCQ, multi_producer_single_consumer) {
    constexpr int kThreads = 8;
    constexpr size_t kIterations = 10000;

    std::atomic<size_t> counters[kThreads] = {};
    cxxime::MPSCQueue q;
    SimpleBarrier sync(kThreads + 1);  // +1 for consumer

    auto producer = [&](int idx) {
        sync.arrive_and_wait();
        for (size_t i = 1; i <= kIterations; ++i) {
            q.push(new_node(i, &counters[idx]));
        }
    };

    // Start producers
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back(producer, t);

    // Consumer: pop until all threads done
    sync.arrive_and_wait();
    size_t num_done = 0;
    size_t spins = 0;
    while (num_done != kThreads) {
        cxxime::MPSCQueue::Node* n;
        while ((n = q.pop()) == nullptr)
            ++spins;
        auto* tn = reinterpret_cast<TestNode*>(n);
        ASSERT_TRUE(tn->ctr->load() == tn->i - 1);
        tn->ctr->store(tn->i);
        if (tn->i == kIterations) ++num_done;
        delete tn;
    }

    for (auto& th : threads)
        th.join();

    std::fprintf(stderr, "  spins: %zu\n", spins);
}

// Multi-producer multi-consumer (pop serialized via mutex).
// Verifies correctness when multiple threads call pop().
TEST(MPSCQ, multi_producer_multi_consumer) {
    constexpr int kProducers = 8;
    constexpr int kConsumers = 4;
    constexpr size_t kIterations = 10000;

    std::atomic<size_t> counters[kProducers] = {};
    cxxime::MPSCQueue q;
    SimpleBarrier sync(kProducers + kConsumers);

    auto producer = [&](int idx) {
        sync.arrive_and_wait();
        for (size_t i = 1; i <= kIterations; ++i) {
            q.push(new_node(i, &counters[idx]));
        }
    };

    struct SharedState {
        std::mutex mu;
        size_t num_done = 0;
        size_t spins = 0;
    } state;

    auto consumer = [&]() {
        sync.arrive_and_wait();
        for (;;) {
            std::lock_guard<std::mutex> lock(state.mu);
            if (state.num_done == kProducers)
                return;
            cxxime::MPSCQueue::Node* n;
            while ((n = q.pop()) == nullptr)
                ++state.spins;
            auto* tn = reinterpret_cast<TestNode*>(n);
            ASSERT_TRUE(tn->ctr->load() == tn->i - 1);
            tn->ctr->store(tn->i);
            if (tn->i == kIterations) ++state.num_done;
            delete tn;
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kProducers; ++t)
        threads.emplace_back(producer, t);
    for (int t = 0; t < kConsumers; ++t)
        threads.emplace_back(consumer);

    for (auto& th : threads)
        th.join();

    std::fprintf(stderr, "  spins: %zu\n", state.spins);
}

RUN_ALL_TESTS()
